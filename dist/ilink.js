/**
 * iLink API — WeChat bot gateway
 * Protocol lifted from weixin-claude-code (MIT), rewritten for clarity.
 *
 * Ownership: this module owns no persistent state.
 * Callers are responsible for storing sync_buf between calls.
 */
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import crypto from "node:crypto";
import { LONG_POLL_TIMEOUT_MS, API_TIMEOUT_MS } from "./config.js";
export const CRED_PATH = path.join(os.homedir(), ".claude", "channels", "wechat", "account.json");
export const SYNC_PATH = path.join(os.homedir(), ".claude", "channels", "wechat", "sync_buf.txt");
// ── Credentials ─────────────────────────────────────────
export function loadCreds() {
    if (!fs.existsSync(CRED_PATH)) {
        throw new Error(`No WeChat credentials. Run: npm run setup`);
    }
    const raw = JSON.parse(fs.readFileSync(CRED_PATH, "utf-8"));
    if (!raw.token)
        throw new Error("WeChat credentials missing token field");
    return { token: raw.token, baseUrl: raw.baseUrl || "https://ilinkai.weixin.qq.com" };
}
export function loadSyncBuf() {
    try {
        return fs.existsSync(SYNC_PATH) ? fs.readFileSync(SYNC_PATH, "utf-8") : "";
    }
    catch {
        return "";
    }
}
export function saveSyncBuf(buf) {
    fs.mkdirSync(path.dirname(SYNC_PATH), { recursive: true });
    fs.writeFileSync(SYNC_PATH, buf, "utf-8");
}
// ── HTTP helpers ─────────────────────────────────────────
function buildHeaders(token, body) {
    const h = {
        "Content-Type": "application/json",
        Authorization: `Bearer ${token}`,
        AuthorizationType: "ilink_bot_token",
        "X-WECHAT-UIN": Buffer.from(String(crypto.randomBytes(4).readUInt32BE(0))).toString("base64"),
    };
    if (body)
        h["Content-Length"] = String(Buffer.byteLength(body, "utf-8"));
    return h;
}
// ── API calls ────────────────────────────────────────────
export async function getUpdates(creds, syncBuf) {
    const body = JSON.stringify({ get_updates_buf: syncBuf });
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), LONG_POLL_TIMEOUT_MS);
    try {
        const res = await fetch(`${creds.baseUrl}/ilink/bot/getupdates`, {
            method: "POST",
            headers: buildHeaders(creds.token, body),
            body,
            signal: ctrl.signal,
        });
        clearTimeout(timer);
        return await res.json();
    }
    catch (err) {
        clearTimeout(timer);
        if (err instanceof Error && err.name === "AbortError") {
            return { ret: 0, msgs: [], get_updates_buf: syncBuf };
        }
        throw err;
    }
}
export async function sendReply(creds, toUser, text, contextToken) {
    const clientId = `wcp-${Date.now()}-${crypto.randomBytes(4).toString("hex")}`;
    const payload = {
        msg: {
            from_user_id: "",
            to_user_id: toUser,
            client_id: clientId,
            message_type: 2, // MSG_TYPE_BOT
            message_state: 2, // MSG_STATE_FINISH
            item_list: [{ type: 1, text_item: { text } }],
            context_token: contextToken,
        },
    };
    const body = JSON.stringify(payload);
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), API_TIMEOUT_MS);
    try {
        const res = await fetch(`${creds.baseUrl}/ilink/bot/sendmessage`, {
            method: "POST",
            headers: buildHeaders(creds.token, body),
            body,
            signal: ctrl.signal,
        });
        clearTimeout(timer);
        if (!res.ok)
            throw new Error(`sendMessage ${res.status}: ${await res.text()}`);
    }
    catch (err) {
        clearTimeout(timer);
        throw err;
    }
}
export async function getBotQRCode(baseUrl) {
    const body = JSON.stringify({ bot_type: 3 });
    const res = await fetch(`${baseUrl}/ilink/bot/get_bot_qrcode`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body,
    });
    return await res.json();
}
export async function pollQRLogin(baseUrl, ticket) {
    const body = JSON.stringify({ ticket });
    const res = await fetch(`${baseUrl}/ilink/bot/query_qrcode_status`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body,
    });
    return await res.json();
}
