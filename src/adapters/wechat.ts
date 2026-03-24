/**
 * WeChat adapter — iLink long-poll loop.
 *
 * Owns the polling lifecycle. Calls router.route() for each
 * incoming user message and sends the reply back via iLink.
 *
 * Ownership model (RustCC-aligned):
 *   - creds: borrowed, not stored; reloaded on each start
 *   - syncBuf: owned by this module, persisted to disk
 *   - running flag: owned, controls loop lifetime
 */

import {
  loadCreds, loadSyncBuf, saveSyncBuf,
  getUpdates, sendReply,
  type ILinkCreds, type ILinkMessage,
} from "../ilink.js";
import { route } from "../router.js";
import { WECHAT_ALLOWED_FROM } from "../config.js";

const MSG_TYPE_USER  = 1;
const MSG_ITEM_TEXT  = 1;
const BACKOFF_MS     = 30_000;
const RETRY_MS       = 2_000;
const MAX_FAILURES   = 3;

let running = false;

function isAllowed(fromUser: string): boolean {
  if (WECHAT_ALLOWED_FROM.length === 0) return true;
  return WECHAT_ALLOWED_FROM.includes(fromUser);
}

const MSG_ITEM_VOICE = 3;

function extractText(msg: ILinkMessage): string | null {
  if (msg.message_type !== MSG_TYPE_USER) return null;
  const parts: string[] = [];
  for (const item of msg.item_list ?? []) {
    if (item.type === MSG_ITEM_TEXT && item.text_item?.text) {
      parts.push(item.text_item.text);
    }
    // Voice: iLink transcribes to text automatically
    if (item.type === MSG_ITEM_VOICE && item.voice_item?.text) {
      parts.push(`[voice] ${item.voice_item.text}`);
    }
  }
  return parts.join("\n").trim() || null;
}

async function handleMessage(creds: ILinkCreds, msg: ILinkMessage): Promise<void> {
  const fromUser = msg.from_user_id ?? "";
  if (!isAllowed(fromUser)) {
    console.log(`[wechat] blocked: ${fromUser}`);
    return;
  }

  const text = extractText(msg);
  if (!text) return;

  console.log(`[wechat] ← ${fromUser}: ${text.slice(0, 80)}`);

  let reply: string;
  try {
    reply = await route(text);
  } catch (err: unknown) {
    reply = `Error: ${err instanceof Error ? err.message : String(err)}`;
  }

  console.log(`[wechat] → reply (${reply.length} chars)`);
  await sendReply(creds, fromUser, reply, msg.context_token);
}

export async function start(): Promise<void> {
  if (running) throw new Error("WeChat adapter already running");
  running = true;

  const creds   = loadCreds();
  let syncBuf   = loadSyncBuf();
  let failures  = 0;

  console.log("[wechat] adapter started, polling iLink...");

  while (running) {
    try {
      const resp = await getUpdates(creds, syncBuf);

      if (resp.errcode && resp.errcode !== 0) {
        console.warn(`[wechat] iLink errcode ${resp.errcode} — backing off`);
        await sleep(BACKOFF_MS);
        continue;
      }

      if (resp.get_updates_buf) {
        syncBuf = resp.get_updates_buf;
        saveSyncBuf(syncBuf);
      }

      for (const msg of resp.msgs ?? []) {
        // Fire-and-forget per message; errors are caught inside handleMessage
        handleMessage(creds, msg).catch((err) => {
          console.error(`[wechat] message handler error: ${err}`);
        });
      }

      failures = 0;
    } catch (err: unknown) {
      failures++;
      console.error(`[wechat] poll error (${failures}/${MAX_FAILURES}): ${err}`);
      if (failures >= MAX_FAILURES) {
        console.error("[wechat] too many failures, backing off");
        await sleep(BACKOFF_MS);
        failures = 0;
      } else {
        await sleep(RETRY_MS);
      }
    }
  }
}

export function stop(): void {
  running = false;
  console.log("[wechat] adapter stopped");
}

function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}
