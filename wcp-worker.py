#!/usr/bin/env python3
"""
wcp-worker.py — Project-aware background agent for WeChat messages
Polls /tmp/wcp-queue/inbox/ for new messages from vsc-agent.py relay.
Processes each with Copilot API (gpt-4o) + project-specific tools + memory.
Writes replies to /tmp/wcp-queue/outbox/<id>.json.

Start:
  python3 wcp-worker.py

Stop:
  kill <PID>   (or Ctrl-C)
"""

import json
import os
import re
import signal
import sqlite3
import subprocess
import sys
import time
from urllib.request import Request, urlopen
from urllib.error import URLError, HTTPError

# ── Config ────────────────────────────────────────────────────────────────────
QUEUE_DIR    = os.environ.get("WCP_QUEUE_DIR",    "/tmp/wcp-queue")
WORK_DIR     = os.environ.get("WORK_DIR",         "/datapai/Tough-C-Profiler")
WCP_DIR      = os.environ.get("WCP_DIR",          "/datapai/RustCC-Claw/wechat-copilot")
API_URL      = os.environ.get("COPILOT_API",      "https://api.githubcopilot.com")
MODEL        = os.environ.get("COPILOT_MODEL",    "gpt-4o")
MAX_ROUNDS   = int(os.environ.get("MAX_ROUNDS",   "15"))
API_TIMEOUT  = int(os.environ.get("API_TIMEOUT",  "120"))
POLL_SLEEP   = float(os.environ.get("POLL_SLEEP", "0.5"))
MAX_HISTORY  = int(os.environ.get("MAX_HISTORY",  "20"))
DB_PATH      = os.environ.get("WCP_DB",           os.path.join(QUEUE_DIR, "memory.db"))

INBOX_DIR    = os.path.join(QUEUE_DIR, "inbox")
OUTBOX_DIR   = os.path.join(QUEUE_DIR, "outbox")

SYSTEM_PROMPT = f"""You are a senior dev assistant connected to the RustCC-Profiler + WeChat-Copilot project on this machine.
You reply via WeChat — be concise, use markdown sparingly (WeChat renders plain text).
Reply in Chinese if the user writes Chinese, English otherwise.

## Projects you know
- **RustCC Profiler** at `{WORK_DIR}` — C++ static analyser enforcing 23 safety rules (TCC-OWN, TCC-LIFE, TCC-CONC, TCC-BORROW, TCC-ITER).
  Build: `cd {WORK_DIR} && cmake -B build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux`
  Tests: `cd {WORK_DIR} && ctest --test-dir build-linux --output-on-failure`
  rcc-check binary: `{WORK_DIR}/build-linux/src/rcc-check`

- **WeChat-Copilot (WCP)** at `{WCP_DIR}` — C++17 bridge: WeChat → Copilot API + local LLMs.
  Build: `cd {WCP_DIR}/cpp && cmake -B build && cmake --build build`
  Sidecars: OmniCode-9B on :8081, Gemma-9B on :8080
  Routes: default→you (this worker), /code→OmniCode, /ask→Gemma

## What you can do
- Read, search and explain code in both projects
- Run builds and tests, report results
- Show git log/diff
- Write and edit files (after confirming with user)
- Run any safe shell command

## Safety
Never run: rm -rf, sudo, shutdown, mkfs, curl|bash, dd if=, fork bombs.
Confirm before write_file on source code.
"""

CMD_BLACKLIST = re.compile(
    r'\b(rm\s+-rf|mkfs|shutdown|reboot|dd\s+if=|fork\s*bomb|'
    r'>\s*/dev/sd|chmod\s+777|curl\s+[^|]+\|\s*(?:ba)?sh|wget\s+[^|]+\|\s*(?:ba)?sh)\b',
    re.IGNORECASE)

