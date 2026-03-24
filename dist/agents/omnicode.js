/**
 * OmniCode sidecar — calls local OmniCode 9B via OpenAI-compat API (:8081)
 * Use with /code prefix for fast local code completions.
 */
import { OMNICODE_URL, OMNICODE_MODEL, API_TIMEOUT_MS } from "../config.js";
export async function queryOmnicode(prompt) {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), API_TIMEOUT_MS * 8); // ~2min for generation
    try {
        const res = await fetch(`${OMNICODE_URL}/chat/completions`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                model: OMNICODE_MODEL,
                messages: [{ role: "user", content: prompt }],
                stream: false,
            }),
            signal: ctrl.signal,
        });
        clearTimeout(timer);
        if (!res.ok)
            throw new Error(`OmniCode HTTP ${res.status}`);
        const data = await res.json();
        return data.choices?.[0]?.message?.content?.trim() ?? "(no response)";
    }
    catch (err) {
        clearTimeout(timer);
        if (err instanceof Error && err.name === "AbortError")
            return "OmniCode timed out.";
        throw err;
    }
}
