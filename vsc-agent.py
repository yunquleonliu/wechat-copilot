#!/usr/bin/env python3
"""
vsc-agent.py — WeChat → VSCode session relay
Listens on 127.0.0.1:9191, receives POST /chat {"text": "..."}
Writes message to /tmp/wcp-queue/inbox/<id>.json
Polls /tmp/wcp-queue/outbox/<id>.json for reply (RELAY_TIMEOUT seconds)
The active VSCode/Copilot CLI session watches the inbox and writes replies.

Start: python3 vsc-agent.py
Check pending:  ls /tmp/wcp-queue/inbox/
Reply to <id>:  echo "your reply" > /tmp/wcp-queue/outbox/<id>.json
"""

import json
import os
import time
import uuid
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT          = int(os.environ.get("VSC_AGENT_PORT", 9191))
QUEUE_DIR     = os.environ.get("WCP_QUEUE_DIR", "/tmp/wcp-queue")
RELAY_TIMEOUT = int(os.environ.get("RELAY_TIMEOUT", 300))   # 5 min
POLL_INTERVAL = 0.5                                          # seconds

INBOX_DIR  = os.path.join(QUEUE_DIR, "inbox")
OUTBOX_DIR = os.path.join(QUEUE_DIR, "outbox")

def ensure_dirs():
    os.makedirs(INBOX_DIR,  exist_ok=True)
    os.makedirs(OUTBOX_DIR, exist_ok=True)

def relay(text: str, user_id: str = "default") -> str:
    """Write to inbox, poll outbox, return reply or timeout message."""
    msg_id   = uuid.uuid4().hex[:12]
    in_path  = os.path.join(INBOX_DIR,  f"{msg_id}.json")
    out_path = os.path.join(OUTBOX_DIR, f"{msg_id}.json")

    payload = {"id": msg_id, "text": text, "user_id": user_id, "ts": time.time()}
    with open(in_path, "w") as f:
        json.dump(payload, f)
    print(f"[vsc-agent] queued {msg_id}: {text[:60]}", flush=True)

    deadline = time.time() + RELAY_TIMEOUT
    while time.time() < deadline:
        if os.path.exists(out_path):
            try:
                with open(out_path) as f:
                    data = json.load(f)
                reply = data.get("reply", "(empty reply)")
            except Exception:
                reply = open(out_path).read().strip()
            # clean up
            try: os.remove(in_path)
            except: pass
            try: os.remove(out_path)
            except: pass
            print(f"[vsc-agent] reply {msg_id}: {reply[:60]}", flush=True)
            return reply
        time.sleep(POLL_INTERVAL)

    # Timeout — leave inbox file for manual processing
    return f"[VSCode session timeout — message queued as {msg_id}]"

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[vsc-agent] {fmt % args}", flush=True)

    def do_GET(self):
        if self.path == "/health":
            pending = len(os.listdir(INBOX_DIR)) if os.path.exists(INBOX_DIR) else 0
            self._respond(200, {"status": "relay", "queue": QUEUE_DIR, "pending": pending})
        elif self.path == "/pending":
            files = sorted(os.listdir(INBOX_DIR)) if os.path.exists(INBOX_DIR) else []
            msgs  = []
            for f in files:
                try:
                    with open(os.path.join(INBOX_DIR, f)) as fh:
                        msgs.append(json.load(fh))
                except: pass
            self._respond(200, {"pending": msgs})
        else:
            self._respond(404, {"error": "not found"})

    def do_POST(self):
        if self.path == "/chat":
            length = int(self.headers.get("Content-Length", 0))
            body   = self.rfile.read(length)
            try:
                req     = json.loads(body)
                text    = req.get("text", "").strip()
                user_id = req.get("user_id", "default")
                if not text:
                    self._respond(400, {"error": "missing text"}); return
                reply = relay(text, user_id)
                self._respond(200, {"reply": reply})
            except Exception as e:
                self._respond(500, {"error": str(e)})

        elif self.path == "/reply":
            # Allow the VSCode session watcher to POST replies back
            # POST /reply {"id": "<msg_id>", "reply": "..."}
            length = int(self.headers.get("Content-Length", 0))
            body   = self.rfile.read(length)
            try:
                req    = json.loads(body)
                msg_id = req.get("id", "").strip()
                reply  = req.get("reply", "").strip()
                if not msg_id or not reply:
                    self._respond(400, {"error": "missing id or reply"}); return
                out_path = os.path.join(OUTBOX_DIR, f"{msg_id}.json")
                with open(out_path, "w") as f:
                    json.dump({"reply": reply}, f)
                self._respond(200, {"ok": True})
            except Exception as e:
                self._respond(500, {"error": str(e)})

        else:
            self._respond(404, {"error": "not found"})

    def _respond(self, code: int, data: dict):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

def main():
    ensure_dirs()
    print(f"[vsc-agent] Relay mode on 127.0.0.1:{PORT}", flush=True)
    print(f"[vsc-agent] Inbox:  {INBOX_DIR}", flush=True)
    print(f"[vsc-agent] Outbox: {OUTBOX_DIR}", flush=True)
    print(f"[vsc-agent] Timeout: {RELAY_TIMEOUT}s", flush=True)
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()

if __name__ == "__main__":
    main()
