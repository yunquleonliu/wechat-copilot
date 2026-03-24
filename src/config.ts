// ── Environment config ──────────────────────────────────

export const WORK_DIR = process.env.WORK_DIR || process.cwd();

// Local Claude Code CLI (VSCode Copilot Chat extension)
export const CLAUDE_BIN = process.env.CLAUDE_BIN ||
  "/home/vtuser/.vscode-server/extensions/github.copilot-chat-0.40.1/dist/cli.js";

// Sidecar endpoints
export const OMNICODE_URL = process.env.OMNICODE_URL || "http://localhost:8081/v1";
export const OMNICODE_MODEL = process.env.OMNICODE_MODEL || "omnicode-9b";
export const GEMMA_URL = process.env.GEMMA_URL || "http://localhost:8080/v1";
export const GEMMA_MODEL = process.env.GEMMA_MODEL || "gemma2-9b";

// WeChat iLink
export const WECHAT_ALLOWED_FROM = process.env.WECHAT_ALLOWED_FROM
  ? process.env.WECHAT_ALLOWED_FROM.split(",").map((s) => s.trim()).filter(Boolean)
  : [];

// Session file
export const SESSION_FILE = `${WORK_DIR}/.wechat-copilot-sessions.json`;

// Timeouts
export const QUERY_TIMEOUT_MS = Number(process.env.QUERY_TIMEOUT_MS) || 180_000;
export const LONG_POLL_TIMEOUT_MS = 35_000;
export const API_TIMEOUT_MS = 15_000;
