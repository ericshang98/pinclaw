// ── WebSocket protocol messages (companion app ↔ plugin) ──

export interface WsAuthMessage {
  type: "auth";
  deviceId: string;
  token: string;
}

export interface WsTextMessage {
  type: "text";
  content: string;
}

export interface WsPingMessage {
  type: "ping";
}

// ── Device Skills protocol ──

export interface DeviceToolDef {
  name: string;
  description: string;
  parameters: { name: string; type: string; required?: boolean; description?: string }[];
}

export interface WsDeviceToolsRegisterMessage {
  type: "device_tools_register";
  tools?: DeviceToolDef[];         // v1 legacy
  skills?: DeviceSkillManifest[];  // v2: full skill manifest
}

export interface DeviceSkillManifest {
  id: string;            // "device.calendar"
  name: string;          // "Calendar"
  enabled: boolean;
  permission: string;    // "authorized" | "denied" | "notDetermined" ...
  tools: DeviceToolDef[];
}

export interface PersistedDeviceState {
  version: 2;
  deviceId: string;
  lastSeen: string;      // ISO 8601
  connected: boolean;
  skills: DeviceSkillManifest[];
  contextHints: ContextHint[];
}

export interface WsToolResultMessage {
  type: "tool_result";
  callId: string;
  success: boolean;
  result?: string;
  error?: string;
}

// ── Context Hints (iPhone → Plugin, passive data sharing) ──

export interface ContextHint {
  skill: string;      // "calendar", "reminders" etc
  summary: string;    // human-readable summary injected into AI context
  updatedAt: string;  // ISO 8601
}

export interface WsContextUpdateMessage {
  type: "context_update";
  hints: ContextHint[];
}

// ── Interactive AI (Play button) ──

export interface WsPlayRequestMessage {
  type: "play_request";
  recentEntries: { type: "user" | "ai" | "interactive"; text: string; timestamp: string }[];
  currentTime: string;
}

// ── Media attachment protocol ──

export interface MediaAttachment {
  mediaType: string;    // MIME type: "image/jpeg", "application/pdf", "audio/wav"
  filename: string;
  url?: string;         // HTTP URL (preferred — from /api/media/upload)
  data?: string;        // base64-encoded (fallback)
}

export interface WsMediaMessage {
  type: "media_message";
  sessionKey?: string;
  text?: string;            // optional text caption
  attachments: MediaAttachment[];
}

export type WsInboundMessage =
  | WsAuthMessage
  | WsTextMessage
  | WsPingMessage
  | WsDeviceToolsRegisterMessage
  | WsToolResultMessage
  | WsContextUpdateMessage
  | WsPlayRequestMessage
  | WsMediaMessage
  | { type: "get_history"; sessionKey?: string; limit?: number }
  | { type: "get_sessions" };

export interface WsAuthOkMessage {
  type: "auth_ok";
  deviceId: string;
}

export interface WsAgentMessage {
  type: "agent_message";
  content: string;
  proactive: boolean;
  requestId?: string;
}

export interface WsAgentDeltaMessage {
  type: "agent_delta";
  content: string;
}

export interface WsAckMessage {
  type: "ack";
  sound?: string;
}

export interface WsErrorMessage {
  type: "error";
  message: string;
}

export interface WsPongMessage {
  type: "pong";
}

export interface WsToolCallMessage {
  type: "tool_call";
  callId: string;
  tool: string;
  params: Record<string, any>;
}

export interface WsUpdateAvailableMessage {
  type: "update_available";
  current: string;
  latest: string;
  update_type: "optional" | "recommended" | "required";
  update_command: string;
}

export interface WsInteractiveResponseMessage {
  type: "interactive_response";
  content: string;
  requestId: string;
}

export interface WsInteractiveErrorMessage {
  type: "interactive_error";
  message: string;
  requestId: string;
}

export type WsOutboundMessage =
  | WsAuthOkMessage
  | WsAgentMessage
  | WsAgentDeltaMessage
  | WsAckMessage
  | WsErrorMessage
  | WsPongMessage
  | WsToolCallMessage
  | WsUpdateAvailableMessage
  | WsInteractiveResponseMessage
  | WsInteractiveErrorMessage;

// ── Relay config (for connecting through Pinclaw Cloud relay) ──

export interface RelayConfig {
  enabled: boolean;
  token: string;
  url?: string; // Default: wss://api.pinclaw.ai
}

// ── Pinclaw account config (from openclaw.json channels.pinclaw) ──

export interface PinclawAccountConfig {
  enabled?: boolean;
  wsPort?: number;
  authToken?: string;
  relay?: RelayConfig;
}

export interface ResolvedPinclawAccount {
  accountId: string;
  enabled: boolean;
  wsPort: number;
  authToken: string;
  config: PinclawAccountConfig;
}
