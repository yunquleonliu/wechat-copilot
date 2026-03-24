# wechat-copilot — project context for Claude Code

## Project
This is the WeChat ↔ Copilot CLI bridge for the RustCC profiler.

## Working directory
Primary codebase: /datapai/Tough-C-Profiler.worktrees/copilot-worktree-2026-03-24T02-26-05

## RustCC Rules (apply to all C/C++ analysis tasks)
- TCC-OWN: enforce unique ownership — T&& means move, original invalid after
- TCC-LIFE: borrowed pointers (T*) must not outlive their owner
- TCC-CONC: no concurrent mutable + shared refs on same data
- Prefer RAII over raw resource management
- Flag raw owning pointers as TCC-OWN violations

## TypeScript code style (this repo)
- Explicit resource ownership — always call stop()/close() on owned processes
- No implicit any — strict TypeScript
- No unhandled promise rejections
- Plain text replies only — WeChat does not render markdown

## Agents available
- Default: Copilot CLI (full agentic, reads/writes local files)
- /code: OmniCode 9B at :8081
- /ask:  Gemma 9B at :8080
