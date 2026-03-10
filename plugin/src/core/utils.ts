import { WebSocket } from "ws";
import { join } from "node:path";
import { homedir } from "node:os";
import type { WsOutboundMessage } from "../types.js";

// ── Constants ──

export const SKILLS_DIR = join(homedir(), ".openclaw", "workspace", "skills");

// ── Logger type ──

export interface Logger {
  info: (...args: any[]) => void;
  warn: (...args: any[]) => void;
  error: (...args: any[]) => void;
}

export function defaultLogger(): Logger {
  return {
    info: (...args: any[]) => console.log("[pinclaw]", ...args),
    warn: (...args: any[]) => console.warn("[pinclaw]", ...args),
    error: (...args: any[]) => console.error("[pinclaw]", ...args),
  };
}

// ── Frontmatter parsing ──

export function parseFrontmatter(raw: string): { meta: Record<string, string>; body: string } {
  const match = raw.match(/^---\r?\n([\s\S]*?)\r?\n---\r?\n?([\s\S]*)$/);
  if (!match) return { meta: {}, body: raw };
  const meta: Record<string, string> = {};
  for (const line of match[1].split("\n")) {
    const idx = line.indexOf(":");
    if (idx > 0) {
      const key = line.slice(0, idx).trim();
      let val = line.slice(idx + 1).trim();
      if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) {
        val = val.slice(1, -1);
      }
      meta[key] = val;
    }
  }
  return { meta, body: match[2] };
}

export function buildSkillMd(name: string, description: string, userInvocable: boolean, body: string): string {
  return `---\nname: "${name}"\ndescription: "${description}"\nuserInvocable: ${userInvocable}\n---\n${body}`;
}

// ── WebSocket helpers ──

export function sendWs(ws: WebSocket, msg: WsOutboundMessage): void {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(msg));
  }
}

// ── HTTP helpers ──

export async function readJsonBody(req: AsyncIterable<Buffer | string>): Promise<any> {
  const chunks: Buffer[] = [];
  for await (const chunk of req) {
    chunks.push(typeof chunk === "string" ? Buffer.from(chunk) : chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString("utf-8"));
}
