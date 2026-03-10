import type { Logger } from "../core/utils.js";

export interface ServerToolParam {
  name: string;
  type: "string" | "number" | "boolean" | "object";
  required?: boolean;
  description?: string;
}

export interface ToolExecutionContext {
  deviceId: string;
  log: Logger;
  gatewayRpc?: (method: string, params: Record<string, unknown>) => Promise<any>;
}

export interface ServerToolDef {
  name: string;
  description: string;
  parameters: ServerToolParam[];
  execute(params: Record<string, any>, context: ToolExecutionContext): Promise<string>;
}
