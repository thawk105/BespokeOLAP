# output/

Synthesized engines live here, one directory per query.

## Layout

```
output/
  q1/
    storage_plan.md     # produced by storage-planner subagent
    loader_impl.cpp     # ParquetTables* load(std::string)
    builder_impl.cpp    # Database*      build(ParquetTables*)
    query_impl.cpp      # void           query(Database*)
    ...                 # any additional headers the implementer needs
  q2/
    ...
```

## Building

```bash
make -C template synth SYNTH_DIR=../output/q1
./template/build/db_synth --data-dir data --query q1
```

The `synth` target links the synth `.cpp` files together with `template/db_static.cpp`
(a thin static-link host that calls `load -> build -> query` directly, bypassing the
dlopen/plugin machinery used by `template/db.cpp` in Phase 3c).

## Validating

```bash
python3 validate.py --engine template/build/db_synth --query q1 --data-dir data
```

Matches the engine's CSV output against DuckDB's result for `reference/q1.sql`.

## Conventions

- One directory per query id (`q1`, `q2`, ..., or arbitrary identifiers from `reference/*.sql`).
- Free functions `load`, `build`, `query` must use the signatures declared in
  `template/loader_api.hpp`, `template/builder_api.hpp`, `template/query_api.hpp`.
- The `Database` and `ParquetTables` structs may be defined wherever the implementer prefers
  (typically `output/q{n}/loader_impl.hpp` and `output/q{n}/builder_impl.hpp`) as long as
  `template/db_static.cpp` only ever sees them through forward declarations.
- Output one CSV row per result row to stdout, no header. Match DuckDB precision exactly
  (use `%.4f` or `%.2f` as the SQL dictates).
