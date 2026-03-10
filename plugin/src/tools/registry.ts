import { readdirSync } from "node:fs";
import { join } from "node:path";
import type { Logger } from "../core/utils.js";
import type { ServerToolDef, ToolExecutionContext } from "./types.js";

const EXCLUDED_FILES = new Set(["registry.ts", "registry.js", "types.ts", "types.js"]);

export class ToolRegistry {
  private tools = new Map<string, ServerToolDef>();
  private log: Logger;

  constructor(log: Logger) {
    this.log = log;
  }

  register(tool: ServerToolDef): void {
    if (this.tools.has(tool.name)) {
      this.log.warn(`Server tool "${tool.name}" already registered, overwriting`);
    }
    this.tools.set(tool.name, tool);
    this.log.info(`Server tool registered: ${tool.name}`);
  }

  async discoverAndLoad(toolsDir: string): Promise<void> {
    let entries: string[];
    try {
      entries = readdirSync(toolsDir);
    } catch {
      this.log.info(`Tools directory not found: ${toolsDir} — skipping auto-discovery`);
      return;
    }

    for (const file of entries) {
      // Skip excluded files and files starting with underscore
      if (file.startsWith("_")) continue;
      if (EXCLUDED_FILES.has(file)) continue;
      if (!file.endsWith(".ts") && !file.endsWith(".js")) continue;

      const filePath = join(toolsDir, file);
      try {
        const mod = await import(filePath);
        const tool: ServerToolDef | undefined = mod.default ?? mod.tool;
        if (tool && tool.name && typeof tool.execute === "function") {
          this.register(tool);
        } else {
          this.log.warn(`Tool file ${file} does not export a valid ServerToolDef (needs default or "tool" export with name + execute)`);
        }
      } catch (err: any) {
        this.log.error(`Failed to load tool from ${file}: ${err.message}`);
      }
    }

    this.log.info(`Tool registry: ${this.tools.size} tool(s) loaded`);
  }

  getAll(): ServerToolDef[] {
    return Array.from(this.tools.values());
  }

  get(name: string): ServerToolDef | undefined {
    return this.tools.get(name);
  }

  async execute(name: string, params: Record<string, any>, context: ToolExecutionContext): Promise<string> {
    const tool = this.tools.get(name);
    if (!tool) {
      throw new Error(`Server tool not found: ${name}`);
    }
    return tool.execute(params, context);
  }

  buildPromptBlock(): string {
    const tools = this.getAll();
    if (tools.length === 0) return "";

    const toolLines = tools.map(t => {
      const paramDesc = t.parameters.length > 0
        ? ` (params: ${t.parameters.map(p => `${p.name}: ${p.type}${p.required === false ? "?" : ""}`).join(", ")})`
        : "";
      return `- ${t.name}: ${t.description}${paramDesc}`;
    }).join("\n");

    return `
## Server Tools (server-side tools)
The server provides these tools. To use one, output:
<server_tool name="tool_name" params='{"key":"value"}'/>

Available tools:
${toolLines}

Rules:
- Only call one tool at a time. Wait for the result before calling another.
- After receiving tool results, compose a natural response for the user.
`;
  }
}
