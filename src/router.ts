/**
 * Router — dispatches WeChat messages to the right agent by prefix.
 *
 * Routing table:
 *   /code <prompt>   → OmniCode 9B sidecar (fast local code completions)
 *   /ask <prompt>    → Gemma 9B sidecar (general Q&A)
 *   /status          → local system status
 *   /reset           → clear Copilot session
 *   (default)        → Copilot CLI agent (full agentic, shares VSCode session)
 */

import { CopilotAgent } from "./agents/copilot.js";
import { queryOmnicode } from "./agents/omnicode.js";
import { queryGemma }    from "./agents/gemma.js";

const copilot = new CopilotAgent();
copilot.start();

export async function route(rawMessage: string): Promise<string> {
  const text = rawMessage.trim();

  if (text === "/status") {
    return buildStatus();
  }

  if (text === "/reset") {
    copilot.stop();
    copilot.start(false); // fresh session, no --continue
    return "✅ Copilot session reset. Starting fresh.";
  }

  if (text.startsWith("/code ")) {
    const prompt = text.slice(6).trim();
    if (!prompt) return "Usage: /code <your coding question>";
    return `[OmniCode]\n${await queryOmnicode(prompt)}`;
  }

  if (text.startsWith("/ask ")) {
    const prompt = text.slice(5).trim();
    if (!prompt) return "Usage: /ask <your question>";
    return `[Gemma]\n${await queryGemma(prompt)}`;
  }

  if (text === "/help") {
    return [
      "wechat-copilot commands:",
      "  (message)     → Copilot CLI (local VSCode agent, shared session)",
      "  /code <msg>   → OmniCode 9B (fast code completions)",
      "  /ask  <msg>   → Gemma 9B (general Q&A)",
      "  /status       → system status",
      "  /reset        → reset Copilot session",
    ].join("\n");
  }

  // Default: full Copilot agent
  const result = await copilot.query(text);
  return result.text;
}

function buildStatus(): string {
  const lines = [
    "wechat-copilot status",
    `  Copilot CLI:  ${process.env.CLAUDE_BIN ?? "default path"}`,
    `  OmniCode:     ${process.env.OMNICODE_URL ?? "http://localhost:8081/v1"}`,
    `  Gemma:        ${process.env.GEMMA_URL    ?? "http://localhost:8080/v1"}`,
    `  WORK_DIR:     ${process.env.WORK_DIR     ?? process.cwd()}`,
  ];
  return lines.join("\n");
}

// Ensure clean shutdown on exit
process.on("SIGTERM", () => { copilot.stop(); process.exit(0); });
process.on("SIGINT",  () => { copilot.stop(); process.exit(0); });
