import "dotenv/config";
import { start, stop } from "./adapters/wechat.js";
process.on("unhandledRejection", (err) => console.error("[fatal] unhandledRejection:", err));
process.on("uncaughtException", (err) => console.error("[fatal] uncaughtException:", err));
process.on("SIGTERM", () => { stop(); process.exit(0); });
process.on("SIGINT", () => { stop(); process.exit(0); });
console.log("wechat-copilot starting...");
start().catch((err) => { console.error("[fatal]", err); process.exit(1); });
