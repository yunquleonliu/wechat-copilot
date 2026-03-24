/**
 * WeChat QR login — saves iLink token to ~/.claude/channels/wechat/account.json
 * Adapted from weixin-claude-code (MIT).
 */

import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import crypto from "node:crypto";

const BASE_URL = "https://ilinkai.weixin.qq.com";
const QR_POLL_TIMEOUT_MS = 35_000;
const MAX_QR_REFRESH = 3;
const CRED_DIR = path.join(os.homedir(), ".claude", "channels", "wechat");
const CRED_PATH = path.join(CRED_DIR, "account.json");

function randomUin(): string {
  return Buffer.from(String(crypto.randomBytes(4).readUInt32BE(0)), "utf-8").toString("base64");
}

async function fetchQR(): Promise<{ qrcode: string; qrcode_img_content: string }> {
  const res = await fetch(`${BASE_URL}/ilink/bot/get_bot_qrcode?bot_type=3`, {
    headers: { "X-WECHAT-UIN": randomUin() },
  });
  if (!res.ok) throw new Error(`QR fetch failed: ${res.status}`);
  return res.json();
}

async function pollStatus(qrcode: string) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), QR_POLL_TIMEOUT_MS);
  try {
    const res = await fetch(
      `${BASE_URL}/ilink/bot/get_qrcode_status?qrcode=${encodeURIComponent(qrcode)}`,
      {
        headers: { "iLink-App-ClientVersion": "1", "X-WECHAT-UIN": randomUin() },
        signal: ctrl.signal,
      },
    );
    clearTimeout(timer);
    if (!res.ok) throw new Error(`poll status: ${res.status}`);
    return res.json() as Promise<{
      status: "wait" | "scaned" | "confirmed" | "expired";
      bot_token?: string;
      ilink_bot_id?: string;
      baseurl?: string;
      ilink_user_id?: string;
    }>;
  } catch (err: unknown) {
    clearTimeout(timer);
    if (err instanceof Error && err.name === "AbortError") return { status: "wait" as const };
    throw err;
  }
}

async function main() {
  console.log("\n╔════════════════════════════════════╗");
  console.log("║   wechat-copilot  WeChat Login     ║");
  console.log("╚════════════════════════════════════╝\n");

  let qr = await fetchQR();
  let refreshCount = 0;

  const showQR = async (content: string) => {
    const mod = await import("qrcode-terminal");
    mod.default.generate(content, { small: true });
  };

  await showQR(qr.qrcode_img_content);
  console.log("\n📱 Scan the QR code with WeChat (iOS)\n");

  const deadline = Date.now() + 5 * 60_000;
  let scanned = false;

  while (Date.now() < deadline) {
    const s = await pollStatus(qr.qrcode);

    if (s.status === "scaned" && !scanned) {
      console.log("👀 Scanned — confirm in WeChat...");
      scanned = true;
    }

    if (s.status === "expired") {
      refreshCount++;
      if (refreshCount >= MAX_QR_REFRESH) {
        console.error("❌ QR expired too many times. Re-run setup.");
        process.exit(1);
      }
      console.log(`\n⏳ Expired, refreshing... (${refreshCount}/${MAX_QR_REFRESH})`);
      qr = await fetchQR();
      scanned = false;
      await showQR(qr.qrcode_img_content);
    }

    if (s.status === "confirmed") {
      if (!s.bot_token) { console.error("❌ No token returned"); process.exit(1); }

      fs.mkdirSync(CRED_DIR, { recursive: true });
      const creds = {
        token:     s.bot_token,
        baseUrl:   s.baseurl || BASE_URL,
        accountId: s.ilink_bot_id,
        userId:    s.ilink_user_id,
        savedAt:   new Date().toISOString(),
      };
      fs.writeFileSync(CRED_PATH, JSON.stringify(creds, null, 2), "utf-8");
      fs.chmodSync(CRED_PATH, 0o600);

      console.log("\n✅ Login successful!");
      console.log(`   Credentials saved: ${CRED_PATH}`);
      if (s.ilink_user_id) {
        console.log(`\n   Add to .env to restrict access:`);
        console.log(`   WECHAT_ALLOWED_FROM=${s.ilink_user_id}\n`);
      }
      return;
    }

    await new Promise<void>((r) => setTimeout(r, 1000));
  }

  console.error("❌ Login timed out. Please retry.");
  process.exit(1);
}

main().catch((err) => { console.error(err); process.exit(1); });
