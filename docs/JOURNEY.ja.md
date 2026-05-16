# JOURNEY (ja)

Phase 1 から Phase 3b までの作業ログ。
英語ドキュメント（`README.md` / `docs/OVERVIEW.md` / `docs/COMPARISON.md` / `docs/PHASE{1,2,3}.md`）が
設計・仕様を扱うのに対し、こちらは経緯・判断の理由・詰まった箇所を日本語で残すことを目的とする。

---

## 1. このドキュメントの位置づけ

- 第三者（あるいは将来の自分）が「なぜこういう構造になったのか」を追えるようにする。
- 英語ドキュメントが扱わないエピソード（互換性問題、context 落ち、自発的な設計判断、外した記述など）を補完する。
- 仕様の正本は英語ドキュメントと `CLAUDE.md`。本ドキュメントは作業日記であり、矛盾があれば英語ドキュメント側を信頼する。

## 2. 動機

本リポジトリは以下 2 本の論文がきっかけで始まった。

- *Bespoke OLAP: Synthesizing Workload-Specific One-size-fits-one Database Engines*
  Wehrstein et al., 2026, TU Darmstadt
- *VibeServe: Can AI Agents Build Bespoke LLM Serving Systems?*
  Kamahori et al., 2026, University of Washington

LLM が C++ で workload-specific なシステムを生成する、という方向性に関心を持ったのが出発点である。

ただし、上流の DataManagementLab/BespokeOLAP は OpenAI Agents SDK + GPT-5.2-codex 前提で実装されている。
オーナーが契約しているのは Anthropic Claude Max 20x プランであり、そのままでは動かない。
さらに 2026-04 以降、Anthropic は third-party harness が Max プランの subscription 枠を利用することを公式に遮断している。
このため `ANTHROPIC_API_KEY` を上流の harness に差し込んでも別途 API 課金が発生する。

「論文の synthesis loop を Claude Code 上に再構成し、Max plan の subscription 内で完結させる」
というのが本プロジェクトのスコープである。

## 3. アプローチ: Frankenstein 戦略

上流リポジトリは大きく 4 つの部分に分解できる:

1. Python の orchestrator（`main.py`, `run_gen_storage_plan.py`, `run_gen_base_impl.py`, `run_optim_loop.py`, `conversations/` ほか）
2. C++ engine の skeleton（hot-reload host 込み）
3. DuckDB ベースの validator
4. 最適化知識のテキスト（`conversations/prompts/expert_knowledge.txt`）

OpenAI に縛られているのは (1) だけで、(2)〜(4) は LLM 非依存かつ実装コストが高い。
したがって (1) を `_legacy/` に隔離し、(2)〜(4) はそのまま流用、
orchestration 層だけを Claude Code 上の primitive（slash command + subagent）で書き直す方針を採った。

上流の `template/compiler.py` による hot-reload も Phase 3c まで保留とし、
当面は静的リンクで合成バイナリを作る簡易構成を選んだ。

## 4. Phase 別の経緯

### 4.1 Phase 1: 足場作り（commit `1cf4f8c`）

- DataManagementLab/BespokeOLAP を thawk105/BespokeOLAP に fork。
- ホスト側で Claude Code が未インストールだったので、
  `curl -fsSL https://claude.ai/install.sh | sh` で native installer から入れて
  `~/.local/bin/claude` を確保。OAuth ブラウザ認証で Max plan session を確立した。
- 以後の作業は基本的に「Issue 形式の指示書を Claude Code セッションに投げて任せる」スタイルで進めた。

Phase 1 で行ったレイアウト変更:

- 上流の Python orchestrator 一式 (`main.py`, `run_*.py`, `conversations/`, `pyproject.toml`, `uv.lock`, `.env_example`) を `_legacy/` 配下に隔離。
- `misc/fasttest/` を `template/` にリネーム（C++ engine skeleton）。
- `conversations/prompts/expert_knowledge.txt` を `docs/expert_knowledge.md` にコピー（`_legacy/` 側にも原本を残置）。
- `.devcontainer/` を追加（Ubuntu 24.04 + Arrow + DuckDB + Claude Code feature）。
- `tools/validate_tool/` はそのまま残した（DuckDB との比較に再利用する）。

