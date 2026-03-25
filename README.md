# wechat-copilot (WCP)

> **EN:** A WeChat → GitHub Copilot bridge bot written in C++17 RustCC style, with a ReAct agentic tool loop and local LLM sidecar support.
>
> **中文：** 一个用 C++17 RustCC 风格实现的微信 → GitHub Copilot 桥接机器人，内嵌 ReAct 工具循环与本地 LLM sidecar 支持。

---

## What it does / 功能概述

**EN:**
WCP bridges WeChat private messages to GitHub Copilot (Claude Sonnet via `api.githubcopilot.com`).
When the model needs local context, it enters a **ReAct loop** — reasoning, calling local tools,
observing results, and reasoning again — before sending a final reply back to WeChat.
A local LLM **sidecar** (OmniCode 9B / Gemma 9B) can be queried independently for offline tasks.

**中文：**
WCP 将微信私聊消息桥接至 GitHub Copilot（通过 `api.githubcopilot.com` 使用 Claude Sonnet）。
当模型需要本地上下文时，进入 **ReAct 循环** —— 推理 → 调用本地工具 → 观察结果 → 再次推理 —— 最终将答复发回微信。
本地 **sidecar** LLM（OmniCode 9B / Gemma 9B）可独立用于离线任务。

---

## Architecture / 架构

```
WeChat ─── iLink long-poll ──► Adapter
                                  │
                               Router
                                  │
                           CopilotAgent (ReAct loop)
                           ┌──────┴────────────────────┐
                     Copilot API              Local Tools
                  (api.githubcopilot.com)   ┌───────────────┐
                                            │ read_file     │
                                            │ list_dir      │
                                            │ run_command * │
                                            │ write_file    │
                                            └───────────────┘
                                                    │
                                           Sidecar LLM (optional)
                                        OmniCode 9B / Gemma 9B
                                        (localhost llama.cpp)
```

`* run_command` is guarded by a safety blacklist (see below).

---

## Built-in Tools / 内置工具

| Tool | Description EN | 描述 |
|------|---------------|------|
| `read_file` | Read a file in WORK_DIR | 读取工作目录中的文件 |
| `list_dir` | List a directory | 列出目录内容 |
| `run_command` | Run a shell command (blacklisted cmds refused) | 执行 shell 命令（危险命令被拒绝） |
| `write_file` | Write / overwrite a file | 写入文件 |

> **Roadmap:** Tool ecosystem needs strengthening — more tools (web search, git ops, build system integration, test runner, diff/patch) are planned.
>
> **路线图：** 工具生态有待加强 —— 计划新增更多工具（网络搜索、git 操作、构建系统集成、测试运行、diff/patch 等）。

### Safety Blacklist / 安全黑名单

`run_command` refuses patterns including: `rm -rf`, `sudo`, `shutdown`, `mkfs`, `dd if=`, reverse shells (`bash -i`, `/dev/tcp`), pipe-to-shell (`| bash`), credential exposure (`/etc/shadow`, `id_rsa`), fork bombs, and more.

---

## Comparison with OpenClaw / 与 OpenClaw 的差距

| Capability / 能力 | WCP | OpenClaw level |
|---|---|---|
| Multi-user / concurrency | ❌ Single-thread poll | ✅ Concurrent sessions |
| Group chat | ❌ Private chat only | ✅ Group context |
| Tool ecosystem | 4 built-in (expanding) | Dozens of plugins |
| Memory / persistence | MAX_HISTORY=20 in-process | Vector memory store |
| Execution sandbox | Blacklist filter | Container isolation |
| Streaming replies | ❌ One-shot response | ✅ Streaming output |
| Multimodal | ❌ Text only | ✅ Image / voice |

**EN:** WCP is a single-user script-level agent. OpenClaw is a production-grade multi-tenant agent platform. WCP's goal is to close this gap incrementally, starting with tool depth.

**中文：** WCP 目前是单用户脚本级 agent，OpenClaw 是生产级多租户 agent 平台。WCP 的目标是逐步缩小差距，首先从工具深度入手。

---

## Quick Start / 快速启动

### Prerequisites / 前提条件

```bash
apt install libcurl4-openssl-dev cmake g++
```

### Build / 构建

```bash
cd cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Configure / 配置

Copy and edit `.env`:

```bash
cp .env.example .env   # or create from CLAUDE.md
```

Key variables:

```env
GITHUB_TOKEN=gho_...          # GitHub OAuth token (Copilot access)
WORK_DIR=/path/to/project     # Working directory for tool execution
WECHAT_ALLOWED_FROM=<user_id> # Comma-separated allowed WeChat user IDs
```

### Login to WeChat iLink / 登录微信 iLink

```bash
./cpp/build/wcp-setup
```

### Run / 运行

```bash
screen -dmS wcp-bridge bash -c 'set -a && . .env && set +a && cd cpp/build && ./wcp-bridge'
```

---

## Implementation / 实现说明

- **Language:** C++17, RustCC style (TCC-OWN / TCC-LIFE / TCC-CONC rules enforced)
- **HTTP:** libcurl with RAII wrappers (`CurlHandle`, `CurlSlist`)
- **JSON:** nlohmann/json (vendored)
- **Memory safety:** Verified with `rcc-check` — all 10 translation units pass
- **No exceptions** propagate through the agent loop; all errors surface as strings

---

## License / 许可

MIT
