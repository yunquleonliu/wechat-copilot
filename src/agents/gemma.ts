/**
 * Gemma sidecar — calls local Gemma 9B via OpenAI-compat API (:8080)
 * Use with /ask prefix for general Q&A.
 */

import { GEMMA_URL, GEMMA_MODEL, API_TIMEOUT_MS } from "../config.js";

export async function queryGemma(prompt: string): Promise<string> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), API_TIMEOUT_MS * 8);
  try {
    const res = await fetch(`${GEMMA_URL}/chat/completions`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        model:    GEMMA_MODEL,
        messages: [{ role: "user", content: prompt }],
        stream:   false,
      }),
      signal: ctrl.signal,
    });
    clearTimeout(timer);
    if (!res.ok) throw new Error(`Gemma HTTP ${res.status}`);
    const data = await res.json() as {
      choices: { message: { content: string } }[];
    };
    return data.choices?.[0]?.message?.content?.trim() ?? "(no response)";
  } catch (err: unknown) {
    clearTimeout(timer);
    if (err instanceof Error && err.name === "AbortError") return "Gemma timed out.";
    throw err;
  }
}
