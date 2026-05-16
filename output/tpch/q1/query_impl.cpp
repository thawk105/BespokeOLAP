// Query kernel for TPC-H Q1.
//
// All input columns are pre-filtered and stored as int64 scaled x100 for
// decimals; the (returnflag, linestatus) pair is pre-packed to a 0..5
// uint8 group_key. The kernel is a single pass over five arrays with
// direct-addressed accumulators (no hash, no probe).
//
// Decimal scale tracking:
//   l_quantity, l_extendedprice, l_discount, l_tax  -- scale 2 (int64)
//   sum_qty, sum_base_price, sum_disc                -- scale 2 (int64)
//   sum_disc_price = sum(ep * (100 - disc))         -- scale 4 (int128)
//   sum_charge     = sum(ep * (100 - disc) * (100 + tax)) -- scale 6 (int128)
//
// Output format (must match DuckDB exactly):
//   l_returnflag, l_linestatus,
//   sum_qty        2 decimals
//   sum_base_price 2 decimals
//   sum_disc_price 4 decimals
//   sum_charge     6 decimals
//   avg_qty, avg_price, avg_disc  -- shortest round-trip double
//   count_order int

#include "q1_types.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace {

using i128 = __int128;

constexpr int kNumSlots = 6;

struct Accumulators {
    int64_t  sum_qty[kNumSlots]        {};
    int64_t  sum_base_price[kNumSlots] {};
    i128     sum_disc_price[kNumSlots] {};
    i128     sum_charge[kNumSlots]     {};
    int64_t  sum_disc[kNumSlots]       {};
    uint64_t count[kNumSlots]          {};
};

// Format a non-negative __int128 / int128 value as base-10 digits into `buf`.
// Returns the number of chars written. Does NOT null-terminate.
int u128_to_chars(__uint128_t v, char* buf) {
    if (v == 0) { buf[0] = '0'; return 1; }
    char tmp[40];
    int n = 0;
    while (v != 0) {
        tmp[n++] = static_cast<char>('0' + static_cast<int>(v % 10));
        v /= 10;
    }
    for (int i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
    return n;
}

// Print a (possibly negative) scaled int128 as "<int>.<frac>" with exactly
// `scale` fractional digits.
void print_scaled_i128(i128 value, int scale, std::string& out) {
    bool neg = (value < 0);
    __uint128_t u = neg ? static_cast<__uint128_t>(-value) : static_cast<__uint128_t>(value);

    char digits[48];
    int ndig = u128_to_chars(u, digits);
    // Ensure at least scale+1 digits (pad with leading zeros for the integer side).
    if (ndig < scale + 1) {
        int pad = (scale + 1) - ndig;
        // Shift right.
        std::memmove(digits + pad, digits, ndig);
        std::memset(digits, '0', pad);
        ndig += pad;
    }
    int int_len = ndig - scale;
    if (neg) out.push_back('-');
    out.append(digits, int_len);
    if (scale > 0) {
        out.push_back('.');
        out.append(digits + int_len, scale);
    }
}

// Print a double using shortest round-trip (std::to_chars).
void print_double(double v, std::string& out) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), v);
    out.append(buf, r.ptr - buf);
}

} // namespace

void query(Database* db) {
    Accumulators acc{};
    const uint64_t n = db->n;
    const uint8_t* gk = db->group_key.data();
    const int64_t* qty = db->quantity.data();
    const int64_t* ep  = db->extendedprice.data();
    const int64_t* dc  = db->discount.data();
    const int64_t* tx  = db->tax.data();

    for (uint64_t i = 0; i < n; ++i) {
        const uint8_t k = gk[i];
        const int64_t qi = qty[i];
        const int64_t ei = ep[i];
        const int64_t di = dc[i];
        const int64_t ti = tx[i];

        acc.sum_qty[k]        += qi;
        acc.sum_base_price[k] += ei;
        acc.sum_disc[k]       += di;
        acc.count[k]          += 1;

        const int64_t one_minus_disc = 100 - di;            // scale 2
        const int64_t one_plus_tax   = 100 + ti;            // scale 2
        const i128 disc_price_term   = static_cast<i128>(ei) * one_minus_disc; // scale 4
        acc.sum_disc_price[k] += disc_price_term;

        // disc_price_term fits in int64 (~5e8 * 100 = 5e10 << 9.2e18).
        // Multiplying by one_plus_tax (<=108) still fits in int64 per row
        // (5.4e12), but accumulating over millions overflows int64, so
        // use int128 for the sum.
        const i128 charge_term = disc_price_term * one_plus_tax; // scale 6
        acc.sum_charge[k] += charge_term;
    }

    // Emit rows in (l_returnflag, l_linestatus) order. Slots are
    // already in that order by construction (rf_idx*2 + ls_idx).
    std::string out;
    out.reserve(1024);
    for (int slot = 0; slot < kNumSlots; ++slot) {
        if (acc.count[slot] == 0) continue;

        char rf, ls;
        unpack_group_key(static_cast<uint8_t>(slot), rf, ls);

        const uint64_t cnt = acc.count[slot];
        // DuckDB's avg returns sum_decimal / count. To match its bit-exact
        // double rounding we fold the decimal-scale denominator (100) into
        // the divisor: avg = sum_int / (100.0 * count). Computing
        // (sum_int / 100.0) / count first loses precision in the worst
        // case (verified vs. DuckDB for SF=1).
        const double denom_q = 100.0 * static_cast<double>(cnt);
        const double avg_qty   = static_cast<double>(acc.sum_qty[slot])        / denom_q;
        const double avg_price = static_cast<double>(acc.sum_base_price[slot]) / denom_q;
        const double avg_disc  = static_cast<double>(acc.sum_disc[slot])       / denom_q;

        out.clear();
        out.push_back(rf);
        out.push_back(',');
        out.push_back(ls);
        out.push_back(',');

        print_scaled_i128(static_cast<i128>(acc.sum_qty[slot]),        2, out); out.push_back(',');
        print_scaled_i128(static_cast<i128>(acc.sum_base_price[slot]), 2, out); out.push_back(',');
        print_scaled_i128(acc.sum_disc_price[slot],                    4, out); out.push_back(',');
        print_scaled_i128(acc.sum_charge[slot],                        6, out); out.push_back(',');

        print_double(avg_qty,   out); out.push_back(',');
        print_double(avg_price, out); out.push_back(',');
        print_double(avg_disc,  out); out.push_back(',');

        char cbuf[32];
        auto cr = std::to_chars(cbuf, cbuf + sizeof(cbuf), cnt);
        out.append(cbuf, cr.ptr - cbuf);

        out.push_back('\n');
        std::fwrite(out.data(), 1, out.size(), stdout);
    }
    std::fflush(stdout);
}
