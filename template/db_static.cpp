// Minimal static-link host for synthesized engines (Phase 3a+).
//
// Bypasses the dlopen/plugin/fork machinery used by db.cpp; calls
// load -> build -> query directly. The synth output dir must define
// these free functions (declarations in {loader,builder,query}_api.hpp):
//
//     ParquetTables* load(std::string);
//     Database*      build(ParquetTables*);
//     void           query(Database*);
//
// Invocation:
//     db_static --data-dir <dir> [--query <id>]
//
// Output: CSV result rows on stdout, no header. The query implementation
// decides what to do with --query (may ignore it for single-query engines).
// The query id is also exposed to the engine via the QUERY_ID env var so
// query_impl.cpp can branch without changing main's argv parsing.

#include "builder_api.hpp"
#include "loader_api.hpp"
#include "query_api.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string data_dir;
    std::string query_id;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--data-dir" || a == "-d") && i + 1 < argc) {
            data_dir = argv[++i];
        } else if ((a == "--query" || a == "-q") && i + 1 < argc) {
            query_id = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::cerr << "Usage: " << argv[0]
                      << " --data-dir <dir> [--query <id>]\n";
            return 0;
        } else {
            std::cerr << "Unknown arg: " << a << "\n";
            return 1;
        }
    }

    if (data_dir.empty()) {
        std::cerr << "Missing --data-dir\n";
        return 1;
    }
    if (data_dir.back() != '/') data_dir.push_back('/');

    if (!query_id.empty()) {
        setenv("QUERY_ID", query_id.c_str(), 1);
    }

    auto* tables = load(data_dir);
    auto* db = build(tables);
    query(db);
    return 0;
}
