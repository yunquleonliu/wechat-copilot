/**
 * Copilot agent — spawns local Claude Code CLI (VSCode Copilot Chat extension).
 *
 * Uses stream-json protocol over stdin/stdout.
 * The spawned process shares ~/.claude/ with VSCode:
 *   - same session history
 *   - same CLAUDE.md rules
 *   - same MCP tools
 *   - --continue resumes the last VSCode session
 *
 * Ownership: caller must call stop() when done to release the child process.
 */
import { spawn } from "node:child_process";
import readline from "node:readline";
import { CLAUDE_BIN, WORK_DIR, QUERY_TIMEOUT_MS } from "../config.js";
// System prompt appended to every session — sets RustCC context
const SYSTEM_PROMPT = [
    "You are running as a WeChat bridge agent for the RustCC profiler project.",
    "Working directory: " + WORK_DIR,
    "Reply in plain text only — no markdown, WeChat does not render it.",
    "Be concise. This is a chat interface.",
    "You have full access to the local filesystem, build tools, git, and the RustCC profiler.",
    "RustCC rules: enforce ownership (TCC-OWN), lifetime (TCC-LIFE), concurrency (TCC-CONC) semantics.",
].join("\n");
export class CopilotAgent {
    child = null;
    rl = null;
    /** Spawn the Claude Code CLI process. */
    start(continueSession = true) {
        const args = [
            CLAUDE_BIN,
            "--output-format", "stream-json",
            "--input-format", "stream-json",
            "--verbose",
            "--dangerously-skip-permissions",
            "--append-system-prompt", SYSTEM_PROMPT,
        ];
        if (continueSession)
            args.push("--continue");
        this.child = spawn("node", args, {
            cwd: WORK_DIR,
            stdio: ["pipe", "pipe", "inherit"],
            env: { ...process.env },
        });
        this.child.on("error", (err) => {
            console.error(`[copilot] process error: ${err.message}`);
        });
        this.rl = readline.createInterface({ input: this.child.stdout });
    }
    /** Send a prompt and collect the full text response. */
    async query(prompt) {
        if (!this.child || !this.rl || this.child.killed) {
            this.start();
        }
        return new Promise((resolve, reject) => {
            const chunks = [];
            let settled = false;
            const timer = setTimeout(() => {
                if (!settled) {
                    settled = true;
                    reject(new Error("Copilot agent timeout"));
                }
            }, QUERY_TIMEOUT_MS);
            const onLine = (line) => {
                try {
                    const msg = JSON.parse(line);
                    if (msg.type === "text" && msg.text)
                        chunks.push(msg.text);
                    if (msg.type === "result")
                        finalize();
                }
                catch { /* non-JSON debug lines */ }
            };
            const finalize = () => {
                if (settled)
                    return;
                settled = true;
                clearTimeout(timer);
                this.rl.off("line", onLine);
                resolve({ text: chunks.join("").trim() || "(no response)" });
            };
            this.rl.on("line", onLine);
            // Send prompt as stream-json message
            const msg = JSON.stringify({ type: "user", message: { role: "user", content: prompt } });
            this.child.stdin.write(msg + "\n");
        });
    }
    /** Cleanly terminate the child process. */
    stop() {
        this.rl?.close();
        this.rl = null;
        if (this.child && !this.child.killed) {
            this.child.kill("SIGTERM");
        }
        this.child = null;
    }
}