検証 prompt で acceptance criteria が全 PASS することを確認してコミット。

### 4.2 Phase 2: 土台（commit `112f1fc`）

VS Code の "Reopen in Container" で Dev Container をビルド。
Apple Silicon (arm64) 上で Ubuntu 24.04 + Arrow 24.0.0 + DuckDB v1.5.2 + Python 3.12 + uv + Claude Code が揃った。
コンテナ内 Claude Code は OAuth 認証空間がホストと別なので、ここで再ログインしている。

環境検証 prompt（15 項目）の結果、2 件が「要確認」として残った:

- `template/` には Makefile/CMakeLists が存在せず、代わりに `template/compiler.py` という Python ベースの動的コンパイラがあった。これは論文の hot-reload 設計（各 `.cpp` を独立に `.so` 化して dlopen）の直接的な痕跡である。
- Apache Arrow 24.x は C++20 必須（`std::span` / `std::bit_width` / `std::popcount` を使用）。
  g++ 13.3 のデフォルト gnu++17 ではコンパイルが通らず、`-std=c++20` の明示が必要。

Phase 2 Issue を投げた際の "Task 0" で重要な発見があった:
`loader_api.cpp` / `builder_api.cpp` / `query_api.cpp` が **同名の `plugin_query()` シンボル** を定義しており、
これらを 1 バイナリに静的リンクすると重複定義でビルドが落ちる。
論文の hot-reload 設計（各 api を独立 `.so` にする）の必然がここで顕在化したので、
Phase 2 の Makefile では `SRCS_MAIN := $(wildcard *.cpp)` のような素朴な実装はせず、
`DB_SRCS := db.cpp utils/build_id.cpp` と明示列挙する方針を採った。

その他、細かな調整:

- `make test` は build-only に変更。`test_runner` は `libmylib*.so` を dlopen する設計で、Phase 2 時点では対応 `.so` が無い。
- system Python に `pip install duckdb` を 1 回実行（`validate.py` が import する）。
- PASS/FAIL の絵文字を文字列に置換（プロジェクト規約「ファイルに絵文字を書かない」に従う）。

### 4.3 Phase 3a: 配管（commit `1f5c140`）

Phase 3a の Issue を投げて synthesis 用の配管を作成した。
Claude Code 側で自発的に下した重要な判断が一つある。

Issue では「静的リンクで動く `db_synth` ターゲットを Makefile に追加する」という素朴な案 (A) を提示していたが、
Claude Code は Task 0 の調査の中で、既存 `template/db.cpp` が dlopen 設計であり (A) と衝突することを発見した。
そのうえで以下の対応を取った:

- `template/db_static.cpp` を **新設**。dlopen / fork / IPC env var を使わない最小 main で、`load -> build -> query` を直接呼び、CLI は `--data-dir` / `--query` に揃えた（`validate.py` と整合）。
- `template/db.cpp` は **無改造で温存**。Phase 3c の hot-reload で復帰させる。
- Makefile に `synth` ターゲットを新規追加（`SYNTH_DIR` 変数で synth 出力 dir を指定）。
- 詳細な経緯は `docs/PHASE3.md` の Task 0 観察メモに残してある。

この判断は Issue に明記していなかったもので、Claude Code が自力で `template/` を読んで導いたものである。

Phase 3a で導入した orchestration 部品:

- subagent 3 種（`.claude/agents/`）
  - `storage-planner`: クエリと parquet サンプルから `storage_plan.md` を生成
  - `base-implementer`: loader / builder / query を `validate.py` PASS まで反復
  - `validator`: `validate.py` を走らせ PASS/FAIL と diff を報告
- slash command `/synthesize <benchmark> <query>`（`.claude/commands/synthesize.md`）
  3 ステージを順に dispatch し、ステージ間で `git commit` を打つ。
- `.claude/settings.local.json` は user-specific のため gitignore（subagent / command 定義は git 管理）。
- `.devcontainer/post-create.sh` に `pip install -r requirements.txt` を追加。

### 4.4 Documentation Sprint（commit `9ab81cf`）

Phase 3b 本番に入る前に、ここまでの設計を英語で文書化した:

