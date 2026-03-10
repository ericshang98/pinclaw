import { WebSocket } from "ws";
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";
import { homedir } from "node:os";
import { randomUUID } from "node:crypto";
import type {
  DeviceToolDef,
  ContextHint,
  DeviceSkillManifest,
  PersistedDeviceState,
} from "../types.js";
import type { Logger } from "./utils.js";
import { sendWs } from "./utils.js";

const DEVICE_STATE_PATH = join(homedir(), ".openclaw", "pinclaw-device-state.json");

export interface DeviceConnection {
  ws: WebSocket;
  deviceId: string;
}

export class DeviceManager {
  private devices = new Map<string, DeviceConnection>();
  private deviceSkills = new Map<string, DeviceSkillManifest[]>();
  private deviceToolsFlat = new Map<string, DeviceToolDef[]>();
  private deviceContextHints = new Map<string, Map<string, ContextHint>>();
  private persistedState: PersistedDeviceState | null = null;
  private pendingToolCalls = new Map<string, {
    resolve: (result: { success: boolean; result?: string; error?: string }) => void;
    reject: (err: Error) => void;
    timer: ReturnType<typeof setTimeout>;
  }>();
  private log: Logger;

  constructor(log: Logger) {
    this.log = log;
    this.loadPersistedState();
  }

  // ── Connection management ──

  addDevice(deviceId: string, ws: WebSocket): void {
    const old = this.devices.get(deviceId);
    if (old) old.ws.close(1000, "Replaced by new connection");
    this.devices.set(deviceId, { ws, deviceId });
  }

  removeDevice(deviceId: string, ws: WebSocket): void {
    const conn = this.devices.get(deviceId);
    if (conn?.ws !== ws) return;

    // Persist current skills before clearing memory
    const skills = this.deviceSkills.get(deviceId);
    if (skills && skills.length > 0) {
      this.persistDeviceState(deviceId, skills, false);
    }

    this.devices.delete(deviceId);
    this.deviceSkills.delete(deviceId);
    this.deviceToolsFlat.delete(deviceId);
    this.deviceContextHints.delete(deviceId);

    for (const [callId, pending] of this.pendingToolCalls) {
      pending.reject(new Error("Device disconnected"));
      clearTimeout(pending.timer);
      this.pendingToolCalls.delete(callId);
    }

    this.log.info(`Device disconnected: ${deviceId}`);
  }

  getDeviceWs(deviceId: string): WebSocket | null {
    const conn = this.devices.get(deviceId);
    return conn && conn.ws.readyState === WebSocket.OPEN ? conn.ws : null;
  }

  getDeviceConnection(deviceId: string): DeviceConnection | undefined {
    return this.devices.get(deviceId);
  }

  isConnected(deviceId: string): boolean {
    const conn = this.devices.get(deviceId);
    return Boolean(conn && conn.ws.readyState === WebSocket.OPEN);
  }

  listConnectedDevices(): string[] {
    return Array.from(this.devices.keys());
  }

  hasDevice(deviceId: string): boolean {
    return this.devices.has(deviceId);
  }

  /** Returns the internal devices map (for version checker notifications etc.) */
  get connectedDevices(): Map<string, DeviceConnection> {
    return this.devices;
  }

  // ── Skills management ──

  registerSkills(deviceId: string, skills: DeviceSkillManifest[]): void {
    this.deviceSkills.set(deviceId, skills);
    const flatTools = skills
      .filter(s => s.enabled && s.permission === "authorized")
      .flatMap(s => s.tools);
    this.deviceToolsFlat.set(deviceId, flatTools);
    this.persistDeviceState(deviceId, skills, true);

    const totalTools = flatTools.length;
    const totalSkills = skills.length;
    this.log.info(`Device ${deviceId} registered ${totalSkills} skill(s), ${totalTools} active tool(s): ${flatTools.map(t => t.name).join(", ")}`);
  }

  updateContextHints(deviceId: string, hints: ContextHint[]): void {
    let hintsMap = this.deviceContextHints.get(deviceId);
    if (!hintsMap) {
      hintsMap = new Map();
      this.deviceContextHints.set(deviceId, hintsMap);
    }
    for (const hint of hints) {
      hintsMap.set(hint.skill, hint);
    }
    this.log.info(`Device ${deviceId} updated context hints: ${hints.map(h => h.skill).join(", ")}`);
  }

  getDeviceTools(deviceId: string): DeviceToolDef[] {
    return this.deviceToolsFlat.get(deviceId) ?? [];
  }

  getAllDeviceTools(): DeviceToolDef[] {
    const all: DeviceToolDef[] = [];
    for (const tools of this.deviceToolsFlat.values()) {
      all.push(...tools);
    }
    return all;
  }

