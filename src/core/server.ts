import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { WebSocketServer, WebSocket } from "ws";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import type { DeviceToolDef, ContextHint, DeviceSkillManifest } from "../types.js";
import type { Logger } from "./utils.js";
import { defaultLogger, sendWs } from "./utils.js";
import { DeviceManager } from "./device-manager.js";
import { GatewayRpc } from "./gateway-rpc.js";
import { CronProxy } from "./cron-proxy.js";
import { SkillsCrud } from "./skills-crud.js";
import { VersionChecker } from "./version-check.js";
import { ToolRegistry } from "../tools/registry.js";
import { handleWsConnection } from "./ws-handler.js";
import { handleHttpRequest } from "./http-router.js";
import { callAgent } from "./ai-pipeline.js";
import { InteractiveAI } from "../interactive-ai.js";

export interface PinclawWsServerOptions {
  port: number;
  authToken: string;
  gatewayUrl: string;
  gatewayToken: string;
  abortSignal?: AbortSignal;
  log?: Logger;
}

export class PinclawWsServer {
  private httpServer: ReturnType<typeof createServer> | null = null;
  private wss: WebSocketServer | null = null;
  private port: number;
  private authToken: string;
  private log: Logger;

  // Composed modules
  private deviceManager: DeviceManager;
  private gatewayRpc: GatewayRpc;
  private cronProxy: CronProxy;
  private skillsCrud: SkillsCrud;
  private versionChecker: VersionChecker;
  private toolRegistry: ToolRegistry;
  private interactiveAI: InteractiveAI | null = null;
  private updateRequired = false;

  constructor(opts: PinclawWsServerOptions) {
    this.port = opts.port;
    this.authToken = opts.authToken;
    this.log = opts.log ?? defaultLogger();

    // Initialize modules
    this.deviceManager = new DeviceManager(this.log);
    this.gatewayRpc = new GatewayRpc({
      gatewayUrl: opts.gatewayUrl,
      gatewayToken: opts.gatewayToken,
      log: this.log,
      onReady: () => this.handleVersionCheck(),
    });
    this.cronProxy = new CronProxy(this.log);
    this.skillsCrud = new SkillsCrud(this.log);
    this.versionChecker = new VersionChecker(this.log);
    this.toolRegistry = new ToolRegistry(this.log);

    // Initialize Interactive AI (separate config, falls back to main AI config)
    const iaKey = process.env.INTERACTIVE_AI_KEY || process.env.AI_API_KEY || "";
    const iaBase = process.env.INTERACTIVE_AI_BASE_URL || process.env.AI_BASE_URL || "";
    const iaModel = process.env.INTERACTIVE_AI_MODEL || process.env.AI_LIGHT_MODEL || "kimi-k2.5";
    if (iaKey && iaBase) {
      this.interactiveAI = new InteractiveAI(iaKey, iaBase, iaModel);
      this.log.info(`Interactive AI enabled (model: ${iaModel}, base: ${iaBase})`);
    }

    if (opts.abortSignal) {
      opts.abortSignal.addEventListener("abort", () => this.stop(), { once: true });
    }
  }

  // ── Lifecycle ──

  async start(): Promise<void> {
    // Discover and load server tools
    const currentDir = dirname(fileURLToPath(import.meta.url));
    const toolsDir = join(currentDir, "..", "tools");
    await this.toolRegistry.discoverAndLoad(toolsDir);

    return new Promise((resolve, reject) => {
      let started = false;

      this.httpServer = createServer((req, res) => this.handleHttp(req, res));
      this.wss = new WebSocketServer({ server: this.httpServer });

      this.httpServer.on("listening", () => {
        started = true;
        this.log.info(`Server listening on port ${this.port} (WebSocket + HTTP fallback)`);
        resolve();
      });

      this.httpServer.on("error", (err) => {
        this.log.error("Server error:", err);
        if (!started) reject(err);
      });

      this.wss.on("connection", (ws) => this.handleWs(ws));

      this.httpServer.listen(this.port);

      // Connect to Gateway via WebSocket RPC
      this.gatewayRpc.connect();
    });
  }

  stop(): void {
    if (!this.wss) return;
    this.gatewayRpc.close();
    for (const deviceId of this.deviceManager.listConnectedDevices()) {
      const ws = this.deviceManager.getDeviceWs(deviceId);
      ws?.close(1000, "Server shutting down");
    }
    this.wss.close();
    this.httpServer?.close();
    this.wss = null;
    this.httpServer = null;
    this.log.info("Server stopped");
  }

  // ── Public API (backward compatible) ──

