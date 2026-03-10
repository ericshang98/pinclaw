import { WebSocket } from "ws";
import { randomUUID } from "node:crypto";
import type { Logger } from "./utils.js";
import { getOrCreateDeviceIdentity, signDevicePayload } from "./device-identity.js";

export class GatewayRpc {
  private ws: WebSocket | null = null;
  private ready = false;
  private gatewayUrl: string;
  private gatewayToken: string;
  private log: Logger;
  private onReady?: () => void;

  // Chat RPC callbacks (chat.send → resolved via chat events)
  private rpcCallbacks = new Map<string, {
    resolve: (text: string) => void;
    reject: (err: Error) => void;
    chunks: string[];
    timer: ReturnType<typeof setTimeout>;
  }>();

  // Generic RPC callbacks (non-chat → resolved via res messages)
  private gwRpcCallbacks = new Map<string, {
    resolve: (data: any) => void;
    reject: (err: Error) => void;
    timer: ReturnType<typeof setTimeout>;
  }>();

  constructor(opts: {
    gatewayUrl: string;
    gatewayToken: string;
    log: Logger;
    onReady?: () => void;
  }) {
    this.gatewayUrl = opts.gatewayUrl;
    this.gatewayToken = opts.gatewayToken;
    this.log = opts.log;
    this.onReady = opts.onReady;
  }

  get isReady(): boolean {
    return this.ready && this.ws?.readyState === WebSocket.OPEN;
  }

  connect(): void {
    if (this.ws?.readyState === WebSocket.OPEN) return;

    const wsUrl = this.gatewayUrl.replace(/^http/, "ws");
    this.log.info(`Connecting to Gateway WebSocket: ${wsUrl}`);

    const identity = getOrCreateDeviceIdentity();
    this.log.info(`Device identity: ${identity.deviceId.slice(0, 12)}...`);

    const ws = new WebSocket(wsUrl);
    this.ws = ws;
    this.ready = false;
    let connectSent = false;

    const clientId = "gateway-client";
    const clientMode = "backend";
    const role = "operator";
    const scopes = ["operator.admin", "operator.write", "operator.read"];

    const sendConnect = (nonce?: string) => {
      if (connectSent) return;
      connectSent = true;

      const signedAt = Date.now();
      const payloadStr = `v2|${identity.deviceId}|${clientId}|${clientMode}|${role}|${scopes.join(",")}|${signedAt}|${this.gatewayToken}|${nonce ?? ""}`;
      const signature = signDevicePayload(identity, payloadStr);

      ws.send(JSON.stringify({
        type: "req",
        id: randomUUID(),
        method: "connect",
        params: {
          minProtocol: 3,
          maxProtocol: 3,
          client: {
            id: clientId,
            version: "0.1.0",
            platform: "node",
            mode: clientMode,
            instanceId: randomUUID(),
          },
          role,
          scopes,
          caps: [],
          device: {
            id: identity.deviceId,
            publicKey: identity.publicKeyB64,
            signature,
            signedAt,
            nonce: nonce ?? undefined,
          },
          auth: { token: this.gatewayToken },
          userAgent: "pinclaw-channel/0.1.0",
          locale: "zh",
        },
      }));
    };

    ws.on("open", () => {
      this.log.info("Gateway WebSocket connected, waiting for challenge...");
      setTimeout(() => sendConnect(), 800);
    });

    ws.on("message", (raw) => {
      let msg: any;
      try { msg = JSON.parse(raw.toString()); } catch { return; }

      if (msg.type === "event" && msg.event === "connect.challenge") {
        const nonce = msg.payload?.nonce;
        this.log.info("Got connect challenge, sending auth...");
        sendConnect(nonce);
        return;
      }

      if (msg.type === "res" && !this.ready) {
        if (msg.ok !== false) {
          this.ready = true;
          const authInfo = msg.payload?.auth;
          this.log.info("Gateway WebSocket authenticated — RPC ready");
          this.log.info(`Granted scopes: ${JSON.stringify(authInfo?.scopes ?? "none")}`);
          this.log.info(`Role: ${authInfo?.role ?? "none"}`);
          const methods = msg.payload?.features?.methods ?? [];
          this.log.info(`Available methods: ${methods.join(", ")}`);
          if (authInfo?.deviceToken) {
            this.log.info("Received device token from Gateway (device paired)");
          }
          this.onReady?.();
        } else {
          this.log.error("Gateway auth failed:", JSON.stringify(msg.error ?? msg).slice(0, 500));
        }
        return;
      }

      // Handle res messages: gwRpcCallbacks first, then rpcCallbacks
      if (msg.type === "res" && msg.id) {
        const gwCb = this.gwRpcCallbacks.get(msg.id);
        if (gwCb) {
          clearTimeout(gwCb.timer);
          this.gwRpcCallbacks.delete(msg.id);
          if (msg.ok === false) {
            gwCb.reject(new Error(msg.error?.message ?? "RPC error"));
          } else {
            gwCb.resolve(msg.payload ?? msg.result ?? {});
          }
          return;
        }

        const cb = this.rpcCallbacks.get(msg.id);
        if (cb && msg.ok === false) {
          cb.reject(new Error(msg.error?.message ?? "RPC error"));
          clearTimeout(cb.timer);
          this.rpcCallbacks.delete(msg.id);
        }
        return;
      }

      if (msg.type === "event" && msg.event === "chat") {
        this.handleChatEvent(msg.payload);
        return;
      }
    });

    ws.on("close", () => {
      this.ready = false;
      this.ws = null;
      this.log.warn("Gateway WebSocket disconnected, reconnecting in 3s...");
      setTimeout(() => this.connect(), 3000);
    });

    ws.on("error", (err) => {
      this.log.error("Gateway WebSocket error:", err.message);
    });
  }

