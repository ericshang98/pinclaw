import WebSocket from "ws";

export interface RelayClientOptions {
  relayToken: string;
  relayUrl: string;           // e.g. "wss://api.pinclaw.ai"
  localPluginPort: number;    // e.g. 18790
  localAuthToken: string;     // authToken for local Plugin WS at :18790
  log?: { info: (...args: any[]) => void; warn: (...args: any[]) => void; error: (...args: any[]) => void };
}

interface ChannelState {
  relayWs: WebSocket | null;
  localWs: WebSocket | null;
  reconnectTimer: ReturnType<typeof setTimeout> | null;
  reconnectAttempts: number;
  stopped: boolean;
}

const MAX_RECONNECT_DELAY_MS = 60_000;
const BASE_RECONNECT_DELAY_MS = 3_000;
const PING_INTERVAL_MS = 30_000;
const PONG_TIMEOUT_MS = 10_000;

export class RelayClient {
  private token: string;
  private relayUrl: string;
  private localPluginPort: number;
  private localAuthToken: string;
  private log: { info: (...args: any[]) => void; warn: (...args: any[]) => void; error: (...args: any[]) => void };

  private plugin: ChannelState;
  private pingInterval: ReturnType<typeof setInterval> | null = null;
  private pongTimer: ReturnType<typeof setTimeout> | null = null;
  private stopped = false;

  constructor(opts: RelayClientOptions) {
    this.token = opts.relayToken;
    this.relayUrl = opts.relayUrl;
    this.localPluginPort = opts.localPluginPort;
    this.localAuthToken = opts.localAuthToken;
    this.log = opts.log ?? {
      info: (...args: any[]) => console.log("[relay]", ...args),
      warn: (...args: any[]) => console.warn("[relay]", ...args),
      error: (...args: any[]) => console.error("[relay]", ...args),
    };

    this.plugin = this.createChannelState();
  }

  private createChannelState(): ChannelState {
    return { relayWs: null, localWs: null, reconnectTimer: null, reconnectAttempts: 0, stopped: false };
  }

  async start(): Promise<void> {
    this.stopped = false;
    this.log.info(`Starting relay client → ${this.relayUrl}`);
    this.connectChannel();

    // Periodic ping to keep relay connection alive
    this.pingInterval = setInterval(() => {
      this.sendPing(this.plugin);
    }, PING_INTERVAL_MS);
  }

  stop(): void {
    this.stopped = true;
    this.plugin.stopped = true;

    if (this.pingInterval) {
      clearInterval(this.pingInterval);
      this.pingInterval = null;
    }
    if (this.pongTimer) {
      clearTimeout(this.pongTimer);
      this.pongTimer = null;
    }

    this.closeChannel(this.plugin);

    this.log.info("Relay client stopped");
  }

  private closeChannel(ch: ChannelState): void {
    if (ch.reconnectTimer) {
      clearTimeout(ch.reconnectTimer);
      ch.reconnectTimer = null;
    }
    if (ch.relayWs) {
      ch.relayWs.removeAllListeners();
      ch.relayWs.close(1000, "Client stopping");
      ch.relayWs = null;
    }
    if (ch.localWs) {
      ch.localWs.removeAllListeners();
      ch.localWs.close(1000, "Client stopping");
      ch.localWs = null;
    }
  }

  private sendPing(ch: ChannelState): void {
    if (ch.relayWs?.readyState === WebSocket.OPEN) {
      ch.relayWs.ping();

      // Start pong timeout — if no pong within PONG_TIMEOUT_MS, connection is dead
      if (this.pongTimer) clearTimeout(this.pongTimer);
      this.pongTimer = setTimeout(() => {
        this.log.warn("Pong timeout — connection appears dead, forcing reconnect");
        if (ch.relayWs) {
          ch.relayWs.terminate(); // hard kill, triggers "close" event → scheduleReconnect
        }
      }, PONG_TIMEOUT_MS);
    }
  }