  async sendToDevice(deviceId: string, text: string): Promise<{ ok: boolean }> {
    return this.deviceManager.sendToDevice(deviceId, text);
  }

  async relayToDevice(deviceId: string, message: string, source?: string): Promise<{ ok: boolean }> {
    return this.deviceManager.relayToDevice(deviceId, message, source);
  }

  isDeviceConnected(deviceId: string): boolean {
    return this.deviceManager.isConnected(deviceId);
  }

  listConnectedDevices(): string[] {
    return this.deviceManager.listConnectedDevices();
  }

  getDeviceTools(deviceId: string): DeviceToolDef[] {
    return this.deviceManager.getDeviceTools(deviceId);
  }

  getAllDeviceTools(): DeviceToolDef[] {
    return this.deviceManager.getAllDeviceTools();
  }

  getAllContextHints(): ContextHint[] {
    return this.deviceManager.getAllContextHints();
  }

  getDeviceSkillsForPrompt(): {
    skills: DeviceSkillManifest[];
    connected: boolean;
    lastSeen: string | null;
  } | null {
    return this.deviceManager.getDeviceSkillsForPrompt();
  }

  getPersistedContextHints(): ContextHint[] {
    return this.deviceManager.getPersistedContextHints();
  }

  getPluginVersion(): string {
    return this.versionChecker.getPluginVersion();
  }

  getVersionInfo() {
    return this.versionChecker.versionInfo;
  }

  getServerToolsForPrompt(): string {
    return this.toolRegistry.buildPromptBlock();
  }

  // ── Internal routing ──

  private async handleVersionCheck(): Promise<void> {
    const required = await this.versionChecker.checkForUpdates();
    if (required) {
      this.updateRequired = true;
      // Notify all connected devices before shutting down
      this.versionChecker.notifyAllDevices(
        new Map(
          this.deviceManager.listConnectedDevices().map((id) => [
            id,
            { ws: this.deviceManager.getDeviceWs(id)! },
          ]),
        ),
      );
      this.log.error("Shutting down in 10 seconds due to required update...");
      setTimeout(() => {
        this.stop();
        process.exit(1);
      }, 10_000);
    }
  }

  private handleWs(ws: WebSocket): void {
    handleWsConnection(ws, {
      authToken: this.authToken,
      deviceManager: this.deviceManager,
      versionChecker: this.versionChecker,
      updateRequired: this.updateRequired,
      interactiveAI: this.interactiveAI,
      gatewayRpc: this.gatewayRpc,
      processMessage: (text, opts) => this.processMessage(text, opts),
      log: this.log,
    });
  }

  private async handleHttp(req: IncomingMessage, res: ServerResponse): Promise<void> {
    await handleHttpRequest(req, res, {
      port: this.port,
      authToken: this.authToken,
      deviceManager: this.deviceManager,
      gatewayRpc: this.gatewayRpc,
      cronProxy: this.cronProxy,
      skillsCrud: this.skillsCrud,
      versionChecker: this.versionChecker,
      callAgent: (deviceId, text) => this.callAgentInternal(deviceId, text),
      processMessage: (text, opts) => this.processMessage(text, opts as any),
      log: this.log,
    });
  }

  // ── Message processing ──

  private async processMessage(userText: string, opts: { deviceId?: string; mediaPaths?: string[] }): Promise<void> {
    const { deviceId, mediaPaths } = opts;

    try {
      const result = await this.callAgentInternal(deviceId ?? "web", userText, mediaPaths);

      if (result.content && deviceId) {
        const ws = this.deviceManager.getDeviceWs(deviceId);
        if (ws) {
          sendWs(ws, { type: "agent_message", content: result.content, proactive: false });
        }
      }

      if (result.error) {
        this.log.error(`AI error: ${result.error}`);
        if (deviceId) {
          const ws = this.deviceManager.getDeviceWs(deviceId);
          if (ws) {
            sendWs(ws, { type: "error", message: result.error });
          }
        }
      }
    } catch (err: any) {
      this.log.error(`processMessage error: ${err.message}`);
      if (deviceId) {
        const ws = this.deviceManager.getDeviceWs(deviceId);
        if (ws) {
          sendWs(ws, { type: "error", message: err.message ?? String(err) });
        }
      }
    }
  }

  private callAgentInternal(deviceId: string, text: string, mediaPaths?: string[]): Promise<{ content?: string; error?: string }> {
    return callAgent(deviceId, text, {
      gatewayRpc: this.gatewayRpc,
      deviceManager: this.deviceManager,
      toolRegistry: this.toolRegistry,
      log: this.log,
      mediaPaths,
    });
  }
}
