// Bespoke TPC-H Q1 in-memory types.
//
// Storage plan: only `lineitem` is needed; we load 7 source columns, apply
// the `l_shipdate <= 1998-09-02` predicate at load time, drop shipdate, and
// keep an SoA columnar layout of the 6 surviving columns.

#pragma once

#include <cstdint>
#include <vector>

// Forward decls only; structs defined here.

// ParquetTables holds the raw parquet read result for lineitem.
// We only need the 7 Q1 columns. We materialize them as primitive C++
// vectors at load time (rather than holding shared_ptr<arrow::Table>),
// because the storage plan calls for a hand-built columnar SoA layout.
struct ParquetTables {
    uint64_t n_raw = 0;                     // raw row count read from parquet
    uint64_t n_kept = 0;                    // rows surviving the load-time filter
    std::vector<int64_t>  quantity;         // DECIMAL(15,2) scaled x100
    std::vector<int64_t>  extendedprice;    // DECIMAL(15,2) scaled x100
    std::vector<int64_t>  discount;         // DECIMAL(15,2) scaled x100
    std::vector<int64_t>  tax;              // DECIMAL(15,2) scaled x100
    std::vector<char>     returnflag;       // single-char per row
    std::vector<char>     linestatus;       // single-char per row
};

// Database: the post-build columnar layout described by the storage plan.
// Predicate applied, shipdate dropped, group_key packed.
struct Database {
    uint64_t n = 0;                         // post-filter row count
    std::vector<uint8_t>  group_key;        // 0..5
    std::vector<int64_t>  quantity;         // scaled x100
    std::vector<int64_t>  extendedprice;    // scaled x100
    std::vector<int64_t>  discount;         // scaled x100
    std::vector<int64_t>  tax;              // scaled x100
};

// Group key packing: returnflag in {'A','N','R'} -> {0,1,2};
// linestatus in {'F','O'} -> {0,1}; key = rf_idx * 2 + ls_idx (range 0..5).
inline uint8_t pack_group_key(char rf, char ls) {
    uint8_t rf_idx = (rf == 'A') ? 0u : (rf == 'N') ? 1u : 2u; // 'R'
    uint8_t ls_idx = (ls == 'F') ? 0u : 1u;                    // 'O'
    return static_cast<uint8_t>(rf_idx * 2u + ls_idx);
}

inline void unpack_group_key(uint8_t k, char& rf, char& ls) {
    static const char rf_chars[3] = {'A', 'N', 'R'};
    static const char ls_chars[2] = {'F', 'O'};
    rf = rf_chars[k / 2];
    ls = ls_chars[k % 2];
}
