import type { IncomingMessage, ServerResponse } from "node:http";
import { randomUUID } from "node:crypto";
import type { Logger } from "./utils.js";
import { readJsonBody } from "./utils.js";
import type { DeviceManager } from "./device-manager.js";
import type { GatewayRpc } from "./gateway-rpc.js";
import type { CronProxy } from "./cron-proxy.js";
import type { SkillsCrud } from "./skills-crud.js";
import type { VersionChecker } from "./version-check.js";
export interface HttpRouterDeps {
  port: number;
  authToken: string;
  deviceManager: DeviceManager;
  gatewayRpc: GatewayRpc;
  cronProxy: CronProxy;
  skillsCrud: SkillsCrud;
  versionChecker: VersionChecker;
  callAgent: (deviceId: string, text: string) => Promise<{ content?: string; error?: string }>;
  processMessage: (text: string, opts: { deviceId?: string }) => Promise<void>;
  log: Logger;
}

export async function handleHttpRequest(
  req: IncomingMessage,
  res: ServerResponse,
  deps: HttpRouterDeps,
): Promise<void> {
  const {
    port, authToken, deviceManager, gatewayRpc, cronProxy, skillsCrud,
    versionChecker, callAgent, processMessage, log,
  } = deps;

  // CORS preflight
  if (req.method === "OPTIONS") {
    res.writeHead(204, {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type, Authorization",
    });
    res.end();
    return;
  }

  // Health check
  if (req.method === "GET" && req.url === "/health") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      ok: true,
      devices: deviceManager.listConnectedDevices().length,
      gateway: gatewayRpc.isReady,
    }));
    return;
  }

  // AI health check
  if (req.method === "GET" && req.url === "/ai-health") {
    const gatewayOk = gatewayRpc.isReady;

    let model = process.env.AI_MODEL || "";
    if (!model && gatewayOk) {
      try {
        const rpcResult = await gatewayRpc.rpc("config.get", {});
        const cfg = rpcResult?.resolved ?? rpcResult?.config ?? rpcResult?.parsed ?? rpcResult;
        model = cfg?.agents?.defaults?.model?.primary || "";
      } catch {}
    }
    const modelShort = model.includes("/") ? model.split("/").pop()! : model;

    const baseUrl = process.env.AI_BASE_URL || "";
    const hasKey = !!process.env.AI_API_KEY;
    let apiOk = false;
    let latencyMs: number | undefined;
    if (hasKey && baseUrl) {
      const key = process.env.AI_API_KEY || "";
      const start = Date.now();
      try {
        const r = await fetch(`${baseUrl}/v1/models`, {
          headers: { Authorization: `Bearer ${key}` },
          signal: AbortSignal.timeout(8_000),
        });
        apiOk = r.ok;
        latencyMs = Date.now() - start;
      } catch {
        latencyMs = Date.now() - start;
      }
    }

    const isLocal = !process.env.AI_API_KEY && !process.env.AI_BASE_URL;

    res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
    res.end(JSON.stringify({
      ok: gatewayOk && (apiOk || isLocal),
      model: modelShort || model || "unknown",
      gateway: { connected: gatewayOk },
      fallback: isLocal
        ? { ok: gatewayOk, latencyMs: undefined }
        : { ok: apiOk, latencyMs, error: apiOk ? undefined : (hasKey ? "API unreachable" : "No API key") },
    }));
    return;
  }

  // POST /message
  if (req.method === "POST" && req.url === "/message") {
    await handleHttpMessage(req, res, { authToken, callAgent, log });
    return;
  }

  // POST /push
  if (req.method === "POST" && req.url === "/push") {
    await handleHttpPush(req, res, { authToken, deviceManager, log });
    return;
  }

  // POST /notify
  if (req.method === "POST" && req.url === "/notify") {
    await handleNotify(req, res, { authToken, deviceManager, log });
    return;
  }

  // GET /version
  if (req.method === "GET" && req.url === "/version") {
    res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
    res.end(JSON.stringify({
      plugin_version: versionChecker.getPluginVersion(),
      ...versionChecker.versionInfo,
    }));
    return;
  }

  // GET /devices
  if (req.method === "GET" && req.url === "/devices") {
    res.writeHead(200, { "Content-Type": "application/json" });
    const devices = deviceManager.listConnectedDevices();
    res.end(JSON.stringify({ devices, count: devices.length }));
    return;
  }

  // ── Cron job management endpoints ──

  const parsedUrl = new URL(req.url ?? "", `http://localhost:${port}`);
  const cronJobsMatch = parsedUrl.pathname.match(/^\/cron\/jobs(?:\/([^/]+))?(?:\/(.+))?$/);

  if (cronJobsMatch) {
    const token = req.headers.authorization?.replace("Bearer ", "");
    if (token !== authToken) {
      res.writeHead(403, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid token" }));
      return;
    }

    const jobId = cronJobsMatch[1];
    const action = cronJobsMatch[2];

    if (req.method === "GET" && !jobId) { await cronProxy.handleList(res); return; }
    if (req.method === "POST" && !jobId) { await cronProxy.handleCreate(req, res); return; }
    if (req.method === "DELETE" && jobId && !action) { await cronProxy.handleDelete(jobId, res); return; }
    if (req.method === "POST" && jobId && action === "toggle") { await cronProxy.handleToggle(jobId, req, res); return; }
  }

  // ── Skills management endpoints ──

  const skillsMatch = parsedUrl.pathname.match(/^\/skills(?:\/([^/]+))?$/);

  if (skillsMatch) {
    const token = req.headers.authorization?.replace("Bearer ", "");
    if (token !== authToken) {
      res.writeHead(403, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid token" }));
      return;
    }

    const skillName = skillsMatch[1] ? decodeURIComponent(skillsMatch[1]) : undefined;

    if (req.method === "GET" && !skillName) { skillsCrud.handleList(res); return; }
    if (req.method === "GET" && skillName) { skillsCrud.handleGet(skillName, res); return; }
    if (req.method === "POST" && !skillName) { await skillsCrud.handleCreate(req, res); return; }
    if (req.method === "PUT" && skillName) { await skillsCrud.handleUpdate(skillName, req, res); return; }
    if (req.method === "DELETE" && skillName) { skillsCrud.handleDelete(skillName, res); return; }
  }

  // ── Gateway proxy endpoints ──

  const gwMatch = parsedUrl.pathname.match(/^\/gateway\/sessions(?:\/(.+?))?(?:\/(history))?$/);

  if (gwMatch && req.method === "GET") {
    const token = req.headers.authorization?.replace("Bearer ", "");
    if (token !== authToken) {
      res.writeHead(403, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid token" }));
      return;
    }

    const sessionKey = gwMatch[1];
    const action = gwMatch[2];

    if (!sessionKey) {
      try {
        const result = await gatewayRpc.rpc("sessions.list", {});
        res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
        res.end(JSON.stringify(result));
      } catch (err: any) {
        res.writeHead(502, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
        res.end(JSON.stringify({ error: err.message }));
      }
      return;
    }

    if (sessionKey && action === "history") {
      try {
        const result = await gatewayRpc.rpc("chat.history", { sessionKey: decodeURIComponent(sessionKey) });
        res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
        res.end(JSON.stringify(result));
      } catch (err: any) {
        res.writeHead(502, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
        res.end(JSON.stringify({ error: err.message }));
      }
      return;
    }
  }

  // POST /pinclaw/send
  if (req.method === "POST" && parsedUrl.pathname === "/pinclaw/send") {
    let body: any;
    try {
      body = await readJsonBody(req);
    } catch {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid JSON" }));
      return;
    }
    const { message } = body;
    if (!message) {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Missing message" }));
      return;
    }

    const requestId = randomUUID();
    res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
    res.end(JSON.stringify({ requestId }));

    const targetDevice = deviceManager.hasDevice("pinclaw") ? "pinclaw" : undefined;
    processMessage(message, { deviceId: targetDevice });
    return;
  }

  res.writeHead(404, { "Content-Type": "application/json" });
  res.end(JSON.stringify({ error: "Not found" }));
}

// ── Internal HTTP handlers ──

async function handleHttpMessage(
  req: IncomingMessage,
  res: ServerResponse,
  deps: { authToken: string; callAgent: (deviceId: string, text: string) => Promise<{ content?: string; error?: string }>; log: Logger },
): Promise<void> {
  let body: any;
  try {
    body = await readJsonBody(req);
  } catch {
    res.writeHead(400, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: "Invalid JSON" }));
    return;
  }

  const token = body.token ?? req.headers.authorization?.replace("Bearer ", "");
  if (token !== deps.authToken) {
    res.writeHead(403, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: "Invalid token" }));
    return;
  }

  const deviceId: string = body.deviceId;
  const content: string = body.content;
  if (!deviceId || !content) {
    res.writeHead(400, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: "Missing deviceId or content" }));
    return;
  }

  deps.log.info(`HTTP fallback message from ${deviceId}: ${content.slice(0, 60)}...`);

  try {
    const agentResponse = await deps.callAgent(deviceId, content);
    res.writeHead(200, {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*",
    });
    res.end(JSON.stringify({
      content: agentResponse.content ?? "",
      error: agentResponse.error,
    }));
  } catch (err: any) {
    res.writeHead(502, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: err.message ?? String(err) }));
  }
}

