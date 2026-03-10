/**
 * Pinclaw WS Server — Automated Test Suite
 *
 * Tests the full WebSocket + HTTP protocol without OpenClaw or hardware.
 * Creates a mock Gateway, starts the real PinclawWsServer, and runs all scenarios.
 *
 * Usage:  npx tsx test/pinclaw-server.test.ts
 */

import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { WebSocket } from "ws";
import { PinclawWsServer } from "../src/ws-server.js";

// ── Config ──
const WS_PORT = 19790; // avoid clashing with real server
const GATEWAY_PORT = 19789;
const AUTH_TOKEN = "test-token-2026";
const GATEWAY_TOKEN = "gw-token-2026";
const MOCK_AGENT_REPLY = "我是 OpenClaw，你好！这是一条测试回复。";

let mockSayResponse = "收到";
let mockSyncResponse = MOCK_AGENT_REPLY;

// ── Test infrastructure ──

let passed = 0;
let failed = 0;
const failures: string[] = [];

function assert(condition: boolean, msg: string): void {
  if (condition) {
    passed++;
    console.log(`  ✅ ${msg}`);
  } else {
    failed++;
    failures.push(msg);
    console.log(`  ❌ ${msg}`);
  }
}

function assertEq(actual: any, expected: any, msg: string): void {
  if (actual === expected) {
    passed++;
    console.log(`  ✅ ${msg}`);
  } else {
    failed++;
    failures.push(`${msg} — expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    console.log(`  ❌ ${msg} — expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

function assertIncludes(actual: string, substring: string, msg: string): void {
  if (actual.includes(substring)) {
    passed++;
    console.log(`  ✅ ${msg}`);
  } else {
    failed++;
    failures.push(`${msg} — "${actual}" does not include "${substring}"`);
    console.log(`  ❌ ${msg} — "${actual}" does not include "${substring}"`);
  }
}

// ── Mock Gateway ──
// Mimics OpenClaw's /v1/chat/completions endpoint

let mockGatewayServer: ReturnType<typeof createServer>;
let gatewayRequestCount = 0;
let lastGatewayRequest: any = null;

function startMockGateway(): Promise<void> {
  return new Promise((resolve) => {
    mockGatewayServer = createServer(async (req: IncomingMessage, res: ServerResponse) => {
      if (req.method === "POST" && req.url === "/v1/chat/completions") {
        gatewayRequestCount++;

        // Read body
        const chunks: Buffer[] = [];
        for await (const chunk of req) {
          chunks.push(typeof chunk === "string" ? Buffer.from(chunk) : chunk);
        }
        const body = JSON.parse(Buffer.concat(chunks).toString());

        // Route: Say request (max_tokens <= 100) vs Sync/callDirectAi
        if (body.max_tokens && body.max_tokens <= 100) {
          res.writeHead(200, { "Content-Type": "application/json" });
          res.end(JSON.stringify({
            id: "mock-say",
            choices: [{
              message: { role: "assistant", content: mockSayResponse },
              finish_reason: "stop",
            }],
          }));
          return;
        }

        lastGatewayRequest = body;

        // Return OpenAI-compatible response (Sync / callDirectAi)
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({
          id: "mock-001",
          choices: [{
            message: { role: "assistant", content: mockSyncResponse },
            finish_reason: "stop",
          }],
        }));
        return;
      }

      res.writeHead(404);
      res.end("Not found");
    });

    mockGatewayServer.listen(GATEWAY_PORT, () => {
      console.log(`  Mock Gateway running on port ${GATEWAY_PORT}`);
      resolve();
    });
  });
}

// ── WebSocket helpers ──

function connectWs(): Promise<WebSocket> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://127.0.0.1:${WS_PORT}`);
    ws.on("open", () => resolve(ws));
    ws.on("error", reject);
  });
}

function sendAndReceive(ws: WebSocket, msg: object): Promise<any> {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error("Timeout waiting for response")), 5000);
    const handler = (data: any) => {
      const parsed = JSON.parse(data.toString());
      // Skip ack messages — wait for the actual response
      if (parsed.type === "ack") {
        ws.once("message", handler);
        return;
      }
      clearTimeout(timeout);
      resolve(parsed);
    };
    ws.once("message", handler);
    ws.send(JSON.stringify(msg));
  });
}

function waitForMessage(ws: WebSocket, timeoutMs = 5000): Promise<any> {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error("Timeout waiting for message")), timeoutMs);
    ws.once("message", (data) => {
      clearTimeout(timeout);
      resolve(JSON.parse(data.toString()));
    });
  });
}

function waitForClose(ws: WebSocket, timeoutMs = 5000): Promise<{ code: number; reason: string }> {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error("Timeout waiting for close")), timeoutMs);
    ws.once("close", (code, reason) => {
      clearTimeout(timeout);
      resolve({ code, reason: reason.toString() });
    });
  });
}

async function authenticatedWs(deviceId: string): Promise<WebSocket> {
  const ws = await connectWs();
  const reply = await sendAndReceive(ws, { type: "auth", deviceId, token: AUTH_TOKEN });
  if (reply.type !== "auth_ok") throw new Error(`Auth failed: ${JSON.stringify(reply)}`);
  return ws;
}

