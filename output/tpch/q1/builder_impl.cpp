// Builder for TPC-H Q1.
//
// Input: filtered SoA arrays from the loader (returnflag/linestatus as char,
// decimal columns as int64 scaled x100).
// Output: same arrays, plus a packed uint8 group_key per row, owned by
// Database. The raw returnflag/linestatus arrays are discarded after packing.

#include "q1_types.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <utility>

Database* build(ParquetTables* tables) {
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();

    auto db = new Database{};
    const uint64_t n = tables->n_kept;
    db->n = n;

    // Move the decimal columns out of the loader-owned ParquetTables; we
    // own them in the Database now.
    db->quantity      = std::move(tables->quantity);
    db->extendedprice = std::move(tables->extendedprice);
    db->discount      = std::move(tables->discount);
    db->tax           = std::move(tables->tax);

    // Pack the (returnflag, linestatus) pair into a uint8 group_key.
    db->group_key.resize(n);
    const char* rf = tables->returnflag.data();
    const char* ls = tables->linestatus.data();
    uint8_t* gk = db->group_key.data();
    for (uint64_t i = 0; i < n; ++i) {
        gk[i] = pack_group_key(rf[i], ls[i]);
    }

    // Drop the now-redundant char arrays from the loader.
    std::vector<char>().swap(tables->returnflag);
    std::vector<char>().swap(tables->linestatus);

    const auto t1 = Clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cerr << "builder: packed " << n << " rows in " << ms << " ms\n";
    return db;
}
