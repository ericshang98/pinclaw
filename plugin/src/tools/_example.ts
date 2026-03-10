/**
 * Example server tool — demonstrates how to create a tool for the Pinclaw Tool Registry.
 *
 * How to contribute a new server tool:
 *
 * 1. Create a new .ts file in this directory (plugin/src/tools/)
 * 2. Export a default object implementing the ServerToolDef interface
 * 3. The tool will be auto-discovered and registered on startup
 *
 * Files starting with "_" (like this one) are excluded from auto-discovery.
 * To activate this example, rename it to "system-info.ts".
 */

import type { ServerToolDef } from "./types.js";

const tool: ServerToolDef = {
  name: "system_info",
  description: "Returns basic system information (uptime, platform, memory usage)",
  parameters: [],
  async execute(_params, context) {
    const uptime = Math.floor(process.uptime());
    const mem = process.memoryUsage();
    const info = {
      platform: process.platform,
      arch: process.arch,
      nodeVersion: process.version,
      uptimeSeconds: uptime,
      memoryMB: Math.round(mem.rss / 1024 / 1024),
      deviceId: context.deviceId,
    };
    context.log.info(`[system_info] Returning system info for device ${context.deviceId}`);
    return JSON.stringify(info);
  },
};

export default tool;
