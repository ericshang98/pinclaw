/**
 * Standalone Pinclaw dev server with mock AI agent.
 *
 * Usage:  npx tsx dev-server.ts
 *
 * This starts:
 *   1. A mock Gateway on port 18789 (echoes back user messages)
 *   2. The Pinclaw WS+HTTP server on port 18790
 *
 * Full voice flow works: speak → BLE → STT → WS → mock agent → TTS
 */

import { createServer } from "node:http";
import { PinclawWsServer } from "./src/ws-server.js";

const PORT = 18790;
const GATEWAY_PORT = 18789;
const AUTH_TOKEN = "pinclaw-dev-token-2026";

// ── Mock Gateway (mimics OpenClaw /v1/chat/completions) ──

const mockGateway = createServer(async (req, res) => {
  if (req.method === "POST" && req.url === "/v1/chat/completions") {
    const chunks: Buffer[] = [];
    for await (const chunk of req) {
      chunks.push(typeof chunk === "string" ? Buffer.from(chunk) : chunk);
    }
    const body = JSON.parse(Buffer.concat(chunks).toString());
    const userMessage = body.messages?.[0]?.content ?? "";

    console.log(`  [mock-agent] User said: "${userMessage}"`);

    // Simple echo reply
    const reply = `我收到了你的消息：「${userMessage}」。这是 Pinclaw 测试模式的自动回复。`;

    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      id: "mock-001",
      choices: [{
        message: { role: "assistant", content: reply },
        finish_reason: "stop",
      }],
    }));
    return;
  }

  res.writeHead(404);
  res.end("Not found");
});

// ── Start both servers ──

mockGateway.listen(GATEWAY_PORT, () => {
  console.log(`  Mock Gateway on :${GATEWAY_PORT}`);

  const server = new PinclawWsServer({
    port: PORT,
    authToken: AUTH_TOKEN,
    gatewayUrl: `http://127.0.0.1:${GATEWAY_PORT}`,
    gatewayToken: "mock",
  });

  server.start().then(() => {
    console.log("");
    console.log("  ╔═══════════════════════════════════════════════════╗");
    console.log("  ║   Pinclaw Dev Server (with mock agent)             ║");
    console.log("  ╠═══════════════════════════════════════════════════╣");
    console.log(`  ║   Pinclaw WS:    ws://IP:${PORT}                  ║`);
    console.log(`  ║   Mock Gateway: http://127.0.0.1:${GATEWAY_PORT}          ║`);
    console.log(`  ║   Auth Token:   ${AUTH_TOKEN}       ║`);
    console.log("  ╠═══════════════════════════════════════════════════╣");
    console.log("  ║   Test commands:                                   ║");
    console.log("  ║     curl POST /push   (proactive push)            ║");
    console.log("  ║     curl GET /pending  (offline messages)          ║");
    console.log("  ║     Voice: speak → BLE → STT → mock agent → TTS   ║");
    console.log("  ╚═══════════════════════════════════════════════════╝");
    console.log("");
  });
});
