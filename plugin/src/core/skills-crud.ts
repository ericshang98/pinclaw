import type { IncomingMessage, ServerResponse } from "node:http";
import { readFileSync, writeFileSync, mkdirSync, readdirSync, existsSync, rmSync } from "node:fs";
import { join } from "node:path";
import type { Logger } from "./utils.js";
import { SKILLS_DIR, parseFrontmatter, buildSkillMd, readJsonBody } from "./utils.js";

export class SkillsCrud {
  private log: Logger;

  constructor(log: Logger) {
    this.log = log;
  }

  handleList(res: ServerResponse): void {
    try {
      if (!existsSync(SKILLS_DIR)) {
        res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
        res.end("[]");
        return;
      }

      const entries = readdirSync(SKILLS_DIR, { withFileTypes: true });
      const skills: any[] = [];

      for (const entry of entries) {
        if (!entry.isDirectory()) continue;
        const mdPath = join(SKILLS_DIR, entry.name, "SKILL.md");
        if (!existsSync(mdPath)) continue;

        try {
          const raw = readFileSync(mdPath, "utf-8");
          const { meta, body } = parseFrontmatter(raw);
          skills.push({
            name: entry.name,
            description: meta.description ?? "",
            userInvocable: meta.userInvocable === "true",
            bodyPreview: body.slice(0, 200),
            bodyLength: body.length,
          });
        } catch {}
      }

      res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify(skills));
    } catch (err: any) {
      this.log.error("skills list failed:", err.message);
      res.writeHead(500, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: err.message }));
    }
  }

  handleGet(name: string, res: ServerResponse): void {
    if (!/^[a-z0-9-]+$/.test(name)) {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid skill name" }));
      return;
    }

    const mdPath = join(SKILLS_DIR, name, "SKILL.md");
    if (!existsSync(mdPath)) {
      res.writeHead(404, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Skill not found" }));
      return;
    }

    try {
      const raw = readFileSync(mdPath, "utf-8");
      const { meta, body } = parseFrontmatter(raw);
      res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({
        name,
        description: meta.description ?? "",
        userInvocable: meta.userInvocable === "true",
        body,
      }));
    } catch (err: any) {
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

    const { name, description, userInvocable, body: skillBody } = body;
    if (!name || typeof name !== "string") {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Missing 'name' field" }));
      return;
    }

    if (!/^[a-z0-9-]+$/.test(name)) {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid name: only a-z, 0-9, and - allowed" }));
      return;
    }

    const skillDir = join(SKILLS_DIR, name);
    if (existsSync(skillDir)) {
      res.writeHead(409, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Skill already exists" }));
      return;
    }

    try {
      mkdirSync(skillDir, { recursive: true });
      const md = buildSkillMd(name, description ?? "", userInvocable ?? false, skillBody ?? "");
      writeFileSync(join(skillDir, "SKILL.md"), md, "utf-8");

      this.log.info(`Skill created: ${name}`);
      res.writeHead(201, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ ok: true, name }));
    } catch (err: any) {
      this.log.error("skill create failed:", err.message);
      res.writeHead(500, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: err.message }));
    }
  }

  async handleUpdate(name: string, req: IncomingMessage, res: ServerResponse): Promise<void> {
    if (!/^[a-z0-9-]+$/.test(name)) {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid skill name" }));
      return;
    }

    const mdPath = join(SKILLS_DIR, name, "SKILL.md");
    if (!existsSync(mdPath)) {
      res.writeHead(404, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Skill not found" }));
      return;
    }

    let body: any;
    try {
      body = await readJsonBody(req);
    } catch {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid JSON" }));
      return;
    }

    try {
      const raw = readFileSync(mdPath, "utf-8");
      const existing = parseFrontmatter(raw);

      const newDescription = body.description ?? existing.meta.description ?? "";
      const newUserInvocable = body.userInvocable ?? (existing.meta.userInvocable === "true");
      const newBody = body.body ?? existing.body;

      const md = buildSkillMd(name, newDescription, newUserInvocable, newBody);
      writeFileSync(mdPath, md, "utf-8");

      this.log.info(`Skill updated: ${name}`);
      res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ ok: true, name }));
    } catch (err: any) {
      this.log.error("skill update failed:", err.message);
      res.writeHead(500, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: err.message }));
    }
  }

  handleDelete(name: string, res: ServerResponse): void {
    if (!/^[a-z0-9-]+$/.test(name)) {
      res.writeHead(400, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Invalid skill name" }));
      return;
    }

    const skillDir = join(SKILLS_DIR, name);
    if (!existsSync(skillDir)) {
      res.writeHead(404, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: "Skill not found" }));
      return;
    }

    try {
      rmSync(skillDir, { recursive: true, force: true });
      this.log.info(`Skill deleted: ${name}`);
      res.writeHead(200, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ ok: true, name }));
    } catch (err: any) {
      this.log.error("skill delete failed:", err.message);
      res.writeHead(500, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
      res.end(JSON.stringify({ error: err.message }));
    }
  }
}