TOOLS = [
    {"type": "function", "function": {
        "name": "read_file",
        "description": "Read a source file. Paths relative to WORK_DIR or absolute.",
        "parameters": {"type": "object",
                       "properties": {"path": {"type": "string"},
                                      "project": {"type": "string",
                                                  "description": "rustcc | wcp | abs",
                                                  "enum": ["rustcc", "wcp", "abs"]}},
                       "required": ["path"]}}},
    {"type": "function", "function": {
        "name": "list_dir",
        "description": "List directory contents.",
        "parameters": {"type": "object",
                       "properties": {"path": {"type": "string"},
                                      "project": {"type": "string",
                                                  "enum": ["rustcc", "wcp", "abs"]}},
                       "required": ["path"]}}},
    {"type": "function", "function": {
        "name": "search_code",
        "description": "Search for a pattern in source files (ripgrep/grep).",
        "parameters": {"type": "object",
                       "properties": {"pattern":  {"type": "string"},
                                      "dir":      {"type": "string"},
                                      "file_glob":{"type": "string"}},
                       "required": ["pattern"]}}},
    {"type": "function", "function": {
        "name": "run_command",
        "description": "Run a shell command (60s timeout). Safety-blacklisted commands refused.",
        "parameters": {"type": "object",
                       "properties": {"command": {"type": "string"},
                                      "cwd":     {"type": "string"}},
                       "required": ["command"]}}},
    {"type": "function", "function": {
        "name": "git_log",
        "description": "Show recent git commits for a project.",
        "parameters": {"type": "object",
                       "properties": {"project": {"type": "string",
                                                  "enum": ["rustcc", "wcp"]},
                                      "n":       {"type": "integer"}},
                       "required": ["project"]}}},
    {"type": "function", "function": {
        "name": "git_diff",
        "description": "Show git diff or diff of a specific commit.",
        "parameters": {"type": "object",
                       "properties": {"project": {"type": "string",
                                                  "enum": ["rustcc", "wcp"]},
                                      "ref":     {"type": "string"}},
                       "required": ["project"]}}},
    {"type": "function", "function": {
        "name": "write_file",
        "description": "Write or overwrite a file. Use only after user confirmation.",
        "parameters": {"type": "object",
                       "properties": {"path":    {"type": "string"},
                                      "content": {"type": "string"},
                                      "project": {"type": "string",
                                                  "enum": ["rustcc", "wcp", "abs"]}},
                       "required": ["path", "content"]}}},
]

# ── Token ──────────────────────────────────────────────────────────────────────
def load_token() -> str:
    t = os.environ.get("GITHUB_TOKEN", "")
    if t:
        return t
    cred = os.path.expanduser("~/.git-credentials")
    if os.path.exists(cred):
        for line in open(cred):
            m = re.search(r'://[^:]+:([^@]+)@github\.com', line)
            if m:
                return m.group(1).strip()
    raise RuntimeError("No GitHub token")

TOKEN = load_token()

# ── Memory (SQLite per WeChat user) ───────────────────────────────────────────
def init_db(path: str) -> sqlite3.Connection:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    db = sqlite3.connect(path, check_same_thread=False)
    db.execute("""CREATE TABLE IF NOT EXISTS history (
        id       INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id  TEXT NOT NULL,
        role     TEXT NOT NULL,
        content  TEXT NOT NULL,
        ts       REAL NOT NULL
    )""")
    db.commit()
    return db

def load_history(db: sqlite3.Connection, user_id: str) -> list:
    rows = db.execute(
        "SELECT role, content FROM history WHERE user_id=? ORDER BY id DESC LIMIT ?",
        (user_id, MAX_HISTORY)).fetchall()
    return [{"role": r, "content": c} for r, c in reversed(rows)]

def save_turn(db: sqlite3.Connection, user_id: str, role: str, content: str):
    db.execute("INSERT INTO history (user_id,role,content,ts) VALUES (?,?,?,?)",
               (user_id, role, content, time.time()))
    db.commit()

def clear_history(db: sqlite3.Connection, user_id: str):
    db.execute("DELETE FROM history WHERE user_id=?", (user_id,))
    db.commit()

# ── Tool dispatch ──────────────────────────────────────────────────────────────
def resolve_dir(path: str, project: str) -> str:
    if project == "wcp":
        base = WCP_DIR
    elif project == "abs" or os.path.isabs(path):
        return path
    else:
        base = WORK_DIR
    return os.path.join(base, path)

def dispatch(name: str, args: dict) -> str:
    try:
        proj = args.get("project", "rustcc")

        if name == "read_file":
            p = resolve_dir(args["path"], proj)
            with open(p) as f:
                content = f.read(12000)
            return content or "(empty)"

        elif name == "list_dir":
            p = resolve_dir(args["path"], proj)
            entries = sorted(os.listdir(p))
            return "\n".join(entries[:150])

        elif name == "search_code":
            pattern   = args["pattern"]
            search_dir = args.get("dir", WORK_DIR)
            if not os.path.isabs(search_dir):
                search_dir = os.path.join(WORK_DIR, search_dir)
            glob      = args.get("file_glob", "*.{cpp,cc,hpp,h,py}")
            cmd       = f'grep -rn --include="{glob}" -m 5 -- {json.dumps(pattern)} {search_dir} 2>/dev/null | head -40'
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=20)
            return (r.stdout + r.stderr).strip() or "(no matches)"

        elif name == "run_command":
            cmd = args["command"]
            if CMD_BLACKLIST.search(cmd):
                return f"[REFUSED] Safety blacklist matched: {cmd}"
            cwd = args.get("cwd", WORK_DIR)
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                               timeout=60, cwd=cwd)
            out = (r.stdout + r.stderr).strip()
            return (out[:6000] if out else f"(exit {r.returncode})")

        elif name == "git_log":
            repo = WCP_DIR if args.get("project") == "wcp" else WORK_DIR
            n    = args.get("n", 10)
            r    = subprocess.run(
                ["git", "--no-pager", "log", "--oneline", f"-{n}"],
                capture_output=True, text=True, cwd=repo)
            return r.stdout.strip() or r.stderr.strip()

        elif name == "git_diff":
            repo = WCP_DIR if args.get("project") == "wcp" else WORK_DIR
            ref  = args.get("ref", "HEAD")
            r    = subprocess.run(
                ["git", "--no-pager", "diff", ref],
                capture_output=True, text=True, cwd=repo)
            return r.stdout[:8000] or "(no diff)"

        elif name == "write_file":
            p = resolve_dir(args["path"], proj)
            os.makedirs(os.path.dirname(p) or ".", exist_ok=True)
            with open(p, "w") as f:
                f.write(args["content"])
            return f"Written {len(args['content'])} bytes → {p}"

        return f"Unknown tool: {name}"
    except Exception as e:
        return f"Tool error ({name}): {e}"

