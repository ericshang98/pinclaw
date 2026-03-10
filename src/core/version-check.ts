import { WebSocket } from "ws";
import type { Logger } from "./utils.js";
import { sendWs } from "./utils.js";

export const PLUGIN_VERSION = "0.1.0";

const VERSION_CHECK_URL = "https://api.pinclaw.ai/api/v1/version-check";
const VERSION_CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000; // 24 hours

export interface VersionInfo {
  pluginLatest?: string;
  pluginUpdateType?: string;
  updateCommand?: string;
}

export class VersionChecker {
  private lastCheck = 0;
  private info: VersionInfo = {};
  private log: Logger;

  constructor(log: Logger) {
    this.log = log;
  }

  get versionInfo(): VersionInfo {
    return this.info;
  }

  /** Returns true if the server says this plugin version requires a mandatory update. */
  get isUpdateRequired(): boolean {
    return this.info.pluginUpdateType === "required";
  }

  getPluginVersion(): string {
    return PLUGIN_VERSION;
  }

  /**
   * Check for updates. Returns `true` if a **required** (forced) update is needed.
   * Respects 24h cache for non-required checks; required results bypass the cache
   * on subsequent calls so the server can lift the requirement.
   */
  async checkForUpdates(): Promise<boolean> {
    const now = Date.now();
    // If we already know an update is required, always re-check (server may have lifted it)
    if (!this.isUpdateRequired && now - this.lastCheck < VERSION_CHECK_INTERVAL_MS) {
      return this.isUpdateRequired;
    }
    this.lastCheck = now;

    try {
      const url = `${VERSION_CHECK_URL}?plugin_version=${PLUGIN_VERSION}`;
      const resp = await fetch(url, { signal: AbortSignal.timeout(10_000) });
      if (!resp.ok) {
        this.log.warn(`Version check HTTP ${resp.status}`);
        return this.isUpdateRequired;
      }
      const data = await resp.json() as any;
      const pluginInfo = data.plugin;
      if (!pluginInfo) return false;

      this.info = {
        pluginLatest: pluginInfo.latest,
        pluginUpdateType: pluginInfo.update_type,
        updateCommand: pluginInfo.update_command,
      };

      if (pluginInfo.update_type === "none") {
        this.log.info(`Plugin is up to date (v${PLUGIN_VERSION})`);
        return false;
      }

      if (pluginInfo.update_type === "required") {
        const cmd = pluginInfo.update_command ?? "openclaw plugins update @openclaw/pinclaw";
        this.log.error("");
        this.log.error("╔══════════════════════════════════════════════════════════╗");
        this.log.error("║          CRITICAL: Plugin update required!              ║");
        this.log.error("╠══════════════════════════════════════════════════════════╣");
        this.log.error(`║  Current: v${PLUGIN_VERSION.padEnd(44)}║`);
        this.log.error(`║  Required: v${(pluginInfo.latest ?? "?").padEnd(43)}║`);
        this.log.error("║                                                          ║");
        this.log.error(`║  Run: ${cmd.padEnd(50)}║`);
        this.log.error("╚══════════════════════════════════════════════════════════╝");
        this.log.error("");
        return true;
      }

      this.log.info(`Plugin update available: v${PLUGIN_VERSION} → v${pluginInfo.latest} (${pluginInfo.update_type})`);
      return false;
    } catch (err: any) {
      this.log.warn("Version check failed:", err.message);
      return this.isUpdateRequired;
    }
  }

  notifyDevice(ws: WebSocket): void {
    if (!this.info.pluginUpdateType || this.info.pluginUpdateType === "none") return;
    sendWs(ws, {
      type: "update_available",
      current: PLUGIN_VERSION,
      latest: this.info.pluginLatest!,
      update_type: this.info.pluginUpdateType as "optional" | "recommended" | "required",
      update_command: this.info.updateCommand ?? "openclaw plugins update @openclaw/pinclaw",
    });
  }

  notifyAllDevices(devices: Map<string, { ws: WebSocket }>): void {
    for (const [, conn] of devices) {
      if (conn.ws.readyState !== WebSocket.OPEN) continue;
      this.notifyDevice(conn.ws);
    }
  }
}
