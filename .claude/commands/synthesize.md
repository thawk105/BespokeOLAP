---
name: synthesize
description: Synthesize a bespoke C++ OLAP engine for one query (TPC-H only in Phase 3).
argument-hint: <benchmark> <query_id>
---

# /synthesize $1 $2

Synthesize a workload-specific C++ engine for **$1 / $2**.

## Pre-flight
- Validate args: $1 must be `tpch`; $2 must match `q[0-9]+`
- Confirm `data/lineitem.parquet` exists (or run `bash scripts/gen_tpch.sh 1` if missing)
- Confirm reference SQL exists: `reference/$2.sql`
- Set `OUT_DIR=output/$2`, create directory, ensure clean (no stale `*.cpp` / `*.hpp` from prior runs; keep `storage_plan.md` if re-running stage 2 only)

## Stage 1: Storage planning
- Dispatch `storage-planner` subagent with:
  - reference SQL: `reference/$2.sql`
  - data dir: `data/`
  - output dir: `output/$2/`
- Wait for `output/$2/storage_plan.md` to exist and be non-empty
- `git add output/$2/storage_plan.md && git commit -m "synth($2): storage plan"`

## Stage 2: Base implementation
- Dispatch `base-implementer` subagent with:
  - storage plan: `output/$2/storage_plan.md`
  - reference SQL: `reference/$2.sql`
  - output dir: `output/$2/`
- The subagent iterates internally until `validate.py` passes (build with `make -C template synth SYNTH_DIR=../output/$2`, then validate)
- After it returns, run `validator` subagent for the final PASS confirmation
- If PASS: `git add output/$2/*.cpp output/$2/*.hpp && git commit -m "synth($2): base impl (PASS)"`
- If FAIL: do **not** commit. Report the issue and stop.

## Stage 3: Optimization (Phase 3c, not implemented yet)
- Skip in Phase 3a/3b. A placeholder comment is fine.

## Output
- Summary table: stage / status / artifacts / commit hash
- Final engine binary path: `template/build/db_synth`
- Test command: `template/build/db_synth --data-dir data --query $2`
