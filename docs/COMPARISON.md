# Comparison with the Paper and Upstream

## Paper

*Bespoke OLAP: Synthesizing Workload-Specific One-size-fits-one Database Engines*
Wehrstein et al., 2026 (PVLDB submission)
arXiv: https://arxiv.org/abs/2603.02001

## Mapping

| Paper concept | Upstream artifact | This fork |
|---|---|---|
| Storage plan agent | `run_gen_storage_plan.py` + ScriptedConversation | `.claude/agents/storage-planner.md` |
| Base implementation agent | `run_gen_base_impl.py` | `.claude/agents/base-implementer.md` |
| Optimization agent (4 rounds) | `run_optim_loop.py` + OptimizationConversation | (planned for Phase 3c) |
| Accuracy gate | `tools/validate_tool/` + DuckDB | `validate.py` + same DuckDB harness |
| Performance Evaluator | folded into the optimization loop | (planned for Phase 3c) |
| Hotpatching | `template/compiler.py` + dlopen in `db.cpp` | (planned for Phase 3c; `template/db.cpp` is preserved unchanged) |
| Regression monitor | rollback inside OptimizationConversation | git snapshot + `git reset --hard` |
| Expert knowledge | `conversations/prompts/expert_knowledge.txt` | `docs/expert_knowledge.md` (verbatim copy) |
| LLM | GPT-5.2-codex | Claude Opus 4.7 |
| Tool interface | OpenAI Agents SDK with 4 tools (ApplyPatch / Shell / compile / run) | Claude Code's Edit / Bash + a `validator` subagent |

## What we kept identical

- TPC-H benchmark and `reference/q1.sql` semantics
- DuckDB as the correctness oracle
- The C++ engine skeleton in `template/` (Apache Arrow + Parquet for data loading, plugin/dlopen plumbing in `template/db.cpp` and `template/utils/`)
- The curated optimization knowledge file, content unchanged

## What we changed structurally

- **Orchestrator language**: Python (OpenAI Agents SDK) becomes Markdown (Claude Code slash command + subagent definitions).
- **Agent state**: ScriptedConversation JSON files become a slash command that dispatches subagents in sequence.
- **Build model**: dlopen + hot-reload is shelved for Phase 3a/3b in favor of a static-link host (`template/db_static.cpp`). The dlopen path will return in Phase 3c.
- **LLM cache / conversation snapshot**: dropped; Claude Code's own session handling covers what we need at this stage.
- **wandb tracking**: dropped. Synthesis progress is read from `git log` and the validator's stdout.

## What is not yet equivalent

- The 4-round optimization loop is not yet implemented. Phase 3a/3b stop after the base implementation passes validation.
- Hotpatching for fast iteration is not yet implemented. Each synthesis cycle currently does a full static link.
- The CEB benchmark (IMDB dataset) is out of scope until at least Phase 3+.
- Only single-query synthesis is supported so far. Multi-query workloads (e.g. the paper's full TPC-H set) are a later consideration.