async function handleHttpPush(
  req: IncomingMessage,
  res: ServerResponse,
  deps: { authToken: string; deviceManager: DeviceManager; log: Logger },
): Promise<void> {
  let body: any;
  try {
    body = await readJsonBody(req);
  } catch {
    res.writeHead(400, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: "Invalid JSON" }));
    return;
  }

  const token = body.token ?? req.headers.authorization?.replace("Bearer ", "");
  if (token !== deps.authToken) {
    res.writeHead(403, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: "Invalid token" }));
    return;
  }

  const deviceId: string = body.deviceId;
  const text: string = body.text;
  if (!deviceId || !text) {
    res.writeHead(400, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: "Missing deviceId or text" }));
    return;
  }

  deps.log.info(`Proactive push to ${deviceId}: ${text.slice(0, 60)}...`);

  const result = await deps.deviceManager.sendToDevice(deviceId, text);
  res.writeHead(200, {
    "Content-Type": "application/json",
    "Access-Control-Allow-Origin": "*",
  });
  res.end(JSON.stringify({ ok: result.ok, deviceId }));
}

async function handleNotify(
  req: IncomingMessage,
  res: ServerResponse,
  deps: { authToken: string; deviceManager: DeviceManager; log: Logger },
): Promise<void> {
  let body: any;
  try {
    body = await readJsonBody(req);
  } catch {
    res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
    res.end(JSON.stringify({ error: "Invalid JSON" }));
    return;
  }

  const token = body.token ?? req.headers.authorization?.replace("Bearer ", "");
  if (token !== deps.authToken) {
    res.writeHead(403, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
    res.end(JSON.stringify({ error: "Invalid token" }));
    return;
  }

  const message: string = body.message;
  const source: string = body.source ?? "system";
  const deviceId: string = body.deviceId ?? "pinclaw";

  if (!message) {
    res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
    res.end(JSON.stringify({ error: "Missing message" }));
    return;
  }

  const result = await deps.deviceManager.relayToDevice(deviceId, message, source);
  res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
  res.end(JSON.stringify({ ...result, deviceId }));
}
