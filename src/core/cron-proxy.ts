import type { IncomingMessage, ServerResponse } from "node:http";
import { execFile } from "node:child_process";
import type { Logger } from "./utils.js";
import { readJsonBody } from "./utils.js";

export class CronProxy {
  private log: Logger;

  constructor(log: Logger) {
    this.log = log;
  }

  private execCron(args: string[]): Promise<string> {
    return new Promise((resolve, reject) => {
      execFile("openclaw", ["cron", ...args], { timeout: 10_000 }, (err, stdout, stderr) => {
        if (err) reject(new Error(stderr || err.message));
        else resolve(stdout.trim());
      });
    });
  }

  async handleList(res: ServerResponse): Promise<void> {
    try {
      const output = await this.execCron(["list", "--json"]);
      res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(output || "[]");
    } catch (err: any) {
      this.log.error("cron list failed:", err.message);
      res.writeHead(500, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: err.message }));
    }
  }

  async handleCreate(req: IncomingMessage, res: ServerResponse): Promise<void> {
    let body: any;
    try {
      body = await readJsonBody(req);
    } catch {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid JSON" }));
      return;
    }

    const { name, message, at, every, cron, announce, deleteAfterRun, channel, to, session, bestEffortDeliver, deliveryType } = body;
    if (!message) {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Missing 'message' field" }));
      return;
    }

    // Validate and apply delivery type tag
    const validDeliveryTypes = ["notify", "silent", "result"] as const;
    if (deliveryType && !validDeliveryTypes.includes(deliveryType)) {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: `Invalid deliveryType: ${deliveryType}. Must be one of: ${validDeliveryTypes.join(", ")}` }));
      return;
    }
    const taggedMessage = deliveryType
      ? `[DELIVERY:${deliveryType.toUpperCase()}] ${message}`
      : message;

    const jobName = name || `pinclaw-${Date.now()}`;
    const args: string[] = ["add", "--json", "--name", jobName, "--message", taggedMessage];
    if (at) args.push("--at", at);
    if (every) args.push("--every", every);
    if (cron) args.push("--cron", cron);
    if (announce !== false) args.push("--announce");
    if (deleteAfterRun) args.push("--delete-after-run");
    if (channel) args.push("--channel", channel);
    if (to) args.push("--to", to);
    if (session) args.push("--session", session);
    if (bestEffortDeliver) args.push("--best-effort-deliver");

    try {
      const output = await this.execCron(args);
      this.log.info(`Cron job created: ${output.slice(0, 100)}`);
      res.writeHead(201, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(output || JSON.stringify({ ok: true }));
    } catch (err: any) {
      this.log.error("cron add failed:", err.message);
      res.writeHead(500, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: err.message }));
    }
  }

  async handleDelete(jobId: string, res: ServerResponse): Promise<void> {
    try {
      await this.execCron(["rm", jobId]);
      this.log.info(`Cron job deleted: ${jobId}`);
      res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ ok: true, id: jobId }));
    } catch (err: any) {
      this.log.error(`cron rm ${jobId} failed:`, err.message);
      res.writeHead(500, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: err.message }));
    }
  }

  async handleToggle(jobId: string, req: IncomingMessage, res: ServerResponse): Promise<void> {
    let body: any = {};
    try {
      body = await readJsonBody(req);
    } catch {}

    const enabled = body.enabled ?? true;
    const action = enabled ? "enable" : "disable";

    try {
      await this.execCron([action, jobId]);
      this.log.info(`Cron job ${action}d: ${jobId}`);
      res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ ok: true, id: jobId, enabled }));
    } catch (err: any) {
      this.log.error(`cron ${action} ${jobId} failed:`, err.message);
      res.writeHead(500, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: err.message }));
    }
  }
}