  close(): void {
    this.ws?.close();
    this.ws = null;
    this.ready = false;
  }

  // ── Chat RPC ──

  chatSend(sessionKey: string, message: string): Promise<string> {
    return new Promise((resolve, reject) => {
      const reqId = randomUUID();
      const timer = setTimeout(() => {
        this.rpcCallbacks.delete(reqId);
        reject(new Error("RPC timeout (120s)"));
      }, 120_000);

      this.rpcCallbacks.set(reqId, {
        resolve, reject, chunks: [], timer,
      });

      this.ws!.send(JSON.stringify({
        type: "req",
        id: reqId,
        method: "chat.send",
        params: {
          sessionKey,
          message,
          idempotencyKey: randomUUID(),
        },
      }));

      this.log.info(`RPC chat.send → session ${sessionKey}: ${message.slice(0, 60)}...`);
    });
  }

  // ── Generic RPC ──

  rpc(method: string, params: Record<string, unknown>): Promise<any> {
    return new Promise((resolve, reject) => {
      if (!this.isReady) {
        return reject(new Error("Gateway not connected"));
      }
      const reqId = randomUUID();
      const timer = setTimeout(() => {
        this.gwRpcCallbacks.delete(reqId);
        reject(new Error("Gateway RPC timeout (15s)"));
      }, 15_000);

      this.gwRpcCallbacks.set(reqId, { resolve, reject, timer });

      this.ws!.send(JSON.stringify({ type: "req", id: reqId, method, params }));
    });
  }

  // ── Internal ──

  private handleChatEvent(payload: any): void {
    if (!payload) return;

    for (const [reqId, cb] of this.rpcCallbacks) {
      if (payload.state === "delta") {
        const text = this.extractTextFromMessage(payload.message);
        if (text) {
          cb.chunks = [text]; // Delta sends full accumulated text
        }
      } else if (payload.state === "final") {
        const finalText = this.extractTextFromMessage(payload.message)
          ?? cb.chunks.join("");
        clearTimeout(cb.timer);
        this.rpcCallbacks.delete(reqId);
        if (finalText) {
          cb.resolve(finalText);
        } else {
          cb.reject(new Error("Empty agent response"));
        }
        return;
      } else if (payload.state === "error" || payload.state === "aborted") {
        clearTimeout(cb.timer);
        this.rpcCallbacks.delete(reqId);
        const partialText = cb.chunks.join("");
        if (partialText) {
          cb.resolve(partialText);
        } else {
          cb.reject(new Error(payload.errorMessage ?? "Agent error/aborted"));
        }
        return;
      }
    }
  }

  private extractTextFromMessage(message: any): string | null {
    if (!message) return null;
    if (typeof message.content === "string") return message.content;
    if (Array.isArray(message.content)) {
      return message.content
        .filter((b: any) => b.type === "text" && b.text)
        .map((b: any) => b.text)
        .join("") || null;
    }
    return null;
  }
}
