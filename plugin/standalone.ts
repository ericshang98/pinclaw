#!/usr/bin/env npx tsx
/**
 * Pinclaw Standalone Launcher
 *
 * Starts the PinclawWsServer directly — no OpenClaw Gateway needed.
 * Connects to Claude Max (or any OpenAI-compatible endpoint) for AI responses.
 *
 * Usage:
 *   npx tsx standalone.ts                          # defaults: Claude Max on 3456
 *   AI_URL=http://localhost:3456 npx tsx standalone.ts
 *   AUTH_TOKEN=my-secret npx tsx standalone.ts
 */

import { PinclawWsServer } from "./src/ws-server.js";
import { networkInterfaces } from "node:os";

// ── Config (env overrides) ──
const WS_PORT = Number(process.env.WS_PORT ?? "18790");
const AUTH_TOKEN = process.env.AUTH_TOKEN ?? "pinclaw-dev-token-2026";
const AI_URL = process.env.AI_URL ?? "https://api.moonshot.ai";
const AI_TOKEN = process.env.AI_TOKEN ?? "";
const FALLBACK_AI_KEY = process.env.FALLBACK_AI_KEY ?? "";

// ── Detect local IP for iPhone config ──
function getLocalIP(): string {
  const interfaces = networkInterfaces();
  for (const name of Object.keys(interfaces)) {
    for (const iface of interfaces[name] ?? []) {
      if (iface.family === "IPv4" && !iface.internal) {
        return iface.address;
      }
    }
  }
  return "127.0.0.1";
}

const localIP = getLocalIP();

// ── Start ──
async function main() {
  console.log("");
  console.log("  ╔═══════════════════════════════════════════════╗");
  console.log("  ║   Pinclaw — Standalone WebSocket Server        ║");
  console.log("  ╚═══════════════════════════════════════════════╝");
  console.log("");
  console.log(`  WebSocket 端口:  ${WS_PORT}`);
  console.log(`  AI 后端:         ${AI_URL}`);
  console.log(`  Auth Token:      ${AUTH_TOKEN}`);
  console.log(`  Fallback AI Key: ${FALLBACK_AI_KEY ? FALLBACK_AI_KEY.slice(0, 8) + "..." : "(not set)"}`);
  console.log("");
  console.log("  ┌──────────────────────────────────────────────┐");
  console.log("  │  iPhone 设置 (Settings 页面):                 │");
  console.log(`  │  WebSocket URL:  ws://${localIP}:${WS_PORT}        │`);
  console.log(`  │  Auth Token:     ${AUTH_TOKEN}  │`);
  console.log(`  │  Device ID:      clip-001                     │`);
  console.log("  └──────────────────────────────────────────────┘");
  console.log("");

  // Verify AI backend is reachable
  try {
    const check = await fetch(`${AI_URL}/v1/models`, {
      signal: AbortSignal.timeout(3000),
    }).catch(() => null);
    if (check && check.ok) {
      console.log(`  ✅ AI 后端可达 (${AI_URL})`);
    } else {
      console.log(`  ⚠️  AI 后端未响应 (${AI_URL}) — 消息会报 502，但服务器仍会启动`);
    }
  } catch {
    console.log(`  ⚠️  AI 后端检查超时 (${AI_URL})`);
  }

  const ac = new AbortController();

  const server = new PinclawWsServer({
    port: WS_PORT,
    authToken: AUTH_TOKEN,
    gatewayUrl: AI_URL,
    gatewayToken: AI_TOKEN,
    fallbackAiKey: FALLBACK_AI_KEY,
    abortSignal: ac.signal,
    log: {
      info: (...args: any[]) => console.log(`  [pinclaw]`, ...args),
      warn: (...args: any[]) => console.warn(`  [pinclaw]`, ...args),
      error: (...args: any[]) => console.error(`  [pinclaw]`, ...args),
    },
  });

  await server.start();
  console.log("");
  console.log("  🟢 服务器已启动，等待 iPhone 连接...");
  console.log("");
  console.log("  ┌──────────────────────────────────────────────┐");
  console.log("  │  Agent 推送 API (主动发消息给设备):           │");
  console.log("  │                                              │");
  console.log(`  │  POST http://${localIP}:${WS_PORT}/push       │`);
  console.log("  │  Body: {token, deviceId, text}               │");
  console.log("  │                                              │");
  console.log(`  │  GET  http://${localIP}:${WS_PORT}/devices     │`);
  console.log("  │  查看已连接设备                               │");
  console.log("  └──────────────────────────────────────────────┘");
  console.log("");
  console.log("  示例 — 推送消息给设备:");
  console.log(`  curl -X POST http://127.0.0.1:${WS_PORT}/push \\`);
  console.log(`    -H "Content-Type: application/json" \\`);
  console.log(`    -d '{"token":"${AUTH_TOKEN}","deviceId":"clip-001","text":"你好，这是 Agent 主动推送的消息"}'`);
  console.log("");
  console.log("     按 Ctrl+C 停止");
  console.log("");

  // Graceful shutdown
  process.on("SIGINT", () => {
    console.log("\n  正在关闭...");
    ac.abort();
    setTimeout(() => process.exit(0), 500);
  });

  process.on("SIGTERM", () => {
    ac.abort();
    setTimeout(() => process.exit(0), 500);
  });

  // Keep alive
  await new Promise(() => {});
}

main().catch((err) => {
  console.error("启动失败:", err);
  process.exit(1);
});
