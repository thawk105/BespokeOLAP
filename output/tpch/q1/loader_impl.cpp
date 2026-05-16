// Loader for TPC-H Q1.
//
// Reads only the 7 columns of lineitem that Q1 needs, applies the WHERE
// predicate `l_shipdate <= 1998-09-02` at load time, drops l_shipdate from
// the materialized output, and stages the surviving rows in plain SoA C++
// vectors of int64 (for decimals) and char (for the two single-char group
// columns) inside ParquetTables. The builder stage will pack them further.
//
// We deliberately bypass loader_utils.cpp from template/ (which reads
// all columns of a table). For Q1 the column-pruning win matters.

#include "q1_types.hpp"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/schema.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int32_t kCutoffDays = 10471; // DATE '1998-09-02' as days since 1970-01-01.

[[noreturn]] void die(const std::string& msg) {
    std::cerr << "loader error: " << msg << "\n";
    std::exit(1);
}

std::shared_ptr<arrow::Table> read_lineitem_columns(const std::string& path) {
    auto file_res = arrow::io::MemoryMappedFile::Open(path, arrow::io::FileMode::READ);
    if (!file_res.ok()) die("open " + path + ": " + file_res.status().ToString());
    auto file = std::move(file_res).ValueOrDie();

    parquet::ReaderProperties reader_props;
    reader_props.set_footer_read_size(8 * 1024 * 1024);

    parquet::ArrowReaderProperties arrow_props(/*use_threads=*/false);
    arrow_props.set_pre_buffer(true);

    parquet::arrow::FileReaderBuilder builder;
    auto open_st = builder.Open(file, reader_props);
    if (!open_st.ok()) die("FileReaderBuilder::Open: " + open_st.ToString());
    builder.properties(arrow_props);
    auto built = builder.Build();
    if (!built.ok()) die("FileReaderBuilder::Build: " + built.status().ToString());
    auto reader = std::move(built).ValueOrDie();

    auto schema_res = reader->parquet_reader()->metadata()->schema();
    // Resolve column indices by name.
    auto names = std::vector<std::string>{
        "l_shipdate", "l_quantity", "l_extendedprice",
        "l_discount", "l_tax", "l_returnflag", "l_linestatus",
    };

    std::vector<int> indices;
    indices.reserve(names.size());
    for (const auto& n : names) {
        int found = -1;
        for (int i = 0; i < schema_res->num_columns(); ++i) {
            if (schema_res->Column(i)->name() == n) {
                found = i;
                break;
            }
        }
        if (found < 0) die("column not found: " + n);
        indices.push_back(found);
    }

    std::shared_ptr<arrow::Table> table;
    auto st = reader->ReadTable(indices, &table);
    if (!st.ok()) die("ReadTable: " + st.ToString());
    return table;
}

// Pull a column by name from a Table.
std::shared_ptr<arrow::ChunkedArray> column(
    const std::shared_ptr<arrow::Table>& tbl, const std::string& name) {
    auto col = tbl->GetColumnByName(name);
    if (!col) die("missing column: " + name);
    return col;
}

} // namespace

ParquetTables* load(std::string path) {
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();

    auto tbl = read_lineitem_columns(path + "lineitem.parquet");
    const int64_t n = tbl->num_rows();

    auto out = new ParquetTables{};
    out->n_raw = static_cast<uint64_t>(n);

    auto shipdate_col      = column(tbl, "l_shipdate");
    auto quantity_col      = column(tbl, "l_quantity");
    auto extendedprice_col = column(tbl, "l_extendedprice");
    auto discount_col      = column(tbl, "l_discount");
    auto tax_col           = column(tbl, "l_tax");
    auto returnflag_col    = column(tbl, "l_returnflag");
    auto linestatus_col    = column(tbl, "l_linestatus");

    // Type sanity checks.
    if (shipdate_col->type()->id() != arrow::Type::DATE32)
        die("l_shipdate type unexpected: " + shipdate_col->type()->ToString());
    if (quantity_col->type()->id() != arrow::Type::DECIMAL128)
        die("l_quantity type unexpected: " + quantity_col->type()->ToString());

    // Pre-size output vectors. We over-allocate to the raw size for simplicity
    // and shrink at the end (the filter passes ~98.6% of rows).
    out->quantity.reserve(n);
    out->extendedprice.reserve(n);
    out->discount.reserve(n);
    out->tax.reserve(n);
    out->returnflag.reserve(n);
    out->linestatus.reserve(n);

    // Iterate chunks in lockstep across all 7 columns.
    const int nchunks = shipdate_col->num_chunks();
    if (quantity_col->num_chunks() != nchunks ||
        extendedprice_col->num_chunks() != nchunks ||
        discount_col->num_chunks() != nchunks ||
        tax_col->num_chunks() != nchunks ||
        returnflag_col->num_chunks() != nchunks ||
        linestatus_col->num_chunks() != nchunks) {
        die("chunk count mismatch across columns");
    }

    int64_t kept = 0;
    for (int c = 0; c < nchunks; ++c) {
        auto sd_arr = std::static_pointer_cast<arrow::Date32Array>(shipdate_col->chunk(c));
        auto qty_arr = std::static_pointer_cast<arrow::Decimal128Array>(quantity_col->chunk(c));
        auto ep_arr  = std::static_pointer_cast<arrow::Decimal128Array>(extendedprice_col->chunk(c));
        auto dc_arr  = std::static_pointer_cast<arrow::Decimal128Array>(discount_col->chunk(c));
        auto tx_arr  = std::static_pointer_cast<arrow::Decimal128Array>(tax_col->chunk(c));
        auto rf_arr  = std::static_pointer_cast<arrow::StringArray>(returnflag_col->chunk(c));
        auto ls_arr  = std::static_pointer_cast<arrow::StringArray>(linestatus_col->chunk(c));

        const int64_t len = sd_arr->length();

        // Raw value pointers for the fixed-width arrays (decimals are 16-byte
        // little-endian; only the low 8 bytes are non-zero for DECIMAL(15,2)
        // values within int64 range -- which is true here).
        const int32_t* sd_raw = sd_arr->raw_values();
        const uint8_t* qty_raw = qty_arr->raw_values();
        const uint8_t* ep_raw  = ep_arr->raw_values();
        const uint8_t* dc_raw  = dc_arr->raw_values();
        const uint8_t* tx_raw  = tx_arr->raw_values();

        for (int64_t i = 0; i < len; ++i) {
            if (sd_raw[i] > kCutoffDays) continue;

            // Read the low 8 bytes of each 16-byte decimal as int64.
            int64_t qty, ep, dc, tx;
            std::memcpy(&qty, qty_raw + i * 16, 8);
            std::memcpy(&ep,  ep_raw  + i * 16, 8);
            std::memcpy(&dc,  dc_raw  + i * 16, 8);
            std::memcpy(&tx,  tx_raw  + i * 16, 8);

            auto rf_view = rf_arr->GetView(i);
            auto ls_view = ls_arr->GetView(i);

            out->quantity.push_back(qty);
            out->extendedprice.push_back(ep);
            out->discount.push_back(dc);
            out->tax.push_back(tx);
            out->returnflag.push_back(rf_view.empty() ? '\0' : rf_view[0]);
            out->linestatus.push_back(ls_view.empty() ? '\0' : ls_view[0]);
            ++kept;
        }
    }

    out->n_kept = static_cast<uint64_t>(kept);

    const auto t1 = Clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cerr << "loader: read " << n << " rows, kept " << kept
              << " after predicate, in " << ms << " ms\n";

    return out;
}