- `README.md` を Quickstart + 設計概要中心に書き直し
- `docs/OVERVIEW.md`（動機・設計判断・workflow）を新設
- `docs/COMPARISON.md`（論文 / upstream / 本 fork のマッピング表）を新設
- 既存の `docs/PHASE{1,2,3}.md` および `CLAUDE.md` は無改変

このスプリントに関連して 1 件、削除したエピソードがある。
初稿の Issue に「Qiita 記事」関連の文言が含まれていたが、
オーナーから「キモいから外して」との指摘があり、Qiita 関連の文字列をすべて削除して再投入した。
最終的な英語ドキュメントには Qiita への言及は残っていない。

このタイミングで context compaction（"Crunched for 1m 40s"）が走っている。

### 4.5 Phase 3a 仕様 fix（commit `126dd29`、v2.1.101 互換問題）

Phase 3b 本番として `/synthesize tpch q1` を投入したところ、以下のエラーが返った:

```
Unknown command: /synthesize
Args from unknown skill: tpch q1
```

調査の結果、Claude Code v2.1.101（2026-04-11）で custom slash command が skill system に統合され、
frontmatter に `name:` フィールドが必須化されていることが判明した。
Phase 3a で書いた `.claude/commands/synthesize.md` には `name:` が無く、これに該当した。
一方、`.claude/agents/*.md` 3 ファイルは Phase 3a の Issue で `name:` を明示指定済みだったため、
agents 側は無修正でよい。

修正 Issue の投入直前、Claude Code がフィードバックダイアログ "How is Claude doing this session?" を表示した。
オーナーが Dismiss を押した直後に context compaction で recap が走り、
修正 Issue の中身が context から落ちた。
このため再投入時には、短縮版の修正 Issue を作り、冒頭に
「直前の recap は無視してください。Phase 3b には進まないでください」と明記して上書きした。

最終的な修正内容は `.claude/commands/synthesize.md` の frontmatter に `name: synthesize` を 1 行追加するだけだった。

### 4.6 Phase 3b: 初ワンポチ（commits `a30a308`, `e30c3b5`）

修正後、Claude Code を `/exit` してから再起動し、改めて `/synthesize tpch q1` を投入した。

- **Stage 1: Storage plan** — `storage-planner` subagent を dispatch、`output/tpch/q1/storage_plan.md` を生成。
  コミット `a30a308`。
- **Stage 2: Base implementation** — `base-implementer` subagent を dispatch、`output/tpch/q1/{loader_impl,builder_impl,query_impl}.cpp` と `q1_types.hpp` を生成。`validate.py` で DuckDB と比較し PASS を確認。
  コミット `e30c3b5`。
- **Stage 3: Optimization** — Phase 3c でやるためスキップ。

## 5. 合成された engine の design choices

Phase 3b の `base-implementer` が選択した戦略は、論文 Table 2 の最適化カタログにきれいに対応している。
重要なのは、これらが `docs/expert_knowledge.md` の明示注入なしに Claude (Opus 4.7) によって自発的に選ばれた点である。

| 合成された戦略 | 内容 | 論文 Table 2 の対応 |
|---|---|---|
| 必要列のみマテリアライズ | `lineitem` の 16 列中、Q1 が触る 7 列のみを in-memory 化。残りは loader 段階で捨てる | Projection Pushdown 相当 |
| Predicate push-down at load | `l_shipdate <= '1998-09-02'` を loader 内で評価し、フィルタ通過行のみを保持。runtime のフィルタ評価が消える | Sorted Range Scan / Predicate Pushdown 相当 |
| Scaled integer decimal | TPC-H DECIMAL(15,2) を scale 100 の int64 で保持。乗算の合計は int128 に拡張して scale 4 / 6 を lossless に維持 | Column Encoding: Scaled Integer / Fixed-Point (exotic) |
| Dense-key + direct-array aggregation | `(l_returnflag, l_linestatus)` を 2bit + 2bit で 6 スロットの key にパックし、`std::array` への直接 index で集約。hash table を使わない | Dense-Key Aggregation (exotic) + Direct Array Aggregation (exotic) + Inline Fused Aggregation |

`output/tpch/q1/storage_plan.md` 側で「Q1 のグループは高々 `A/F`, `N/F`, `N/O`, `R/F` の 4 種類」を
実際の parquet を SUMMARIZE して確認したうえで、direct-array aggregation を採用している。

