# Phase 2: Foundation

## できたこと
- TPC-H SF=1 データ生成（`scripts/gen_tpch.sh`） → `data/` に 8 表 + `schema.sql` + `load.sql`
- `validate.py` (Q1 対応、DuckDB と engine の stdout CSV を比較)
- `template/Makefile` (静的ビルド、C++20、Arrow/Parquet リンク、`make` / `make clean` / `make test`)
- `CLAUDE.md`（合成方針、Phase 3 ワークフロー、制約一覧）
- `template/build/db` がビルドでき、引数なしで usage 表示 + exit 1（segfault なし）

## まだ無いもの
- `output/` での実際の合成（Phase 3）
- hot-reload 機構（Phase 3、`template/compiler.py` を復活）
- slash command / subagent 定義（Phase 3）
- 複数 query 対応（Phase 3 以降。現状 `reference/q1.sql` のみ）
- 環境変数 `P2C_FD` / `C2P_FD` を介した親子 IPC の実動作確認（Phase 3 で `compiler.py` が制御）

## Task 0 仕様調査メモ（要点のみ）

### `template/db.cpp`
- 1 個の positional arg `<PARQUET_DIR>` を取る（Usage 文字列に `>` 欠落あり、軽微なので未修正）
- `fork()` し、子は `./build/lib{loader,builder,query}.so` を dlopen
- 親は env var `P2C_FD` / `C2P_FD` を読む。未設定だと `std::runtime_error` で abort
- Phase 2 の smoke test は **引数なし起動** で usage 表示 + exit 1（dlopen も env var も触らない経路）

### Plugin 設計とリンクの落とし穴
- `loader_api.cpp` / `builder_api.cpp` / `query_api.cpp` は全て **同じ extern "C" シンボル `plugin_query()` を定義**
  → 1 つのバイナリに全部 link するとシンボル重複でビルド失敗
  → 各 api .cpp は別 .so にコンパイルする設計
- `db.cpp` は impl .cpp を link 時に必要としない（forward decl + dlopen）
- → Phase 2 Makefile は `DB_SRCS := db.cpp utils/build_id.cpp` のみに固定

### 必要シンボル一覧（Phase 3 で埋める）
- `ParquetTables* load(std::string)` — impl 済（TPC-H 8 表を `ReadParquetTable` で読む）
- `Database* build(ParquetTables*)` — **STUB**（空 Database を new。`struct Database` も TODO）
- `void query(Database*)` — **STUB**（stdin 行を QueryRequest として echo するだけ）

### Arrow / C++20 注意
- Arrow 24.x ヘッダは `std::span` / `std::bit_width` / `std::popcount` を要求 → **`-std=c++20` 必須**
- `pkg-config --cflags arrow parquet` は `-std=` を含まないので、自分で付ける

### aarch64 (ARM) チェック
- x86 専用 intrinsic は未発見
- `<elf.h>` + sched_setaffinity は ARM Linux で問題なし

## 検証結果

| 項目 | 結果 |
|------|------|
| `bash scripts/gen_tpch.sh 1` で data/*.parquet 生成 | OK（lineitem 6,001,215 行） |
| `make -C template` でビルド成功 | OK（警告なし） |
| `./template/build/db` が起動・graceful exit | OK（exit 1、usage 表示） |
| `make -C template test` で test_runner ビルド | OK（実行は Phase 3 で `.so` 群が揃ってから） |
| `python3 validate.py --help` | OK |
