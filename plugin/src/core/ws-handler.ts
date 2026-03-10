import { WebSocket } from "ws";
import { randomUUID } from "node:crypto";
import { writeFileSync, mkdirSync, existsSync } from "node:fs";
import { join, extname } from "node:path";
import { tmpdir } from "node:os";
import type {
  WsInboundMessage,
  DeviceSkillManifest,
  WsPlayRequestMessage,
  WsMediaMessage,
  MediaAttachment,
} from "../types.js";
import type { Logger } from "./utils.js";
import { sendWs } from "./utils.js";
import type { DeviceManager } from "./device-manager.js";
import type { VersionChecker } from "./version-check.js";
import type { InteractiveAI } from "../interactive-ai.js";
import type { GatewayRpc } from "./gateway-rpc.js";

export interface WsHandlerDeps {
  authToken: string;
  deviceManager: DeviceManager;
  versionChecker: VersionChecker;
  updateRequired: boolean;
  interactiveAI: InteractiveAI | null;
  gatewayRpc: GatewayRpc;
  processMessage: (text: string, opts: { deviceId?: string; mediaPaths?: string[] }) => Promise<void>;
  log: Logger;
}

export function handleWsConnection(ws: WebSocket, deps: WsHandlerDeps): void {
  const { authToken, deviceManager, versionChecker, updateRequired, interactiveAI, gatewayRpc, processMessage, log } = deps;

  let deviceId: string | null = null;
  let authenticated = false;

  const authTimer = setTimeout(() => {
    if (!authenticated) {
      sendWs(ws, { type: "error", message: "Auth timeout" });
      ws.close(4001, "Auth timeout");
    }
  }, 10_000);

  ws.on("message", async (raw, isBinary) => {
    // Binary frames not used (STT handled directly by iPhone)
    if (isBinary) return;

    let msg: WsInboundMessage;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      sendWs(ws, { type: "error", message: "Invalid JSON" });
      return;
    }

    if (msg.type === "auth") {
      if (msg.token !== authToken) {
        sendWs(ws, { type: "error", message: "Invalid token" });
        ws.close(4003, "Invalid token");
        return;
      }
      deviceId = msg.deviceId;
      authenticated = true;
      clearTimeout(authTimer);

      // Reject new connections when a required plugin update is pending
      if (updateRequired) {
        versionChecker.notifyDevice(ws);
        sendWs(ws, { type: "error", message: "Plugin update required. The server is shutting down for a mandatory update." });
        ws.close(4010, "Plugin update required");
        return;
      }

      deviceManager.addDevice(deviceId, ws);

      // Build services config for direct iPhone API calls
      const services: Record<string, any> = {};
      const iaKey = process.env.INTERACTIVE_AI_KEY || process.env.AI_API_KEY || "";
      const iaBase = process.env.INTERACTIVE_AI_BASE_URL || process.env.AI_BASE_URL || "";
      const iaModel = process.env.INTERACTIVE_AI_MODEL || process.env.AI_LIGHT_MODEL || "kimi-k2.5";
      if (iaKey && iaBase) {
        services.interactiveAI = { apiKey: iaKey, baseUrl: iaBase, model: iaModel };
      }
      sendWs(ws, {
        type: "auth_ok",
        deviceId,
        ...(Object.keys(services).length > 0 ? { services } : {}),
      });
      log.info(`Device authenticated: ${deviceId} (services: ${Object.keys(services).join(", ") || "none"})`);

      // Notify device of available updates
      versionChecker.notifyDevice(ws);
      return;
    }

    if (msg.type === "ping") {
      sendWs(ws, { type: "pong" });
      return;
    }

    if (msg.type === "device_tools_register") {
      if (!authenticated || !deviceId) {
        sendWs(ws, { type: "error", message: "Not authenticated" });
        return;
      }

      let skills: DeviceSkillManifest[];
      if (msg.skills && msg.skills.length > 0) {
        skills = msg.skills;
      } else if (msg.tools && msg.tools.length > 0) {
        skills = [{
          id: "device.legacy",
          name: "Legacy Tools",
          enabled: true,
          permission: "authorized",
          tools: msg.tools,
        }];
      } else {
        skills = [];
      }

      deviceManager.registerSkills(deviceId, skills);
      return;
    }

    if (msg.type === "context_update") {
      if (!authenticated || !deviceId) {
        sendWs(ws, { type: "error", message: "Not authenticated" });
        return;
      }
      deviceManager.updateContextHints(deviceId, msg.hints);
      return;
    }

    if (msg.type === "tool_result") {
      deviceManager.resolvePendingToolCall(msg.callId, {
        success: msg.success,
        result: msg.result,
        error: msg.error,
      });
      return;
    }

    if (msg.type === "play_request") {
      if (!authenticated || !deviceId) {
        sendWs(ws, { type: "error", message: "Not authenticated" });
        return;
      }
      if (!interactiveAI) {
        sendWs(ws, { type: "interactive_error", message: "Interactive AI not configured", requestId: randomUUID() });
        return;
      }
      const requestId = randomUUID();
      try {
        const playMsg = msg as WsPlayRequestMessage;
        const content = await interactiveAI.generate(playMsg.recentEntries, playMsg.currentTime);
        sendWs(ws, { type: "interactive_response", content, requestId });
      } catch (err: any) {
        log.error("[interactive] Error:", err.message);
        sendWs(ws, { type: "interactive_error", message: err.message ?? "Unknown error", requestId: randomUUID() });
      }
      return;
    }

    if (msg.type === "get_history") {
      if (!authenticated) {
        sendWs(ws, { type: "error", message: "Not authenticated" });
        return;
      }
      const sessionKey = msg.sessionKey || "main";
      if (!gatewayRpc.isReady) {
        sendWs(ws, { type: "history_result", sessionKey, messages: [], error: "Gateway not connected" });
        return;
      }
      try {
        const result = await gatewayRpc.rpc("chat.history", { sessionKey });
        const messages = (result?.messages ?? [])
          .filter((m: any) => m.role === "user" || m.role === "assistant")
          .map((m: any) => ({
            role: m.role,
            content: typeof m.content === "string" ? m.content
              : Array.isArray(m.content)
                ? m.content.filter((b: any) => b.type === "text").map((b: any) => b.text).join("")
                : "",
          }))
          .filter((m: any) => m.content); // skip empty (tool-call-only) messages
        sendWs(ws, { type: "history_result", sessionKey, messages });
      } catch (err: any) {
        sendWs(ws, { type: "history_result", sessionKey, messages: [], error: err.message });
      }
      return;
    }

    if (msg.type === "get_sessions") {
      if (!authenticated) {
        sendWs(ws, { type: "error", message: "Not authenticated" });
        return;
      }
      if (!gatewayRpc.isReady) {
        sendWs(ws, { type: "sessions_result", sessions: [], error: "Gateway not connected" });
        return;
      }
      try {
        const result = await gatewayRpc.rpc("sessions.list", {});
        sendWs(ws, { type: "sessions_result", sessions: result?.sessions ?? [] });
      } catch (err: any) {
        sendWs(ws, { type: "sessions_result", sessions: [], error: err.message });
      }
      return;
    }

    // ── Generic Gateway RPC forwarding (used by cloud API) ──

    if (msg.type === "gateway_rpc") {
      if (!authenticated) {
        sendWs(ws, { type: "gateway_rpc_result", id: msg.id, error: "Not authenticated" });
        return;
      }
      if (!gatewayRpc.isReady) {
        sendWs(ws, { type: "gateway_rpc_result", id: msg.id, error: "Gateway not connected" });
        return;
      }
      try {
        const result = await gatewayRpc.rpc(msg.method, msg.params || {});
        sendWs(ws, { type: "gateway_rpc_result", id: msg.id, payload: result });
      } catch (err: any) {
        sendWs(ws, { type: "gateway_rpc_result", id: msg.id, error: err.message });
      }
      return;
    }

    // ── Media message: download attachments from URL → temp files → forward to AI ──

    if (msg.type === "media_message") {
      if (!authenticated || !deviceId) {
        sendWs(ws, { type: "error", message: "Not authenticated" });
        return;
      }
      const mediaMsg = msg as WsMediaMessage;
      const mediaPaths: string[] = [];

      sendWs(ws, { type: "ack", sound: "notifyArrive" });

      if (mediaMsg.attachments?.length) {
        const mediaDir = join(tmpdir(), "pinclaw-media");
        if (!existsSync(mediaDir)) mkdirSync(mediaDir, { recursive: true });

        for (const att of mediaMsg.attachments) {
          try {
            const ext = extForMime(att.mediaType) || extname(att.filename) || ".bin";
            const filename = `${randomUUID()}${ext}`;
            const filePath = join(mediaDir, filename);

            if (att.url) {
              // Download from URL (HTTP upload path)
              const res = await fetch(att.url);
              if (!res.ok) throw new Error(`HTTP ${res.status}`);
              const buf = Buffer.from(await res.arrayBuffer());
              writeFileSync(filePath, buf);
            } else if (att.data) {
              // Fallback: base64 inline (for backwards compat)
              writeFileSync(filePath, Buffer.from(att.data, "base64"));
            } else {
              continue;
            }

            mediaPaths.push(filePath);
            log.info(`[media] Saved attachment: ${att.filename} (${att.mediaType}) → ${filePath}`);
          } catch (err: any) {
            log.error(`[media] Failed to save attachment ${att.filename}: ${err.message}`);
          }
        }
      }

      const text = mediaMsg.text?.trim() || "";
      processMessage(text, { deviceId, mediaPaths });
      return;
    }

    if (msg.type === "text") {
      if (!authenticated || !deviceId) {
        sendWs(ws, { type: "error", message: "Not authenticated" });
        return;
      }
      sendWs(ws, { type: "ack", sound: "notifyArrive" });
      processMessage(msg.content, { deviceId });
      return;
    }

    // ── Cron management via Gateway RPC ──

    if (msg.type === "cron_list") {
      if (!authenticated) { sendWs(ws, { type: "error", message: "Not authenticated" }); return; }
      if (!gatewayRpc.isReady) { sendWs(ws, { type: "cron_result", action: "list", jobs: [], error: "Gateway not connected" }); return; }
      try {
        const result = await gatewayRpc.rpc("cron.list", {});
        sendWs(ws, { type: "cron_result", action: "list", jobs: result?.jobs ?? [] });
      } catch (err: any) {
        sendWs(ws, { type: "cron_result", action: "list", jobs: [], error: err.message });
      }
      return;
    }

    if (msg.type === "cron_add") {
      if (!authenticated) { sendWs(ws, { type: "error", message: "Not authenticated" }); return; }
      if (!gatewayRpc.isReady) { sendWs(ws, { type: "cron_result", action: "add", error: "Gateway not connected" }); return; }
      try {
        const result = await gatewayRpc.rpc("cron.add", msg.params ?? {});
        sendWs(ws, { type: "cron_result", action: "add", ok: true, result });
      } catch (err: any) {
        sendWs(ws, { type: "cron_result", action: "add", error: err.message });
      }
      return;
    }

    if (msg.type === "cron_remove") {
      if (!authenticated) { sendWs(ws, { type: "error", message: "Not authenticated" }); return; }
      if (!gatewayRpc.isReady) { sendWs(ws, { type: "cron_result", action: "remove", error: "Gateway not connected" }); return; }
      try {
        await gatewayRpc.rpc("cron.remove", { id: msg.id });
        sendWs(ws, { type: "cron_result", action: "remove", ok: true, id: msg.id });
      } catch (err: any) {
        sendWs(ws, { type: "cron_result", action: "remove", error: err.message });
      }
      return;
    }

    if (msg.type === "cron_toggle") {
      if (!authenticated) { sendWs(ws, { type: "error", message: "Not authenticated" }); return; }
      if (!gatewayRpc.isReady) { sendWs(ws, { type: "cron_result", action: "toggle", error: "Gateway not connected" }); return; }
      try {
        await gatewayRpc.rpc("cron.update", { id: msg.id, patch: { enabled: msg.enabled } });
        sendWs(ws, { type: "cron_result", action: "toggle", ok: true, id: msg.id });
      } catch (err: any) {
        sendWs(ws, { type: "cron_result", action: "toggle", error: err.message });
      }
      return;
    }

    if (msg.type === "cron_run") {
      if (!authenticated) { sendWs(ws, { type: "error", message: "Not authenticated" }); return; }
      if (!gatewayRpc.isReady) { sendWs(ws, { type: "cron_result", action: "run", error: "Gateway not connected" }); return; }
      try {
        await gatewayRpc.rpc("cron.run", { id: msg.id, mode: "force" });
        sendWs(ws, { type: "cron_result", action: "run", ok: true, id: msg.id });
      } catch (err: any) {
        sendWs(ws, { type: "cron_result", action: "run", error: err.message });
      }
      return;
    }

    if (msg.type === "cron_runs") {
      if (!authenticated) { sendWs(ws, { type: "error", message: "Not authenticated" }); return; }
      if (!gatewayRpc.isReady) { sendWs(ws, { type: "cron_result", action: "runs", runs: [], error: "Gateway not connected" }); return; }
      try {
        const result = await gatewayRpc.rpc("cron.runs", { id: msg.id, limit: msg.limit ?? 20 });
        sendWs(ws, { type: "cron_result", action: "runs", id: msg.id, runs: result?.runs ?? [] });
      } catch (err: any) {
        sendWs(ws, { type: "cron_result", action: "runs", runs: [], error: err.message });
      }
      return;
    }
  });

  ws.on("close", () => {
    clearTimeout(authTimer);
    if (deviceId) {
      deviceManager.removeDevice(deviceId, ws);
    }
  });

  ws.on("error", (err) => {
    log.error(`WebSocket error (device=${deviceId}):`, err.message);
  });

  const pingInterval = setInterval(() => {
    if (ws.readyState === WebSocket.OPEN) ws.ping();
  }, 30_000);
  ws.on("close", () => clearInterval(pingInterval));
}

function extForMime(mime: string): string {
  const map: Record<string, string> = {
    "image/jpeg": ".jpg", "image/png": ".png", "image/gif": ".gif",
    "image/webp": ".webp", "image/heic": ".heic",
    "audio/wav": ".wav", "audio/mp4": ".m4a", "audio/mpeg": ".mp3",
    "audio/ogg": ".ogg", "audio/aac": ".aac",
    "application/pdf": ".pdf",
    "text/plain": ".txt", "text/csv": ".csv",
  };
  return map[mime] ?? "";
}
