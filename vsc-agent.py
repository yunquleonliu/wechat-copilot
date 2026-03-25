#!/usr/bin/env python3
"""
vsc-agent.py — VSCode Copilot Agent HTTP Bridge
Listens on 127.0.0.1:9191, receives POST /chat {"text": "..."}
Runs full agentic loop via GitHub Copilot API (same as VSCode session).
Start: python3 vsc-agent.py
"""

import json
import os
import re
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.request import Request, urlopen
from urllib.error import URLError

# ── Config ────────────────────────────────────────────────────────────────────
PORT        = int(os.environ.get("VSC_AGENT_PORT", 9191))
API_URL     = os.environ.get("COPILOT_API", "https://api.githubcopilot.com")
MODEL       = os.environ.get("COPILOT_MODEL", "claude-sonnet-4.6")
WORK_DIR    = os.environ.get("WORK_DIR", os.getcwd())
MAX_ROUNDS  = 10
TIMEOUT     = 180

SYSTEM_PROMPT = (
    "You are GitHub Copilot running as a VSCode agent bridge for WeChat. "
    "You have access to tools: read_file, list_dir, run_command, write_file. "
    f"Work directory: {WORK_DIR}. "
    "Answer concisely in Chinese or English matching the user's language. "
    "For code tasks, use tools to read/write files and run commands. "
    "Always confirm what you did at the end."
)

TOOLS = [
    {"type": "function", "function": {
        "name": "read_file",
        "description": "Read a file's contents",
        "parameters": {"type": "object",
                       "properties": {"path": {"type": "string"}},
                       "required": ["path"]}}},
    {"type": "function", "function": {
        "name": "list_dir",
        "description": "List directory contents",
        "parameters": {"type": "object",
                       "properties": {"path": {"type": "string"}},
                       "required": ["path"]}}},
    {"type": "function", "function": {
        "name": "run_command",
        "description": "Run a shell command (30s timeout). No rm/sudo/shutdown.",
        "parameters": {"type": "object",
                       "properties": {"command": {"type": "string"}},
                       "required": ["command"]}}},
    {"type": "function", "function": {
        "name": "write_file",
        "description": "Write content to a file",
        "parameters": {"type": "object",
                       "properties": {"path": {"type": "string"},
                                      "content": {"type": "string"}},
                       "required": ["path", "content"]}}},
]

BLACKLIST = re.compile(
    r'\b(rm\s+-rf|mkfs|shutdown|reboot|dd\s+if|'
    r'chmod\s+777|sudo|curl\s+.*\|\s*bash|wget\s+.*\|\s*sh)\b')

# ── GitHub token ──────────────────────────────────────────────────────────────
def load_token() -> str:
    t = os.environ.get("GITHUB_TOKEN", "")
    if t:
        return t
    cred = os.path.expanduser("~/.git-credentials")
    if os.path.exists(cred):
        for line in open(cred):
            m = re.search(r'://([^:]+):([^@]+)@github\.com', line)
            if m:
                return m.group(2).strip()
    raise RuntimeError("No GitHub token. Set GITHUB_TOKEN or ~/.git-credentials")

TOKEN = load_token()

# ── Tool dispatch ─────────────────────────────────────────────────────────────
def dispatch_tool(name: str, args: dict) -> str:
    try:
        if name == "read_file":
            p = os.path.join(WORK_DIR, args["path"]) if not os.path.isabs(args["path"]) else args["path"]
            with open(p) as f:
                content = f.read(8192)
            return content if content else "(empty file)"

        elif name == "list_dir":
            p = os.path.join(WORK_DIR, args["path"]) if not os.path.isabs(args["path"]) else args["path"]
            entries = os.listdir(p)
            return "\n".join(sorted(entries)[:100])

        elif name == "run_command":
            cmd = args["command"]
            if BLACKLIST.search(cmd):
                return f"[BLOCKED] Command contains forbidden pattern: {cmd}"
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                               timeout=30, cwd=WORK_DIR)
            out = (r.stdout + r.stderr).strip()
            return out[:4096] if out else f"(exit {r.returncode})"

        elif name == "write_file":
            p = os.path.join(WORK_DIR, args["path"]) if not os.path.isabs(args["path"]) else args["path"]
            os.makedirs(os.path.dirname(p) or ".", exist_ok=True)
            with open(p, "w") as f:
                f.write(args["content"])
            return f"Written {len(args['content'])} bytes to {p}"

        else:
            return f"Unknown tool: {name}"
    except Exception as e:
        return f"Tool error: {e}"

# ── Copilot API call ──────────────────────────────────────────────────────────
def copilot_chat(messages: list) -> dict:
    body = json.dumps({
        "model": MODEL,
        "messages": messages,
        "tools": TOOLS,
        "stream": False,
    }).encode()
    req = Request(
        f"{API_URL}/chat/completions",
        data=body,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {TOKEN}",
            "Copilot-Integration-Id": "vscode-chat",
            "Editor-Version": "vscode/1.95.0",
        },
        method="POST",
    )
    with urlopen(req, timeout=TIMEOUT) as resp:
        return json.loads(resp.read())

# ── Agentic loop ──────────────────────────────────────────────────────────────
def run_agent(prompt: str) -> str:
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user",   "content": prompt},
    ]
    for _ in range(MAX_ROUNDS):
        try:
            resp = copilot_chat(messages)
        except URLError as e:
            return f"Copilot API error: {e}"

        choice  = resp["choices"][0]
        message = choice["message"]
        reason  = choice.get("finish_reason", "")

        messages.append(message)

        if reason == "stop" or reason == "end_turn":
            return message.get("content") or "(no response)"

        if reason == "tool_calls" or message.get("tool_calls"):
            for tc in message.get("tool_calls", []):
                fn   = tc["function"]["name"]
                args = json.loads(tc["function"].get("arguments", "{}"))
                result = dispatch_tool(fn, args)
                messages.append({
                    "role": "tool",
                    "tool_call_id": tc["id"],
                    "content": result,
                })
            continue

        # Fallback: return content if present
        content = message.get("content", "")
        if content:
            return content

    return "(max rounds reached)"

# ── HTTP server ───────────────────────────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[vsc-agent] {fmt % args}", flush=True)

    def do_GET(self):
        if self.path == "/health":
            self._respond(200, {"status": "ok", "model": MODEL, "work_dir": WORK_DIR})
        else:
            self._respond(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/chat":
            self._respond(404, {"error": "use POST /chat"}); return
        length = int(self.headers.get("Content-Length", 0))
        body   = self.rfile.read(length)
        try:
            req  = json.loads(body)
            text = req.get("text", "").strip()
            if not text:
                self._respond(400, {"error": "missing text"}); return
            print(f"[vsc-agent] query: {text[:80]}", flush=True)
            reply = run_agent(text)
            print(f"[vsc-agent] reply: {reply[:80]}", flush=True)
            self._respond(200, {"reply": reply})
        except Exception as e:
            self._respond(500, {"error": str(e)})

    def _respond(self, code: int, data: dict):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

def main():
    print(f"[vsc-agent] Starting on 127.0.0.1:{PORT}", flush=True)
    print(f"[vsc-agent] Model: {MODEL}  WorkDir: {WORK_DIR}", flush=True)
    srv = HTTPServer(("127.0.0.1", PORT), Handler)
    srv.serve_forever()

if __name__ == "__main__":
    main()