  private connectChannel(): void {
    const ch = this.plugin;
    if (ch.stopped || this.stopped) return;

    const url = `${this.relayUrl}/relay/connect?token=${encodeURIComponent(this.token)}&channel=plugin`;
    this.log.info(`Connecting relay plugin → ${this.relayUrl}/relay/connect`);

    const ws = new WebSocket(url);
    ch.relayWs = ws;

    ws.on("open", () => {
      this.log.info("Relay plugin connected");
      ch.reconnectAttempts = 0;

      // Connect to local plugin server
      this.connectLocal();
    });

    ws.on("message", (data, isBinary) => {
      // Forward from Cloud relay → local WS server
      if (ch.localWs?.readyState === WebSocket.OPEN) {
        ch.localWs.send(isBinary ? data : data.toString());
      } else {
        // Local not connected yet, try to connect and queue
        this.connectLocal();
        // Buffer: wait a bit then retry
        setTimeout(() => {
          if (ch.localWs?.readyState === WebSocket.OPEN) {
            ch.localWs.send(isBinary ? data : data.toString());
          }
        }, 500);
      }
    });

    ws.on("close", (code, reason) => {
      this.log.warn(`Relay plugin closed: ${code} ${reason.toString()}`);
      ch.relayWs = null;
      // Close local connection too
      if (ch.localWs) {
        ch.localWs.removeAllListeners();
        ch.localWs.close(1000, "Relay disconnected");
        ch.localWs = null;
      }
      this.scheduleReconnect();
    });

    ws.on("error", (err) => {
      this.log.error(`Relay plugin error: ${err.message}`);
      // The close event will fire after this
    });

    ws.on("pong", () => {
      // Cloud responded to our ping — connection is alive, cancel timeout
      if (this.pongTimer) {
        clearTimeout(this.pongTimer);
        this.pongTimer = null;
      }
    });
  }

  private connectLocal(): void {
    const ch = this.plugin;
    if (ch.localWs?.readyState === WebSocket.OPEN || ch.localWs?.readyState === WebSocket.CONNECTING) {
      return; // Already connected or connecting
    }

    const url = `ws://127.0.0.1:${this.localPluginPort}`;

    const local = new WebSocket(url);
    ch.localWs = local;

    local.on("open", () => {
      this.log.info(`Local plugin connected (port ${this.localPluginPort}), sending auth...`);
      // Third layer auth: relay-client must authenticate to local Plugin WS
      const authMsg = JSON.stringify({ type: "auth", token: this.localAuthToken, deviceId: "relay-client" });
      local.send(authMsg);
    });

    local.on("message", (data, isBinary) => {
      // Forward from local WS server → Cloud relay
      if (ch.relayWs?.readyState === WebSocket.OPEN) {
        // Filter out auth_ok from local Plugin — iPhone already got auth_ok from Server
        if (!isBinary) {
          const str = data.toString();
          try {
            const parsed = JSON.parse(str);
            if (parsed.type === "auth_ok") {
              this.log.info("Local plugin auth OK, not forwarding to relay");
              return;
            }
          } catch { /* not JSON, forward as-is */ }
        }
        ch.relayWs.send(isBinary ? data : data.toString());
      }
    });

    local.on("close", () => {
      ch.localWs = null;
    });

    local.on("error", (err) => {
      this.log.warn(`Local plugin error: ${err.message}`);
      ch.localWs = null;
    });
  }

  private scheduleReconnect(): void {
    const ch = this.plugin;
    if (ch.stopped || this.stopped) return;

    const delay = Math.min(
      BASE_RECONNECT_DELAY_MS * Math.pow(2, ch.reconnectAttempts),
      MAX_RECONNECT_DELAY_MS,
    );
    // Add jitter: ±25%
    const jitter = delay * (0.75 + Math.random() * 0.5);
    ch.reconnectAttempts++;

    this.log.info(`Reconnecting relay plugin in ${Math.round(jitter / 1000)}s (attempt ${ch.reconnectAttempts})`);

    ch.reconnectTimer = setTimeout(() => {
      ch.reconnectTimer = null;
      this.connectChannel();
    }, jitter);
  }
}
