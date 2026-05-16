# Overview

## Motivation

The paper *Bespoke OLAP: Synthesizing Workload-Specific One-size-fits-one Database Engines* (Wehrstein et al., 2026, TU Darmstadt) argues that general-purpose query engines pay a "generality tax": code paths that account for features the workload never uses still cost branches, indirection, and memory traffic. The paper shows that an LLM-driven synthesis loop can emit a custom C++ engine for a fixed workload and beat DuckDB on the target queries by a meaningful margin.

The upstream implementation is built around the OpenAI Agents SDK and the `gpt-5.2-codex` model. That stack bills per token through OpenAI, which puts the full synthesis loop out of reach for someone whose only LLM budget is an Anthropic Max subscription. The aim of this fork is narrow: reproduce the same synthesis loop inside Claude Code, where subagents, slash commands, and hooks already cover the orchestration the paper needs, and where the cost is the flat subscription rather than per-token API spend.

## Design decisions

### Frankenstein strategy

The upstream repository contains four parts: (1) a Python orchestrator driving the LLM, (2) a C++ engine skeleton with a hot-reload host, (3) a DuckDB-backed validator, and (4) a curated knowledge base of optimization advice. Of these, only the orchestrator is tied to OpenAI; the rest are LLM-agnostic and represent real engineering effort. This fork therefore keeps (2), (3), and (4) intact and quarantines (1) under `_legacy/` for reference. The early phases were essentially a port of the Python orchestrator into Claude Code's native primitives — replacing `main.py` with a slash command, replacing scripted conversations with subagents, and replacing the LLM cache with whatever Claude Code already provides.

### Subagent decomposition

The paper describes three agent roles: an *Implementer* that writes C++, an *Accuracy Judge* that checks results against DuckDB, and a *Performance Evaluator* that grades speed. This fork maps them onto three Claude Code subagents: `storage-planner` (designs the in-memory layout from the SQL and parquet samples), `base-implementer` (writes `loader_impl.cpp` / `builder_impl.cpp` / `query_impl.cpp` and iterates until the validator passes), and `validator` (runs `validate.py` and reports pass/fail with diff). The Performance Evaluator role is intentionally omitted in Phase 3a/3b — correctness comes first — and is planned for Phase 3c.

### Static linking first, hot-reload later

The upstream `template/db.cpp` is a long-running host process that `dlopen`s `libloader.so` / `libbuilder.so` / `libquery.so` and recompiles them in place between iterations. That gives the optimization loop sub-second turnaround, but the machinery (forked child, IPC over pipes, build-id-based change detection) is unnecessary while the goal is just to make one engine that runs correctly. Phase 3a therefore introduces `template/db_static.cpp` — a 60-line `main` that calls `load -> build -> query` directly and parses `--data-dir` / `--query` — and adds a separate `synth` Makefile target that links it with the synth output. `template/db.cpp` is left untouched so that Phase 3c can restore the hot-reload path without merge churn.

### Output tracking

The upstream repository keeps `output/` gitignored, on the assumption that synthesis runs are ephemeral and that snapshots are tracked separately through a git-cache mechanism backed by `GitSnapshotter`. This fork takes the opposite stance: every successful synthesis stage commits its artifacts into `output/q{n}/`. Regression rollback then becomes `git reset --hard`, and the synthesis history is reviewable through ordinary `git log`. The cost is one commit per stage and slightly larger repo size; the gain is that nothing depends on an external snapshot cache.

## Workflow

### Phase 1: Scaffold

Fork the upstream repository; move the Python orchestrator and its prompt/cache state into `_legacy/`; rename `misc/fasttest/` to `template/`; extract the curated optimization knowledge from `_legacy/conversations/prompts/expert_knowledge.txt` to `docs/expert_knowledge.md`; add a Dev Container with Ubuntu 24.04, the Arrow C++ development packages, DuckDB CLI, uv, and Claude Code preinstalled.

### Phase 2: Foundation

Add `scripts/gen_tpch.sh` (TPC-H Parquet generation via DuckDB's `tpch` extension); add `validate.py` (DuckDB comparison harness); author `CLAUDE.md` (project rules, constraints, synthesis workflow); write `template/Makefile` with `-std=c++20`, Arrow/Parquet linkage, and a `build/db` target; confirm the host process compiles and exits cleanly with no arguments.

### Phase 3a: Synthesis infrastructure

Define the three subagents in `.claude/agents/`; define `/synthesize` in `.claude/commands/`; extend the Makefile with a `synth` target driven by a `SYNTH_DIR` variable; introduce `template/db_static.cpp` so synth output can be linked statically without disturbing the dlopen host; track `output/` in git; formalize `requirements.txt` and wire it into the post-create hook.

### Phase 3b: First one-shot synthesis (next)

Run `/synthesize tpch q1`. Q1 is the natural starting point because it is a single-table scan + filter + aggregation with no joins, which keeps the storage plan small and the implementation a few hundred lines. Success is defined as `validate.py` reporting PASS — performance is not yet graded.

### Phase 3c: Optimization loop (future)

Add a `performance-evaluator` subagent. Inject the curated `docs/expert_knowledge.md` content into the base-implementer's context. Run a fixed number of optimization rounds with regression rollback via `git reset --hard`. Restore the dlopen / `template/compiler.py` hot-reload path so that each round avoids a full process restart.

## Comparison with the paper

See [COMPARISON.md](COMPARISON.md).
