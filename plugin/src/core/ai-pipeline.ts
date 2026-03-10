import type { Logger } from "./utils.js";
import type { DeviceManager } from "./device-manager.js";
import type { GatewayRpc } from "./gateway-rpc.js";
import type { ToolRegistry } from "../tools/registry.js";

export function parseDeviceToolCall(text: string): { toolName: string; params: Record<string, any> } | null {
  const match = text.match(/<device_tool\s+name="([^"]+)"\s+params='([^']*)'\s*\/>/);
  if (!match) return null;
  try {
    return { toolName: match[1], params: JSON.parse(match[2] || "{}") };
  } catch {
    return { toolName: match[1], params: {} };
  }
}

export function parseServerToolCall(text: string): { toolName: string; params: Record<string, any> } | null {
  const match = text.match(/<server_tool\s+name="([^"]+)"\s+params='([^']*)'\s*\/>/);
  if (!match) return null;
  try {
    return { toolName: match[1], params: JSON.parse(match[2] || "{}") };
  } catch {
    return { toolName: match[1], params: {} };
  }
}

export async function callAgent(
  deviceId: string,
  text: string,
  deps: {
    gatewayRpc: GatewayRpc;
    deviceManager: DeviceManager;
    toolRegistry: ToolRegistry;
    log: Logger;
    mediaPaths?: string[];
  },
): Promise<{ content?: string; error?: string }> {
  const { gatewayRpc, deviceManager, toolRegistry, log, mediaPaths } = deps;

  if (!gatewayRpc.isReady) {
    return { error: "Gateway not connected" };
  }

  // Build message with media references for Gateway media understanding
  let message = text;
  if (mediaPaths?.length) {
    const mediaRefs = mediaPaths.map(p => `MEDIA:${p}`).join("\n");
    message = mediaRefs + (text ? `\n${text}` : "");
    log.info(`[ai-pipeline] Sending message with ${mediaPaths.length} media attachment(s)`);
  }

  let aiContent: string;
  try {
    aiContent = await gatewayRpc.chatSend("main", message);
  } catch (err: any) {
    return { error: `Gateway RPC failed: ${err.message}` };
  }

  if (!aiContent) return { error: "Empty AI response" };

  // Tool call loop (max 3 rounds) — supports both device tools and server tools
  const deviceToolsList = deviceManager.getDeviceTools(deviceId);
  const hasDeviceTools = deviceToolsList.length > 0;
  const hasServerTools = toolRegistry.getAll().length > 0;

  if (hasDeviceTools || hasServerTools) {
    let currentContent = aiContent;
    for (let round = 0; round < 3; round++) {
      // 1. Check for device tool call
      const deviceCall = parseDeviceToolCall(currentContent);
      if (deviceCall) {
        log.info(`AI requested device tool: ${deviceCall.toolName} (round ${round + 1})`);

        let toolResultText: string;
        try {
          const result = await deviceManager.callDeviceTool(deviceId, deviceCall.toolName, deviceCall.params);
          toolResultText = result.success
            ? (result.result ?? "Success")
            : `Error: ${result.error ?? "Unknown error"}`;
        } catch (err: any) {
          toolResultText = `Error: ${err.message}`;
        }

        const followUp = `[Tool result for ${deviceCall.toolName}]: ${toolResultText}\n\nBased on this result, respond to the user.`;
        try {
          currentContent = await gatewayRpc.chatSend("main", followUp);
        } catch {
          currentContent = toolResultText;
          break;
        }
        continue;
      }

      // 2. Check for server tool call
      const serverCall = parseServerToolCall(currentContent);
      if (serverCall) {
        log.info(`AI requested server tool: ${serverCall.toolName} (round ${round + 1})`);

        let toolResultText: string;
        try {
          toolResultText = await toolRegistry.execute(serverCall.toolName, serverCall.params, {
            deviceId,
            log,
            gatewayRpc: (method, params) => gatewayRpc.rpc(method, params),
          });
        } catch (err: any) {
          toolResultText = `Error: ${err.message}`;
        }

        const followUp = `[Tool result for ${serverCall.toolName}]: ${toolResultText}\n\nBased on this result, respond to the user.`;
        try {
          currentContent = await gatewayRpc.chatSend("main", followUp);
        } catch {
          currentContent = toolResultText;
          break;
        }
        continue;
      }

      // 3. No tool calls found — done
      break;
    }
    return { content: currentContent };
  }

  return { content: aiContent };
}