# ── Copilot API ────────────────────────────────────────────────────────────────
def chat(messages: list) -> dict:
    body = json.dumps({
        "model":    MODEL,
        "messages": messages,
        "tools":    TOOLS,
        "stream":   False,
    }).encode()
    req = Request(
        f"{API_URL}/chat/completions",
        data=body,
        headers={
            "Content-Type":           "application/json",
            "Authorization":          f"Bearer {TOKEN}",
            "Copilot-Integration-Id": "vscode-chat",
            "Editor-Version":         "vscode/1.95.0",
        },
        method="POST",
    )
    with urlopen(req, timeout=API_TIMEOUT) as resp:
        return json.loads(resp.read())

# ── Agent loop ─────────────────────────────────────────────────────────────────
def run_agent(user_text: str, history: list) -> str:
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages += history
    messages.append({"role": "user", "content": user_text})

    for _ in range(MAX_ROUNDS):
        try:
            resp = chat(messages)
        except (URLError, HTTPError) as e:
            return f"API error: {e}"
        except Exception as e:
            return f"Error: {e}"

        choice  = resp["choices"][0]
        msg     = choice["message"]
        reason  = choice.get("finish_reason", "")
        messages.append(msg)

        if reason in ("stop", "end_turn"):
            return msg.get("content") or "(no content)"

        if msg.get("tool_calls") or reason == "tool_calls":
            for tc in msg.get("tool_calls", []):
                fn     = tc["function"]["name"]
                args   = json.loads(tc["function"].get("arguments", "{}"))
                result = dispatch(fn, args)
                messages.append({
                    "role":         "tool",
                    "tool_call_id": tc["id"],
                    "content":      result,
                })
            continue

        if msg.get("content"):
            return msg["content"]

    return "(max rounds reached)"

# ── Queue processing ───────────────────────────────────────────────────────────
def process_inbox(db: sqlite3.Connection):
    files = sorted(f for f in os.listdir(INBOX_DIR) if f.endswith(".json"))
    for fname in files:
        in_path  = os.path.join(INBOX_DIR,  fname)
        msg_id   = fname[:-5]
        out_path = os.path.join(OUTBOX_DIR, f"{msg_id}.json")
        if os.path.exists(out_path):
            continue  # already answered
        try:
            with open(in_path) as f:
                payload = json.load(f)
        except Exception:
            continue

        text    = payload.get("text", "").strip()
        user_id = payload.get("user_id", "default")
        if not text:
            continue

        print(f"[worker] processing {msg_id} from {user_id}: {text[:60]}", flush=True)

        # Special commands
        if text.strip().lower() in ("/reset", "reset"):
            clear_history(db, user_id)
            reply = "Conversation history cleared."
        else:
            history = load_history(db, user_id)
            reply   = run_agent(text, history)
            save_turn(db, user_id, "user",      text)
            save_turn(db, user_id, "assistant", reply)

        with open(out_path, "w") as f:
            json.dump({"reply": reply}, f)
        print(f"[worker] replied {msg_id}: {reply[:80]}", flush=True)

# ── Main loop ──────────────────────────────────────────────────────────────────
def main():
    os.makedirs(INBOX_DIR,  exist_ok=True)
    os.makedirs(OUTBOX_DIR, exist_ok=True)

    db = init_db(DB_PATH)

    print(f"[worker] started  model={MODEL}", flush=True)
    print(f"[worker] inbox:   {INBOX_DIR}", flush=True)
    print(f"[worker] outbox:  {OUTBOX_DIR}", flush=True)
    print(f"[worker] memory:  {DB_PATH}", flush=True)
    print(f"[worker] rustcc:  {WORK_DIR}", flush=True)
    print(f"[worker] wcp:     {WCP_DIR}", flush=True)

    def _stop(sig, frame):
        print("\n[worker] stopping", flush=True)
        sys.exit(0)
    signal.signal(signal.SIGTERM, _stop)

    while True:
        try:
            process_inbox(db)
        except Exception as e:
            print(f"[worker] error: {e}", flush=True)
        time.sleep(POLL_SLEEP)

if __name__ == "__main__":
    main()
