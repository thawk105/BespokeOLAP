# Phase 1: Scaffold

## 現状
- `_legacy/` に OpenAI Agents SDK ベースの orchestrator を隔離
- `template/` に C++ engine のひな型を移動（旧 `misc/fasttest/`）
- `docs/expert_knowledge.md` に最適化知識ファイルを取り出し
- `.devcontainer/` で Ubuntu 24.04 + Arrow + DuckDB + uv + Claude Code を用意

## まだ無いもの
- TPC-H Parquet データ生成スクリプト
- `validate.py`（DuckDB との結果比較）
- `CLAUDE.md`（合成方針）
- slash command / subagent 定義
