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

export async function route(rawMessage: string): Promise<string> {
  const text = rawMessage.trim();

  if (text === "/status") {
    return buildStatus();
  }

  if (text === "/reset") {
    copilot.reset();
    return "Copilot session reset. Starting fresh.";
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
      "  (message)     -> Copilot (claude-sonnet-4.6 via GitHub)",
      "  /code <msg>   -> OmniCode 9B (local :8081)",
      "  /ask  <msg>   -> Gemma 9B (local :8080)",
      "  /status       -> system status",
      "  /reset        -> reset conversation history",
    ].join("\n");
  }

  // Default: GitHub Copilot (claude-sonnet-4.6)
  return await copilot.query(text);
}

function buildStatus(): string {
  return [
    "wechat-copilot status",
    `  Copilot:  claude-sonnet-4.6 via api.githubcopilot.com`,
    `  OmniCode: ${process.env.OMNICODE_URL ?? "http://localhost:8081/v1"}`,
    `  Gemma:    ${process.env.GEMMA_URL    ?? "http://localhost:8080/v1"}`,
    `  WORK_DIR: ${process.env.WORK_DIR     ?? process.cwd()}`,
  ].join("\n");
}
