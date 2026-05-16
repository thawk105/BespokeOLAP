# Phase 3: Synthesis

## 3a (本 phase): インフラ
- subagent: storage-planner / base-implementer / validator (`.claude/agents/`)
- slash command: `/synthesize <benchmark> <query>` (`.claude/commands/synthesize.md`)
- Makefile に合成 build target (`synth`)
- `output/` を git 管理 (`.gitignore` から除外、`output/README.md` + `.gitkeep`)
- `requirements.txt` 整備、`.devcontainer/post-create.sh` で自動 install
- 新規ファイル `template/db_static.cpp` (静的リンク用の最小 host、db.cpp は不変)

## 3b (次): 初ワンポチ
- `/synthesize tpch q1` を実行
- `output/q1/` に `storage_plan.md` + 3 つの `.cpp`（loader/builder/query impl）が生成され、validate PASS

## 3c (本番): Optimization loop
- performance-evaluator subagent 追加
- `docs/expert_knowledge.md` の注入
- regression rollback (`git reset --hard`)
- hot-reload 復活 (`template/compiler.py` の再活用、`template/db.cpp` 経由)

---

## Task 0 観察メモ（合成 build の設計）

### 現状の `template/db.cpp` 制約
- dlopen 設計: `./build/lib{loader,builder,query}.so` をランタイムで動的ロード
- 親プロセスは env var `P2C_FD` / `C2P_FD` を要求（無いと `std::runtime_error`）
- CLI は `<PARQUET_DIR>` の positional 1 個のみ

### 静的リンク方式 (A) の素朴な実装が成立しない理由
ユーザ提示テンプレ `$(BUILD)/db_synth: $(OBJS_MAIN) $(SYNTH_OBJS)` をそのまま採ると:
1. `db.cpp` は `load/build/query` を直接参照しないので、synth 側の対応シンボルがあろうがなかろうが link は成功してしまう（空 dir でも link が通り、smoke test の期待「link error」が発生しない）
2. ランタイムで dlopen 失敗 + env var 不在で abort、validate.py から呼べない
3. `validate.py` は `engine --query <id> --data-dir <dir>` で呼ぶが、`db.cpp` は positional 1 個のみ受け取るので CLI 不整合

### 採用した実装: `template/db_static.cpp` 新設
- dlopen/fork/IPC を一切使わない最小 main
- CLI: `--data-dir <dir> [--query <id>]`（validate.py と整合）
- `load(dir) → build(tables) → query(db)` を直接呼ぶ
- `--query` 値は `setenv("QUERY_ID", ...)` で query_impl 側に橋渡し
- Makefile の `synth` target は `db_static.o + utils/build_id.o + $(SYNTH_OBJS)` をリンク
  - 空 `SYNTH_DIR` だと `load/build/query` 未定義で link 失敗 → smoke test 期待値と一致
- `template/db.cpp` は無改造（Phase 3c の hot-reload で使う）

### Makefile への影響
- `all`（既存 db ターゲット）は不変、`make` で `build/db` が変わらずビルドされる
- `synth` は新規 phony target、`SYNTH_DIR` 変数で synth 側 dir を指定
- ヘッダ参照のため `-I$(SYNTH_DIR)` を synth/*.o コンパイル時に追加（output 配下の `*.hpp` を `#include "database.hpp"` 等で参照できるよう）

### Agent prompts への反映
- `base-implementer.md` の forbidden list に `template/db_static.cpp` を追加
- 同 prompt で「`--query` 値は env var `QUERY_ID` 経由」を明記
- ビルド/検証コマンド例も `db_synth` + `--data-dir` 形式に統一