// ── HTTP helpers ──

async function httpGet(path: string, bearerToken?: string): Promise<{ status: number; body: any }> {
  const headers: Record<string, string> = {};
  if (bearerToken) headers["Authorization"] = `Bearer ${bearerToken}`;
  const res = await fetch(`http://127.0.0.1:${WS_PORT}${path}`, { headers });
  const body = await res.json();
  return { status: res.status, body };
}

async function httpPost(path: string, data: any, headers?: Record<string, string>): Promise<{ status: number; body: any }> {
  const res = await fetch(`http://127.0.0.1:${WS_PORT}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json", ...headers },
    body: JSON.stringify(data),
  });
  const body = await res.json();
  return { status: res.status, body };
}

// ══════════════════════════════════════════
//  TEST CASES
// ══════════════════════════════════════════

let server: PinclawWsServer;

async function setup() {
  console.log("\n🔧 Setup\n");
  await startMockGateway();

  server = new PinclawWsServer({
    port: WS_PORT,
    authToken: AUTH_TOKEN,
    gatewayUrl: `http://127.0.0.1:${GATEWAY_PORT}`,
    gatewayToken: GATEWAY_TOKEN,
    fallbackAiKey: "test-key",
    fallbackAiUrl: `http://127.0.0.1:${GATEWAY_PORT}/v1/chat/completions`,
    log: {
      info: () => {},   // suppress during tests
      warn: () => {},
      error: () => {},
    },
  });
  await server.start();
  console.log(`  Pinclaw WS Server running on port ${WS_PORT}`);
}

async function teardown() {
  server.stop();
  mockGatewayServer.close();
  // give sockets time to close
  await new Promise((r) => setTimeout(r, 200));
}

// ── 1. WebSocket Auth ──

