/**
 * Copilot agent — calls GitHub Copilot API (OpenAI-compat) directly.
 *
 * Uses the GitHub OAuth token from git credentials (same account as VSCode).
 * Model: claude-sonnet-4.6 via GitHub Copilot — no separate Anthropic billing.
 *
 * Session memory: per-instance message history for multi-turn conversation.
 */

import fs from "node:fs";
import { QUERY_TIMEOUT_MS } from "../config.js";

const COPILOT_API = "https://api.githubcopilot.com";
const MODEL = process.env.COPILOT_MODEL ?? "claude-sonnet-4.6";
const MAX_HISTORY = 20;

const SYSTEM_PROMPT =
  "You are a coding agent for the RustCC profiler project, accessible via WeChat. " +
  "You have full access to the local filesystem, build tools, git, and the RustCC profiler. " +
  "RustCC rules: enforce ownership (TCC-OWN), lifetime (TCC-LIFE), concurrency (TCC-CONC) semantics. " +
  "Reply in plain text only — no markdown, WeChat does not render it. Be concise.";

interface Message { role: "system" | "user" | "assistant"; content: string; }

function loadToken(): string {
  if (process.env.GITHUB_TOKEN) return process.env.GITHUB_TOKEN;
  try {
    const creds = fs.readFileSync("/home/vtuser/.git-credentials", "utf-8");
    const match = creds.match(/https:\/\/\d+:(gho_[^@\s]+)@github\.com/);
    if (match) return match[1];
  } catch { /* ignore */ }
  throw new Error("No GitHub token found. Set GITHUB_TOKEN in .env");
}

export class CopilotAgent {
  private history: Message[] = [];
  private readonly token: string;

  constructor() {
    this.token = loadToken();
  }

  reset(): void {
    this.history = [];
  }

  async query(prompt: string): Promise<string> {
    this.history.push({ role: "user", content: prompt });
    if (this.history.length > MAX_HISTORY) {
      this.history = this.history.slice(-MAX_HISTORY);
    }

    const messages: Message[] = [
      { role: "system", content: SYSTEM_PROMPT },
      ...this.history,
    ];

    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), QUERY_TIMEOUT_MS);

    try {
      const res = await fetch(`${COPILOT_API}/chat/completions`, {
        method: "POST",
        headers: {
          Authorization:            `Bearer ${this.token}`,
          "Content-Type":           "application/json",
          "Copilot-Integration-Id": "vscode-chat",
          "Editor-Version":         "vscode/1.95.0",
        },
        body: JSON.stringify({ model: MODEL, messages, stream: false }),
        signal: ctrl.signal,
      });
      clearTimeout(timer);

      if (!res.ok) {
        const errText = await res.text();
        throw new Error(`Copilot API ${res.status}: ${errText}`);
      }

      const data = await res.json() as { choices: { message: { content: string } }[] };
      const reply = data.choices?.[0]?.message?.content?.trim() ?? "(no response)";
      this.history.push({ role: "assistant", content: reply });
      return reply;

    } catch (err: unknown) {
      clearTimeout(timer);
      if (err instanceof Error && err.name === "AbortError") return "Copilot timed out.";
      throw err;
    }
  }
}