## 6. できるようになったこと

- Anthropic Max plan の subscription 内で、OpenAI 課金なしに論文の synthesis loop を 1 サイクル動かせる。
- `/synthesize tpch q1` のワンコマンドで、storage plan → base impl → validate までを git commit 込みで自動実行できる。
- 出力 (`output/tpch/q1/`) は git 管理下にあり、`git log` で合成履歴を辿れる。
- DuckDB との結果一致を `validate.py` で機械的に検証できる。

## 7. 改善の余地

### 7.1 本リポジトリ内（Phase 3c）

- **Optimization loop の実装**。`performance-evaluator` subagent を追加し、`docs/expert_knowledge.md` を base-implementer の context に注入。固定回数の最適化ラウンドを `git reset --hard` ベースの regression rollback と組み合わせる。
- **Hot-reload の復帰**。`template/compiler.py` と `template/db.cpp` のペアを再活用し、各最適化ラウンドのフルプロセス再起動を避ける。`template/db_static.cpp` は Phase 3a/3b 用に残置。
- **対応クエリ拡張**。現状 `reference/q1.sql` のみ。Q3 / Q6 / Q14 など join を含まない近い構造のクエリから順に試す。

### 7.2 別リポジトリで検討すべき高速化（並列化）

オーナーから「自動合成は subagent をいい感じに生やしてローカルマシンを使い倒すか？」という問いがあり、
以下のように整理した:

- 現状の `/synthesize tpch q1` は完全逐次。Stage 1 → 2 → 3 は依存関係上、並列化不可。
- 論文も並列化していない。論文 §4 では conversation branching の commit 順序について
  *"the first per-query Bespoke Agent commits its changes before the second begins"* と明記している。
  これは shared storage の整合性を維持するための逐次実行制約である。
- 1 query 内で本格的に並列化したい場合、shared storage を諦める必要があり、構造的な変更を伴う。
- Claude Code の subagent は同時に最大 10 個まで dispatch できるが、上記の制約下では恩恵が小さい。

このため、論文準拠の構成は本リポジトリで維持し、
並列化や shared storage トレードオフの実験は別リポジトリでやる、という方針にしてある。

## 8. 副次的な発見・学び

- Claude (Opus 4.7) は DB 最適化の語彙（dictionary encoding / scaled int / dense-key aggregation など）を持っており、`expert_knowledge.md` を明示注入せずとも適切な戦略を選択できた。Phase 3c で expert knowledge を注入したときに何が積み上がるかは別途観察する価値がある。
- Frankenstein 戦略のおかげで、ゼロから書いたコードは Markdown 定義ファイル数枚、Makefile、`validate.py`、`gen_tpch.sh`、`db_static.cpp` 程度に収まった。C++ 資産（loader/builder/query API, Arrow/Parquet 周り）と最適化知識は上流から無改変で引き継げている。
- Claude Code の slash command + subagent は、論文の per-query agent thread 構造を素直に表現できる。orchestration 層の置き換え先として相性がよい。
- v2.1.101 の `name:` 必須化のような互換性問題は今後も起こりうる。slash command / subagent の frontmatter は常に `name:` 込みで書いておくほうが安全。
- Dev Container 化により、Apple Silicon Mac からでも論文と同じ環境（Arrow / Parquet / C++20 一式）が再現できた。`-std=c++20` 明示は忘れがちなので Makefile 側に固定しておくのが楽。
- context compaction で直前の Issue 内容が落ちることがある。重要な修正 Issue の冒頭で
  「直前の recap は無視」と明示しておくと事故を防げる。

## 9. 今後の進め方

- 直近の次マイルストーンは Phase 3c。`performance-evaluator` subagent と最適化ループの実装に着手する。
- Q1 以外への展開は Phase 3c の後。`reference/q*.sql` を追加し、`/synthesize tpch q3` などを順次回す。
- 並列化や shared storage の見直しは別リポジトリで扱い、本リポジトリは論文準拠の実装として整える。
- 英語ドキュメント（`README.md` / `docs/OVERVIEW.md` / `docs/COMPARISON.md` / `docs/PHASE*.md`）は phase 進行に合わせて随時更新する。日本語の経緯ログは本ファイルに追記する。