async function testAuthSuccess() {
  console.log("\n📋 Test: WebSocket auth — success");
  const ws = await connectWs();
  const reply = await sendAndReceive(ws, {
    type: "auth",
    deviceId: "test-001",
    token: AUTH_TOKEN,
  });
  assertEq(reply.type, "auth_ok", "Should reply auth_ok");
  assertEq(reply.deviceId, "test-001", "Should echo deviceId");
  assert(server.isDeviceConnected("test-001"), "Device should be registered");
  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testAuthBadToken() {
  console.log("\n📋 Test: WebSocket auth — bad token");
  const ws = await connectWs();
  const closePromise = waitForClose(ws);
  const reply = await sendAndReceive(ws, {
    type: "auth",
    deviceId: "bad-001",
    token: "wrong-token",
  });
  assertEq(reply.type, "error", "Should reply error");
  assertIncludes(reply.message, "Invalid token", "Error should mention invalid token");
  const closeEvent = await closePromise;
  assertEq(closeEvent.code, 4003, "Should close with 4003");
}

async function testAuthTimeout() {
  console.log("\n📋 Test: WebSocket auth — timeout (10s)");
  console.log("  ⏳ Waiting 11 seconds for auth timeout...");
  const ws = await connectWs();
  const closePromise = waitForClose(ws, 15_000);
  const msgPromise = waitForMessage(ws, 15_000);

  const msg = await msgPromise;
  assertEq(msg.type, "error", "Should send error before close");
  assertIncludes(msg.message, "Auth timeout", "Should mention auth timeout");

  const closeEvent = await closePromise;
  assertEq(closeEvent.code, 4001, "Should close with 4001");
}

// ── 2. WebSocket Text Message ──

async function testTextMessage() {
  console.log("\n📋 Test: WebSocket text message → Say/Sync responses");
  mockSayResponse = "收到";
  mockSyncResponse = MOCK_AGENT_REPLY;
  const ws = await authenticatedWs("text-001");

  // Collect all messages (ack + Say #1 + Sync + possibly Say #2)
  const collectPromise = collectAllMessages(ws, 3000);
  ws.send(JSON.stringify({ type: "text", content: "你好，你是谁？" }));
  const msgs = await collectPromise;

  const nonAck = msgs.filter(m => m.type !== "ack");
  assert(nonAck.length >= 1, "Should receive at least 1 non-ack message");

  // Should have at least one hw=true (Say) message
  const hwMsgs = nonAck.filter(m => m.hw === true);
  assert(hwMsgs.length >= 1, "Should have at least 1 hw=true (Say) message");

  // First non-ack should be Say #1 (hw=true)
  const first = nonAck[0];
  assertEq(first.type, "agent_message", "First should be agent_message");
  assertEq(first.hw, true, "First should be hw=true (Say)");
  assertEq(first.proactive, false, "Should not be proactive");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testTextWithoutAuth() {
  console.log("\n📋 Test: WebSocket text without auth");
  const ws = await connectWs();
  const reply = await sendAndReceive(ws, {
    type: "text",
    content: "should fail",
  });
  assertEq(reply.type, "error", "Should reply error");
  assertIncludes(reply.message, "Not authenticated", "Should mention not authenticated");
  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

// ── 3. WebSocket Ping/Pong ──

async function testPingPong() {
  console.log("\n📋 Test: WebSocket ping/pong");
  const ws = await authenticatedWs("ping-001");
  const reply = await sendAndReceive(ws, { type: "ping" });
  assertEq(reply.type, "pong", "Should reply pong");
  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

// ── 4. Proactive Push ──

async function testProactivePush() {
  console.log("\n📋 Test: Proactive push via sendToDevice");
  const ws = await authenticatedWs("push-001");
  const msgPromise = waitForMessage(ws);

  const result = await server.sendToDevice("push-001", "这是一条主动推送测试");

  assert(result.ok, "sendToDevice should succeed");
  const msg = await msgPromise;
  assertEq(msg.type, "agent_message", "Should be agent_message");
  assertEq(msg.content, "这是一条主动推送测试", "Should contain pushed text");
  assertEq(msg.proactive, true, "Should be marked as proactive");
  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testProactivePushDisconnected() {
  console.log("\n📋 Test: Proactive push to disconnected device — queued");
  const result = await server.sendToDevice("nonexistent-999", "should be queued");
  assertEq(result.ok, true, "Should succeed (queued)");
  assertEq(result.queued, true, "Should be marked as queued");
}

// ── 4b. HTTP Push API ──

async function testHttpPush() {
  console.log("\n📋 Test: HTTP POST /push — proactive push via API");
  const ws = await authenticatedWs("httppush-001");
  const msgPromise = waitForMessage(ws);

  const { status, body } = await httpPost("/push", {
    token: AUTH_TOKEN,
    deviceId: "httppush-001",
    text: "Agent 主动推送测试",
  });

  assertEq(status, 200, "Should return 200");
  assertEq(body.ok, true, "Should be ok");
  const msg = await msgPromise;
  assertEq(msg.type, "agent_message", "Should be agent_message");
  assertEq(msg.content, "Agent 主动推送测试", "Content should match");
  assertEq(msg.proactive, true, "Should be proactive");
  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testHttpPushNoDevice() {
  console.log("\n📋 Test: HTTP POST /push — device not connected (queued)");
  const { status, body } = await httpPost("/push", {
    token: AUTH_TOKEN,
    deviceId: "nobody-999",
    text: "should be queued",
  });
  assertEq(status, 200, "Should return 200 (queued)");
  assertEq(body.ok, true, "Should be ok");
  assertEq(body.queued, true, "Should be marked as queued");
}

async function testHttpDevicesList() {
  console.log("\n📋 Test: GET /devices — list connected devices");
  const ws = await authenticatedWs("devlist-001");
  const { status, body } = await httpGet("/devices");
  assertEq(status, 200, "Should return 200");
  assert(body.devices.includes("devlist-001"), "Should list connected device");
  assert(typeof body.count === "number", "Should have count");
  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

// ── 5. Device Management ──

async function testDeviceReplacement() {
  console.log("\n📋 Test: Device connection replacement");
  const ws1 = await authenticatedWs("replace-001");
  const ws1ClosePromise = waitForClose(ws1);

  const ws2 = await authenticatedWs("replace-001");

  // Old connection should be closed
  const closeEvent = await ws1ClosePromise;
  assertEq(closeEvent.code, 1000, "Old connection closed with 1000");
  assertIncludes(closeEvent.reason, "Replaced", "Close reason should mention replacement");

  // New connection should work
  const reply = await sendAndReceive(ws2, { type: "ping" });
  assertEq(reply.type, "pong", "New connection should work");

  ws2.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testListDevices() {
  console.log("\n📋 Test: List connected devices");
  const ws1 = await authenticatedWs("list-001");
  const ws2 = await authenticatedWs("list-002");
  const ws3 = await authenticatedWs("list-003");

  const devices = server.listConnectedDevices();
  assert(devices.includes("list-001"), "Should contain list-001");
  assert(devices.includes("list-002"), "Should contain list-002");
  assert(devices.includes("list-003"), "Should contain list-003");
  assertEq(devices.length >= 3, true, "Should have at least 3 devices");

  ws1.close();
  ws2.close();
  ws3.close();
  await new Promise((r) => setTimeout(r, 200));
}

async function testDeviceDisconnect() {
  console.log("\n📋 Test: Device disconnect cleanup");
  const ws = await authenticatedWs("disc-001");
  assert(server.isDeviceConnected("disc-001"), "Should be connected");

  ws.close();
  await new Promise((r) => setTimeout(r, 200));
  assert(!server.isDeviceConnected("disc-001"), "Should be disconnected after close");
}

// ── 6. HTTP Endpoints ──

async function testHealthCheck() {
  console.log("\n📋 Test: HTTP GET /health");
  const { status, body } = await httpGet("/health");
  assertEq(status, 200, "Should return 200");
  assertEq(body.ok, true, "Should be ok");
  assert(typeof body.devices === "number", "Should report device count");
}

async function testHttpMessage() {
  console.log("\n📋 Test: HTTP POST /message — success");
  const prevCount = gatewayRequestCount;

  const { status, body } = await httpPost("/message", {
    token: AUTH_TOKEN,
    deviceId: "http-001",
    content: "通过 HTTP 发送的消息",
  });

  assertEq(status, 200, "Should return 200");
  assertEq(body.content, MOCK_AGENT_REPLY, "Should return agent reply");
  assertEq(gatewayRequestCount, prevCount + 1, "Should have called gateway");
}

async function testHttpMessageBadToken() {
  console.log("\n📋 Test: HTTP POST /message — bad token");
  const { status, body } = await httpPost("/message", {
    token: "wrong-token",
    deviceId: "http-002",
    content: "should fail",
  });
  assertEq(status, 403, "Should return 403");
  assertIncludes(body.error, "Invalid token", "Should mention invalid token");
}

async function testHttpMessageMissingFields() {
  console.log("\n📋 Test: HTTP POST /message — missing fields");
  const { status, body } = await httpPost("/message", {
    token: AUTH_TOKEN,
    // missing deviceId and content
  });
  assertEq(status, 400, "Should return 400");
  assertIncludes(body.error, "Missing", "Should mention missing fields");
}

async function testHttpMessageBearerAuth() {
  console.log("\n📋 Test: HTTP POST /message — Bearer auth header");
  const { status, body } = await httpPost("/message", {
    deviceId: "http-003",
    content: "使用 Bearer 认证",
  }, {
    Authorization: `Bearer ${AUTH_TOKEN}`,
  });
  assertEq(status, 200, "Should return 200 with Bearer auth");
  assertEq(body.content, MOCK_AGENT_REPLY, "Should return agent reply");
}

async function testHttp404() {
  console.log("\n📋 Test: HTTP GET /nonexistent — 404");
  const { status, body } = await httpGet("/nonexistent");
  assertEq(status, 404, "Should return 404");
  assertIncludes(body.error, "Not found", "Should say not found");
}

async function testHttpCors() {
  console.log("\n📋 Test: HTTP OPTIONS — CORS preflight");
  const res = await fetch(`http://127.0.0.1:${WS_PORT}/message`, {
    method: "OPTIONS",
  });
  assertEq(res.status, 204, "Should return 204");
  const allowOrigin = res.headers.get("Access-Control-Allow-Origin");
  assertEq(allowOrigin, "*", "Should allow all origins");
}

// ── 7. Invalid JSON ──

async function testInvalidJson() {
  console.log("\n📋 Test: WebSocket invalid JSON");
  const ws = await connectWs();
  const reply = await new Promise<any>((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error("Timeout")), 3000);
    ws.once("message", (data) => {
      clearTimeout(timeout);
      resolve(JSON.parse(data.toString()));
    });
    ws.send("this is not json{{{");
  });
  assertEq(reply.type, "error", "Should reply error");
  assertIncludes(reply.message, "Invalid JSON", "Should mention invalid JSON");
  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

// ── 8. Multiple concurrent messages ──

async function testConcurrentMessages() {
  console.log("\n📋 Test: Multiple concurrent text messages");
  const ws = await authenticatedWs("concurrent-001");

  // Send 3 messages sequentially, collect all messages for each
  let successCount = 0;
  for (let i = 0; i < 3; i++) {
    const collectPromise = collectAllMessages(ws, 4000);
    ws.send(JSON.stringify({ type: "text", content: `顺序消息 ${i}` }));
    const msgs = await collectPromise;
    const agentMsgs = msgs.filter(m => m.type === "agent_message");
    if (agentMsgs.length >= 1) successCount++;
  }
  assertEq(successCount, 3, "All 3 messages should get at least 1 agent response");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

// ── 9. Pending message queue ──

async function testPendingBasic() {
  console.log("\n📋 Test: Push to offline device → GET /pending returns messages");
  const deviceId = "pending-001";
  // Push 3 messages while device is offline
  for (let i = 0; i < 3; i++) {
    await httpPost("/push", { token: AUTH_TOKEN, deviceId, text: `离线消息 ${i}` });
  }

  // GET /pending should return all 3
  const { status, body } = await httpGet(`/pending?deviceId=${deviceId}`, AUTH_TOKEN);
  assertEq(status, 200, "Should return 200");
  assertEq(body.messages.length, 3, "Should have 3 pending messages");
  assertEq(body.messages[0].text, "离线消息 0", "First message text matches");
  assertEq(body.messages[2].text, "离线消息 2", "Last message text matches");
  assert(typeof body.messages[0].id === "string", "Messages should have id");

  // Second GET should return empty (messages consumed)
  const { body: body2 } = await httpGet(`/pending?deviceId=${deviceId}`, AUTH_TOKEN);
  assertEq(body2.messages.length, 0, "Queue should be empty after retrieval");
}

async function testPendingOverflow() {
  console.log("\n📋 Test: Push 21 messages → oldest dropped, return 20");
  const deviceId = "pending-overflow";
  for (let i = 0; i < 21; i++) {
    await httpPost("/push", { token: AUTH_TOKEN, deviceId, text: `msg-${i}` });
  }

  const { body } = await httpGet(`/pending?deviceId=${deviceId}`, AUTH_TOKEN);
  assertEq(body.messages.length, 20, "Should cap at 20 messages");
  // Oldest (msg-0) should be dropped, first message should be msg-1
  assertEq(body.messages[0].text, "msg-1", "Oldest message should be dropped (FIFO)");
  assertEq(body.messages[19].text, "msg-20", "Newest message should be last");
}

async function testPendingWsReconnectDelivery() {
  console.log("\n📋 Test: WS reconnect → pending messages auto-delivered");
  const deviceId = "pending-reconnect";

  // Push while offline
  await httpPost("/push", { token: AUTH_TOKEN, deviceId, text: "你有一个提醒" });
  await httpPost("/push", { token: AUTH_TOKEN, deviceId, text: "第二条提醒" });

  // Connect via WS — should receive pending messages after auth
  const ws = await connectWs();
  const messages: any[] = [];

  // Collect messages: auth_ok + 2 pending
  const collectPromise = new Promise<void>((resolve) => {
    let count = 0;
    ws.on("message", (data) => {
      const msg = JSON.parse(data.toString());
      messages.push(msg);
      count++;
      if (count >= 3) resolve(); // auth_ok + 2 agent_messages
    });
    setTimeout(resolve, 3000); // timeout fallback
  });

  ws.send(JSON.stringify({ type: "auth", deviceId, token: AUTH_TOKEN }));
  await collectPromise;

  const authMsg = messages.find(m => m.type === "auth_ok");
  assert(authMsg !== undefined, "Should receive auth_ok");
  const agentMsgs = messages.filter(m => m.type === "agent_message");
  assertEq(agentMsgs.length, 2, "Should receive 2 pending messages via WS");
  assertEq(agentMsgs[0].content, "你有一个提醒", "First pending message matches");
  assertEq(agentMsgs[1].content, "第二条提醒", "Second pending message matches");
  assertEq(agentMsgs[0].proactive, true, "Pending messages should be proactive");

  // Queue should be empty now
  const { body } = await httpGet(`/pending?deviceId=${deviceId}`, AUTH_TOKEN);
  assertEq(body.messages.length, 0, "Queue empty after WS delivery");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testPendingAuthRequired() {
  console.log("\n📋 Test: GET /pending without auth → 403");
  const { status, body } = await httpGet("/pending?deviceId=test");
  assertEq(status, 403, "Should return 403 without auth");
  assertIncludes(body.error, "Invalid token", "Should mention invalid token");
}

async function testPendingMissingDeviceId() {
  console.log("\n📋 Test: GET /pending without deviceId → 400");
  const { status, body } = await httpGet("/pending", AUTH_TOKEN);
  assertEq(status, 400, "Should return 400 without deviceId");
  assertIncludes(body.error, "Missing", "Should mention missing param");
}

// ── 10. Device Tools Protocol ──

async function testDeviceToolsRegister() {
  console.log("\n📋 Test: device_tools_register → server records tools");
  const ws = await authenticatedWs("devtools-001");

  // Register tools
  ws.send(JSON.stringify({
    type: "device_tools_register",
    tools: [
      { name: "calendar_today", description: "Get today events", parameters: [] },
      { name: "contacts_search", description: "Search contacts", parameters: [{ name: "query", type: "string" }] },
    ],
  }));
  await new Promise((r) => setTimeout(r, 200));

  const tools = server.getDeviceTools("devtools-001");
  assertEq(tools.length, 2, "Should have 2 registered tools");
  assertEq(tools[0].name, "calendar_today", "First tool name matches");
  assertEq(tools[1].name, "contacts_search", "Second tool name matches");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testDeviceToolsDisconnectCleanup() {
  console.log("\n📋 Test: Device disconnect → tools list cleared");
  const ws = await authenticatedWs("devtools-cleanup");

  ws.send(JSON.stringify({
    type: "device_tools_register",
    tools: [{ name: "timer_set", description: "Set timer", parameters: [] }],
  }));
  await new Promise((r) => setTimeout(r, 200));
  assertEq(server.getDeviceTools("devtools-cleanup").length, 1, "Should have 1 tool before disconnect");

  ws.close();
  await new Promise((r) => setTimeout(r, 300));
  assertEq(server.getDeviceTools("devtools-cleanup").length, 0, "Should have 0 tools after disconnect");
}

async function testDeviceToolCall() {
  console.log("\n📋 Test: tool_call → device receives → tool_result → server resolves");
  const ws = await authenticatedWs("devtools-call");

  // Register a tool
  ws.send(JSON.stringify({
    type: "device_tools_register",
    tools: [{ name: "calendar_today", description: "Get today events", parameters: [] }],
  }));
  await new Promise((r) => setTimeout(r, 200));

  // Listen for tool_call on the device side and auto-respond
  ws.on("message", (raw) => {
    const msg = JSON.parse(raw.toString());
    if (msg.type === "tool_call") {
      ws.send(JSON.stringify({
        type: "tool_result",
        callId: msg.callId,
        success: true,
        result: "10:00 - 11:00: Team standup\n14:00 - 15:00: Design review",
      }));
    }
  });

  // Server calls the device tool
  const result = await server.callDeviceTool("devtools-call", "calendar_today", {});
  assertEq(result.success, true, "Tool call should succeed");
  assertIncludes(result.result ?? "", "Team standup", "Result should contain event data");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testDeviceToolCallTimeout() {
  console.log("\n📋 Test: tool_call timeout → error returned");
  const ws = await authenticatedWs("devtools-timeout");

  ws.send(JSON.stringify({
    type: "device_tools_register",
    tools: [{ name: "slow_tool", description: "Never responds", parameters: [] }],
  }));
  await new Promise((r) => setTimeout(r, 200));

  // Don't respond to tool_call — should timeout
  try {
    await server.callDeviceTool("devtools-timeout", "slow_tool", {});
    assert(false, "Should have thrown timeout error");
  } catch (err: any) {
    assertIncludes(err.message, "timeout", "Error should mention timeout");
  }

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testDeviceToolCallUnregistered() {
  console.log("\n📋 Test: Call unregistered tool → error");
  const ws = await authenticatedWs("devtools-unreg");

  ws.send(JSON.stringify({
    type: "device_tools_register",
    tools: [{ name: "calendar_today", description: "Get today", parameters: [] }],
  }));
  await new Promise((r) => setTimeout(r, 200));

  try {
    await server.callDeviceTool("devtools-unreg", "nonexistent_tool", {});
    assert(false, "Should have thrown error for unregistered tool");
  } catch (err: any) {
    assertIncludes(err.message, "not registered", "Error should mention not registered");
  }

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testDeviceToolCallOffline() {
  console.log("\n📋 Test: Call tool on offline device → error");
  try {
    await server.callDeviceTool("nonexistent-device", "calendar_today", {});
    assert(false, "Should have thrown error for offline device");
  } catch (err: any) {
    assertIncludes(err.message, "not connected", "Error should mention not connected");
  }
}

async function testDeviceToolCallError() {
  console.log("\n📋 Test: tool_call → device returns error → server gets error");
  const ws = await authenticatedWs("devtools-err");

  ws.send(JSON.stringify({
    type: "device_tools_register",
    tools: [{ name: "contacts_search", description: "Search", parameters: [{ name: "query", type: "string" }] }],
  }));
  await new Promise((r) => setTimeout(r, 200));

  ws.on("message", (raw) => {
    const msg = JSON.parse(raw.toString());
    if (msg.type === "tool_call") {
      ws.send(JSON.stringify({
        type: "tool_result",
        callId: msg.callId,
        success: false,
        error: "Contacts permission denied",
      }));
    }
  });

  const result = await server.callDeviceTool("devtools-err", "contacts_search", { query: "test" });
  assertEq(result.success, false, "Tool call should report failure");
  assertEq(result.error, "Contacts permission denied", "Should return error message");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testDeviceToolsReRegister() {
  console.log("\n📋 Test: Re-register tools → replaces previous list");
  const ws = await authenticatedWs("devtools-rereg");

  ws.send(JSON.stringify({
    type: "device_tools_register",
    tools: [{ name: "tool_a", description: "A", parameters: [] }],
  }));
  await new Promise((r) => setTimeout(r, 200));
  assertEq(server.getDeviceTools("devtools-rereg").length, 1, "Should have 1 tool initially");

  ws.send(JSON.stringify({
    type: "device_tools_register",
    tools: [
      { name: "tool_b", description: "B", parameters: [] },
      { name: "tool_c", description: "C", parameters: [] },
    ],
  }));
  await new Promise((r) => setTimeout(r, 200));
  assertEq(server.getDeviceTools("devtools-rereg").length, 2, "Should have 2 tools after re-register");
  assertEq(server.getDeviceTools("devtools-rereg")[0].name, "tool_b", "First tool should be tool_b");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testDeviceToolsRegisterWithoutAuth() {
  console.log("\n📋 Test: device_tools_register without auth → error");
  const ws = await connectWs();
  const reply = await sendAndReceive(ws, {
    type: "device_tools_register",
    tools: [{ name: "test", description: "test", parameters: [] }],
  });
  assertEq(reply.type, "error", "Should reply error");
  assertIncludes(reply.message, "Not authenticated", "Should mention not authenticated");
  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

// ── 11. Say / Sync Architecture ──

function collectAllMessages(ws: WebSocket, timeoutMs = 3000): Promise<any[]> {
  return new Promise((resolve) => {
    const msgs: any[] = [];
    const handler = (data: any) => msgs.push(JSON.parse(data.toString()));
    ws.on("message", handler);
    setTimeout(() => {
      ws.removeListener("message", handler);
      resolve(msgs);
    }, timeoutMs);
  });
}

async function testSaySyncParallel() {
  console.log("\n📋 Test: Say/Sync — parallel execution, Say first");
  mockSayResponse = "帮你查一下";
  // Must be >80 chars AND contain list/structure to avoid suppression
  mockSyncResponse = "查询天气的方法有很多种，以下是几种常用方式：\n\n1. 使用天气应用查看实时天气\n2. 通过浏览器搜索当地天气预报\n3. 关注气象局官方微信公众号获取最新预报信息";
  const ws = await authenticatedWs("saysync-parallel");

  const collectPromise = collectAllMessages(ws, 8000);
  ws.send(JSON.stringify({ type: "text", content: "帮我查一下天气" }));
  const msgs = await collectPromise;

  const nonAck = msgs.filter(m => m.type !== "ack");
  assert(nonAck.length >= 2, "Should receive at least 2 non-ack messages (Say #1 + Sync)");

  // First should be Say (hw=true)
  const first = nonAck[0];
  assertEq(first.hw, true, "First message should be hw=true (Say #1)");
  assertEq(first.mode, "voice", "Say should be voice mode");

  // Should have a Sync message (hw=false)
  const syncMsgs = nonAck.filter(m => m.hw === false);
  assert(syncMsgs.length >= 1, "Should have at least 1 Sync message (hw=false)");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testSaySyncSuppression() {
  console.log("\n📋 Test: Say/Sync — short Sync suppressed by Say");
  mockSayResponse = "你好";
  mockSyncResponse = "你好啊"; // Short (<80 chars), no lists/errors → should be suppressed
  const ws = await authenticatedWs("saysync-suppress");

  const collectPromise = collectAllMessages(ws, 6000);
  ws.send(JSON.stringify({ type: "text", content: "你好" }));
  const msgs = await collectPromise;

  const nonAck = msgs.filter(m => m.type !== "ack");
  // Say #1 should be sent, Sync should be suppressed
  assert(nonAck.length >= 1, "Should receive at least 1 message (Say)");

  const hwMsgs = nonAck.filter(m => m.hw === true);
  assert(hwMsgs.length >= 1, "Should have Say message (hw=true)");

  // Sync should NOT appear (suppressed)
  const syncMsgs = nonAck.filter(m => m.type === "agent_message" && m.hw === false);
  assertEq(syncMsgs.length, 0, "Short Sync should be suppressed when Say covers it");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testSaySyncLongAnswer() {
  console.log("\n📋 Test: Say/Sync — long answer: Say #1 + Sync + Say #2");
  mockSayResponse = "帮你理一下";
  mockSyncResponse = "成立艺人公司需要以下7个关键步骤：1. 注册公司实体 2. 获取营业执照 3. 签约艺人 4. 建立管理团队 5. 制定发展战略 6. 搭建宣传渠道 7. 确保法律合规。每个步骤都有具体的操作流程和注意事项。";
  const ws = await authenticatedWs("saysync-long");

  const collectPromise = collectAllMessages(ws, 8000);
  ws.send(JSON.stringify({ type: "text", content: "怎么成立一个艺人公司" }));
  const msgs = await collectPromise;

  const nonAck = msgs.filter(m => m.type !== "ack");
  assert(nonAck.length >= 2, "Should receive at least 2 messages for long answer");

  // Should have Say messages (hw=true)
  const hwMsgs = nonAck.filter(m => m.hw === true);
  assert(hwMsgs.length >= 1, "Should have at least 1 Say message");

  // Should have Sync display (hw=false)
  const syncMsgs = nonAck.filter(m => m.hw === false);
  assert(syncMsgs.length >= 1, "Should have Sync display message for long content");
  assert(syncMsgs[0].content.length > 80, "Sync content should be the full detailed answer");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testSayFailure() {
  console.log("\n📋 Test: Say failure — falls back to Sync only");
  // Create a server without fallback AI key → Say returns empty → only Sync sent
  const noAiServer = new PinclawWsServer({
    port: WS_PORT + 1,
    authToken: AUTH_TOKEN,
    gatewayUrl: `http://127.0.0.1:${GATEWAY_PORT}`,
    gatewayToken: GATEWAY_TOKEN,
    fallbackAiKey: "", // No AI key → Say will return empty
    fallbackAiUrl: `http://127.0.0.1:${GATEWAY_PORT}/v1/chat/completions`,
    log: { info: () => {}, warn: () => {}, error: () => {} },
  });
  await noAiServer.start();

  const ws = await new Promise<WebSocket>((resolve, reject) => {
    const w = new WebSocket(`ws://127.0.0.1:${WS_PORT + 1}`);
    w.on("open", () => resolve(w));
    w.on("error", reject);
  });
  const authReply = await sendAndReceive(ws, { type: "auth", deviceId: "sayfail-001", token: AUTH_TOKEN });
  assertEq(authReply.type, "auth_ok", "Should auth ok on fallback server");

  const collectPromise = collectAllMessages(ws, 3000);
  ws.send(JSON.stringify({ type: "text", content: "测试" }));
  const msgs = await collectPromise;

  const nonAck = msgs.filter(m => m.type !== "ack");
  // Should still get Sync response (via callDirectAi which also has empty key → error)
  // Actually without fallback key, callDirectAi will fail too → error message
  // But callAgent tries Gateway RPC first → which isn't connected → then callDirectAi → fails
  // So we expect an error or hw=true error message
  assert(nonAck.length >= 1, "Should receive at least 1 message even when Say fails");

  ws.close();
  noAiServer.stop();
  await new Promise((r) => setTimeout(r, 200));
}

// ── 12. requestId + userText deduplication ──

async function testRequestIdPresent() {
  console.log("\n📋 Test: agent_message includes requestId + userText");
  mockSayResponse = "好的";
  mockSyncResponse = "这是一段较长的同步回复内容，用来测试 requestId 是否出现在 Sync 消息中。这段话需要超过八十个字符才能避免被压缩掉，所以我要多写一些内容来确保它足够长。";
  const ws = await authenticatedWs("rid-present");

  const collectPromise = collectAllMessages(ws, 8000);
  ws.send(JSON.stringify({ type: "text", content: "测试requestId" }));
  const msgs = await collectPromise;

  const agentMsgs = msgs.filter(m => m.type === "agent_message");
  assert(agentMsgs.length >= 1, "Should have at least 1 agent_message");

  // All agent_messages should have requestId
  for (const m of agentMsgs) {
    assert(typeof m.requestId === "string" && m.requestId.length > 0, `agent_message should have requestId (hw=${m.hw})`);
    assertEq(m.userText, "测试requestId", `agent_message should have userText (hw=${m.hw})`);
  }

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testRequestIdShared() {
  console.log("\n📋 Test: Same requestId across Say + Sync");
  mockSayResponse = "帮你查一下";
  mockSyncResponse = "这是一段较长的同步回复内容，包含详细信息。需要确保超过八十个字符来避免被抑制，同时包含足够的结构化内容，比如列表和步骤说明。\n\n1. 第一步\n2. 第二步\n3. 第三步";
  const ws = await authenticatedWs("rid-shared");

  const collectPromise = collectAllMessages(ws, 8000);
  ws.send(JSON.stringify({ type: "text", content: "详细查询" }));
  const msgs = await collectPromise;

  const agentMsgs = msgs.filter(m => m.type === "agent_message");
  const sayMsgs = agentMsgs.filter(m => m.hw === true);
  const syncMsgs = agentMsgs.filter(m => m.hw === false);

  assert(sayMsgs.length >= 1, "Should have at least 1 Say message");
  assert(syncMsgs.length >= 1, "Should have at least 1 Sync message");

  // All messages should share the same requestId
  const rids = new Set(agentMsgs.map(m => m.requestId));
  assertEq(rids.size, 1, "All agent_messages should share the same requestId");

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

async function testCallDirectAiPlainText() {
  console.log("\n📋 Test: callDirectAi returns plain text (no XML)");
  // The mock gateway returns mockSyncResponse as-is for non-Say requests.
  // Set it to plain text to verify the prompt change works.
  mockSyncResponse = "这是一个纯文本回复，没有XML标签。";
  mockSayResponse = "收到";
  const ws = await authenticatedWs("plaintext-001");

  const collectPromise = collectAllMessages(ws, 6000);
  ws.send(JSON.stringify({ type: "text", content: "测试纯文本" }));
  const msgs = await collectPromise;

  const agentMsgs = msgs.filter(m => m.type === "agent_message");
  assert(agentMsgs.length >= 1, "Should have at least 1 agent_message");

  // Verify no XML tags in content
  for (const m of agentMsgs) {
    const hasXml = /<mode>|<voice>|<sound>|<display>/.test(m.content);
    assert(!hasXml, `agent_message content should not contain XML tags: "${m.content.slice(0, 60)}"`);
  }

  ws.close();
  await new Promise((r) => setTimeout(r, 100));
}

// ══════════════════════════════════════════
//  MAIN
// ══════════════════════════════════════════

async function main() {
  console.log("╔══════════════════════════════════════════════╗");
  console.log("║   Pinclaw WS Server — Automated Test Suite   ║");
  console.log("╚══════════════════════════════════════════════╝");

  try {
    await setup();

    // Fast tests first
    await testAuthSuccess();
    await testAuthBadToken();
    await testTextMessage();
    await testTextWithoutAuth();
    await testPingPong();
    await testProactivePush();
    await testProactivePushDisconnected();
    await testHttpPush();
    await testHttpPushNoDevice();
    await testHttpDevicesList();
    await testDeviceReplacement();
    await testListDevices();
    await testDeviceDisconnect();
    await testHealthCheck();
    await testHttpMessage();
    await testHttpMessageBadToken();
    await testHttpMessageMissingFields();
    await testHttpMessageBearerAuth();
    await testHttp404();
    await testHttpCors();
    await testInvalidJson();
    await testConcurrentMessages();

    // Say/Sync architecture tests
    await testSaySyncParallel();
    await testSaySyncSuppression();
    await testSaySyncLongAnswer();
    await testSayFailure();

    // requestId + deduplication tests
    await testRequestIdPresent();
    await testRequestIdShared();
    await testCallDirectAiPlainText();

    // Pending queue tests
    await testPendingBasic();
    await testPendingOverflow();
    await testPendingWsReconnectDelivery();
    await testPendingAuthRequired();
    await testPendingMissingDeviceId();

    // Device tools tests
    await testDeviceToolsRegister();
    await testDeviceToolsDisconnectCleanup();
    await testDeviceToolCall();
    await testDeviceToolCallTimeout();
    await testDeviceToolCallUnregistered();
    await testDeviceToolCallOffline();
    await testDeviceToolCallError();
    await testDeviceToolsReRegister();
    await testDeviceToolsRegisterWithoutAuth();

    // Slow test last (11 second wait)
    await testAuthTimeout();

  } catch (err) {
    console.error("\n💥 FATAL ERROR:", err);
    failed++;
  } finally {
    await teardown();
  }

  // ── Summary ──
  console.log("\n══════════════════════════════════════");
  console.log(`  Results: ${passed} passed, ${failed} failed`);
  if (failures.length > 0) {
    console.log("\n  Failures:");
    for (const f of failures) {
      console.log(`    ❌ ${f}`);
    }
  }
  console.log("══════════════════════════════════════\n");

  process.exit(failed > 0 ? 1 : 0);
}

main();
