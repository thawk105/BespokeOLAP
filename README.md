> **Fork notice**: This is a fork of [DataManagementLab/BespokeOLAP](https://github.com/DataManagementLab/BespokeOLAP) (Apache-2.0).
> The OpenAI-Agents-SDK-based orchestrator has been moved to `_legacy/`,
> and a Claude Code-based re-implementation is under development.
>
> **Current Phase: 3a (synthesis infra)** — subagents + `/synthesize` slash command + Makefile `synth` target. The actual synthesis run happens in Phase 3b. See `docs/PHASE3.md` for the roadmap, `CLAUDE.md` for the synthesis workflow.

# BespokeOLAP (Claude Code edition)

A fork of [DataManagementLab/BespokeOLAP](https://github.com/DataManagementLab/BespokeOLAP) (Apache-2.0)
that replaces the OpenAI-Agents-SDK orchestrator with a Claude Code based one,
runnable entirely within an Anthropic Max plan (no per-token OpenAI billing).

## What this does
Given a SQL workload, an LLM agent generates a workload-specific C++ OLAP engine from scratch,
validates it against DuckDB, and (eventually) optimizes it against a target benchmark.
This fork demonstrates the same synthesis loop using Claude Code subagents
instead of GPT-5.2-codex via OpenAI Agents SDK.

## Quickstart

### Prerequisites
- Docker + VS Code with Dev Containers extension
- Anthropic Claude Pro / Max / Team account (for Claude Code in the container)
- ~ 10 GB free disk (Arrow, DuckDB, TPC-H data at SF=1)

### Steps
1. Clone & open
   ```bash
   git clone https://github.com/<your-fork>/BespokeOLAP
   cd BespokeOLAP
   code .
   ```
2. Reopen in Container (`Cmd+Shift+P` -> "Dev Containers: Reopen in Container")
3. Generate data
   ```bash
   bash scripts/gen_tpch.sh 1
   ```
4. Start Claude Code in the container (re-auth required on first run)
   ```bash
   claude
   ```
5. Synthesize
   ```
   /synthesize tpch q1
   ```
6. Inspect
   ```bash
   ls output/q1/
   template/build/db_synth --data-dir data --query q1
   ```

## What changed from upstream

| Area | Upstream | This fork |
|---|---|---|
| Orchestrator LLM | GPT-5.2-codex via OpenAI Agents SDK | Claude (Opus 4.7) via Claude Code |
| Agent loop | `main.py` + ScriptedConversation / OptimizationConversation | `.claude/commands/synthesize.md` + 3 subagents |
| Build model | dlopen + hot-reload via `template/compiler.py` | Static linking via `template/db_static.cpp` (Phase 3a/3b); hot-reload preserved in `template/db.cpp` for Phase 3c |
| Cost model | per-token API | Subscription flat fee |
| Legacy code | Active | Moved to `_legacy/` for reference |

## Architecture
See [docs/OVERVIEW.md](docs/OVERVIEW.md).

## Directory layout
- `.claude/` — subagents, slash commands, settings
- `template/` — C++ engine skeleton (host process + helpers)
- `output/` — synthesized engines, one directory per query (git-tracked)
- `data/` — TPC-H Parquet files (gitignored)
- `reference/` — ground-truth SQL queries
- `scripts/` — data generation, helpers
- `docs/` — design docs, phase logs, expert knowledge
- `_legacy/` — original OpenAI Agents SDK orchestrator (reference only)

## Phase history
- [Phase 1](docs/PHASE1.md) — scaffold (Frankenstein layout)
- [Phase 2](docs/PHASE2.md) — foundation (data, validate, CLAUDE.md, static build)
- [Phase 3a](docs/PHASE3.md) — synthesis infrastructure (subagents + /synthesize)
- Phase 3b (next) — first one-shot synthesis run
- Phase 3c (future) — optimization loop with `expert_knowledge.md`

## License
Apache-2.0 (inherited from upstream)