  getAllContextHints(): ContextHint[] {
    const all: ContextHint[] = [];
    for (const hintsMap of this.deviceContextHints.values()) {
      all.push(...hintsMap.values());
    }
    return all;
  }

  getDeviceSkillsForPrompt(): {
    skills: DeviceSkillManifest[];
    connected: boolean;
    lastSeen: string | null;
  } | null {
    for (const [, skills] of this.deviceSkills) {
      if (skills.length > 0) {
        return { skills, connected: true, lastSeen: null };
      }
    }
    if (this.persistedState && this.persistedState.skills.length > 0) {
      return {
        skills: this.persistedState.skills,
        connected: false,
        lastSeen: this.persistedState.lastSeen,
      };
    }
    return null;
  }

  getPersistedContextHints(): ContextHint[] {
    return this.persistedState?.contextHints ?? [];
  }

  // ── Tool call bridge ──

  callDeviceTool(deviceId: string, toolName: string, params: Record<string, any>): Promise<{ success: boolean; result?: string; error?: string }> {
    const conn = this.devices.get(deviceId);
    if (!conn || conn.ws.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error("Device not connected"));
    }

    const tools = this.deviceToolsFlat.get(deviceId);
    if (!tools || !tools.some(t => t.name === toolName)) {
      return Promise.reject(new Error(`Tool not registered: ${toolName}`));
    }

    const skills = this.deviceSkills.get(deviceId);
    if (skills) {
      const parentSkill = skills.find(s => s.tools.some(t => t.name === toolName));
      if (parentSkill && (!parentSkill.enabled || parentSkill.permission !== "authorized")) {
        return Promise.reject(new Error(`Skill "${parentSkill.name}" is disabled or not authorized`));
      }
    }

    const callId = randomUUID();
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pendingToolCalls.delete(callId);
        reject(new Error("Tool call timeout (10s)"));
      }, 10_000);

      this.pendingToolCalls.set(callId, { resolve, reject, timer });
      sendWs(conn.ws, { type: "tool_call", callId, tool: toolName, params });
      this.log.info(`Tool call → ${deviceId}: ${toolName}(${JSON.stringify(params).slice(0, 100)})`);
    });
  }

  resolvePendingToolCall(callId: string, result: { success: boolean; result?: string; error?: string }): void {
    const pending = this.pendingToolCalls.get(callId);
    if (pending) {
      clearTimeout(pending.timer);
      this.pendingToolCalls.delete(callId);
      pending.resolve(result);
    }
  }

  // ── Send to device ──

  async sendToDevice(deviceId: string, text: string): Promise<{ ok: boolean }> {
    const ws = this.getDeviceWs(deviceId);
    if (ws) {
      sendWs(ws, { type: "agent_message", content: text, proactive: true });
      return { ok: true };
    }
    // Fallback: broadcast to all connected devices (handles relay-client deviceId mismatch)
    const allDevices = this.listConnectedDevices();
    if (allDevices.length > 0) {
      this.log.info(`sendToDevice: ${deviceId} not found, broadcasting to ${allDevices.length} connected device(s)`);
      for (const id of allDevices) {
        const dws = this.getDeviceWs(id);
        if (dws) sendWs(dws, { type: "agent_message", content: text, proactive: true });
      }
      return { ok: true };
    }
    this.log.warn(`sendToDevice: no devices connected (target was ${deviceId})`);
    return { ok: false };
  }

  async relayToDevice(deviceId: string, message: string, source: string = "system"): Promise<{ ok: boolean }> {
    this.log.info(`relay from ${source} to ${deviceId}: ${message.slice(0, 80)}...`);
    return this.sendToDevice(deviceId, message);
  }

  // ── Persistence ──

  private loadPersistedState(): void {
    try {
      const raw = readFileSync(DEVICE_STATE_PATH, "utf-8");
      const data: PersistedDeviceState = JSON.parse(raw);
      if (data.version === 2 && data.skills) {
        data.connected = false;
        this.persistedState = data;
        this.log.info(`Loaded persisted device state: ${data.skills.length} skill(s), last seen ${data.lastSeen}`);
      }
    } catch {}
  }

  private persistDeviceState(deviceId: string, skills: DeviceSkillManifest[], connected: boolean): void {
    const hintsMap = this.deviceContextHints.get(deviceId);
    const contextHints: ContextHint[] = hintsMap ? [...hintsMap.values()] : (this.persistedState?.contextHints ?? []);

    const state: PersistedDeviceState = {
      version: 2,
      deviceId,
      lastSeen: new Date().toISOString(),
      connected,
      skills,
      contextHints,
    };

    try {
      mkdirSync(join(homedir(), ".openclaw"), { recursive: true });
      writeFileSync(DEVICE_STATE_PATH, JSON.stringify(state, null, 2));
      this.persistedState = state;
    } catch (err: any) {
      this.log.error("Failed to persist device state:", err.message);
    }
  }
}
