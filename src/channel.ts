import { join } from "node:path";
import { homedir } from "node:os";
import { getPinclawWsServer, setPinclawWsServer } from "./runtime.js";
import { PinclawWsServer } from "./ws-server.js";
import { RelayClient } from "./relay-client.js";
import type { ResolvedPinclawAccount, PinclawAccountConfig, RelayConfig } from "./types.js";

const DEFAULT_WS_PORT = 18790;
const DEFAULT_ACCOUNT_ID = "default";

// ── Delivery tag parsing ──
// Cron messages can carry a [DELIVERY:TYPE] prefix to control routing.
// Tags are stripped before forwarding to the device.

type DeliveryType = "notify" | "silent" | "result";

const DELIVERY_TAG_RE = /^\[DELIVERY:(NOTIFY|SILENT|RESULT)\]\s*/i;
const NO_REPLY_RE = /\[NO_REPLY\]/i;

function parseDeliveryTag(text: string): { type: DeliveryType; text: string; hasNoReply: boolean } {
  const m = text.match(DELIVERY_TAG_RE);
  if (!m) return { type: "notify", text, hasNoReply: false };
  const type = m[1].toLowerCase() as DeliveryType;
  const stripped = text.slice(m[0].length);
  return { type, text: stripped, hasNoReply: NO_REPLY_RE.test(stripped) };
}

export const pinclawPlugin = {
  id: "pinclaw" as const,

  meta: {
    id: "pinclaw" as const,
    label: "Pinclaw",
    selectionLabel: "Pinclaw Hardware Clip",
    docsPath: "channels/pinclaw",
    blurb: "Hardware voice interface for OpenClaw — wearable clip with mic, speaker, and button",
    aliases: ["hardware", "clip"],
  },

  capabilities: {
    chatTypes: ["direct" as const],
  },

  reload: { configPrefixes: ["channels.pinclaw"] },

  // ── Config adapter ──

  config: {
    listAccountIds: (cfg: any): string[] => {
      const section = cfg.channels?.pinclaw;
      if (!section) return [];
      if (section.accounts) return Object.keys(section.accounts);
      // Top-level config counts as default account
      if (section.enabled !== false) return [DEFAULT_ACCOUNT_ID];
      return [];
    },

    resolveAccount: (cfg: any, accountId?: string | null): ResolvedPinclawAccount => {
      const id = accountId ?? DEFAULT_ACCOUNT_ID;
      const section = cfg.channels?.pinclaw;
      const acct: PinclawAccountConfig =
        id !== DEFAULT_ACCOUNT_ID
          ? section?.accounts?.[id] ?? {}
          : section ?? {};

      return {
        accountId: id,
        enabled: acct.enabled !== false,
        wsPort: acct.wsPort ?? DEFAULT_WS_PORT,
        authToken: acct.authToken ?? "",
        config: acct,
      };
    },

    defaultAccountId: (): string => DEFAULT_ACCOUNT_ID,

    isConfigured: (account: ResolvedPinclawAccount): boolean =>
      Boolean(account.authToken?.trim()),

    isEnabled: (account: ResolvedPinclawAccount): boolean =>
      account.enabled,

    describeAccount: (account: ResolvedPinclawAccount) => ({
      accountId: account.accountId,
      enabled: account.enabled,
      configured: Boolean(account.authToken?.trim()),
    }),
  },

  // ── Outbound adapter (agent → hardware device) ──
  // OpenClaw's announce/cron pipeline calls sendText/sendMedia to deliver messages.

  outbound: {
    deliveryMode: "direct" as const,
    textChunkLimit: 4096,

    sendText: async (ctx: { to: string; text: string; [k: string]: any }) => {
      const server = getPinclawWsServer();
      const msgId = `pinclaw-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
      const deviceId = ctx.to || "pinclaw";
      const delivery = parseDeliveryTag(ctx.text ?? "");

      if (delivery.type === "silent") {
        console.log(`[pinclaw outbound] SILENT — skipped sendText to=${deviceId} text=${delivery.text.slice(0, 80)}...`);
        return { channel: "pinclaw" as const, messageId: msgId };
      }
      if (delivery.type === "result" && delivery.hasNoReply) {
        console.log(`[pinclaw outbound] RESULT+NO_REPLY — skipped sendText to=${deviceId}`);
        return { channel: "pinclaw" as const, messageId: msgId };
      }

      console.log(`[pinclaw outbound] sendText (${delivery.type}) to=${deviceId} text=${delivery.text.slice(0, 80)}...`);
      if (server) {
        await server.sendToDevice(deviceId, delivery.text);
      } else {
        console.log(`[pinclaw outbound] NO server instance — message lost!`);
      }
      return { channel: "pinclaw" as const, messageId: msgId };
    },

    sendMedia: async (ctx: { to: string; text: string; mediaUrl?: string; [k: string]: any }) => {
      const server = getPinclawWsServer();
      const msgId = `pinclaw-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
      const deviceId = ctx.to || "pinclaw";
      const delivery = parseDeliveryTag(ctx.text ?? "");

      if (delivery.type === "silent") {
        console.log(`[pinclaw outbound] SILENT — skipped sendMedia to=${deviceId}`);
        return { channel: "pinclaw" as const, messageId: msgId };
      }
      if (delivery.type === "result" && delivery.hasNoReply) {
        console.log(`[pinclaw outbound] RESULT+NO_REPLY — skipped sendMedia to=${deviceId}`);
        return { channel: "pinclaw" as const, messageId: msgId };
      }

      const text = ctx.mediaUrl ? `${delivery.text}\n[media: ${ctx.mediaUrl}]` : delivery.text;
      if (server) {
        await server.sendToDevice(deviceId, text);
      }
      return { channel: "pinclaw" as const, messageId: msgId };
    },
  },

  // ── Status adapter ──

  status: {
    defaultRuntime: {
      accountId: DEFAULT_ACCOUNT_ID,
      running: false,
      lastError: null,
    },

    buildAccountSnapshot: ({ account, runtime }: any) => ({
      accountId: account.accountId,
      enabled: account.enabled,
      configured: Boolean(account.authToken?.trim()),
      running: runtime?.running ?? false,
      lastError: runtime?.lastError ?? null,
    }),
  },

  // ── Security adapter ──

  security: {
    resolveDmPolicy: ({ account }: { account: ResolvedPinclawAccount }) => ({
      policy: "open" as const,
      allowFrom: ["*"],
      allowFromPath: "channels.pinclaw.dm.",
      approveHint: "Pinclaw hardware device",
    }),
  },

  // ── Gateway adapter (lifecycle) ──

  gateway: {
    startAccount: async (ctx: any): Promise<void> => {
      const account: ResolvedPinclawAccount = ctx.account;
      const gatewayPort = ctx.cfg.gateway?.port ?? 18789;
      const gatewayToken =
        ctx.cfg.gateway?.auth?.token ?? "";

      const server = new PinclawWsServer({
        port: account.wsPort,
        authToken: account.authToken,
        gatewayUrl: `http://127.0.0.1:${gatewayPort}`,
        gatewayToken,
        abortSignal: ctx.abortSignal,
        log: ctx.log
          ? {
              info: (...args: any[]) => ctx.log.info("[pinclaw]", ...args),
              warn: (...args: any[]) => ctx.log.warn("[pinclaw]", ...args),
              error: (...args: any[]) => ctx.log.error("[pinclaw]", ...args),
            }
          : undefined,
      });

      await server.start();
      setPinclawWsServer(server);

      ctx.setStatus({
        accountId: account.accountId,
        running: true,
      });

      ctx.log?.info(
        `Pinclaw WebSocket server started on port ${account.wsPort}`,
      );

      // Start relay client if configured
      let relayClient: RelayClient | null = null;
      const relayConfig: RelayConfig | undefined =
        account.config.relay ??
        (process.env.PINCLAW_RELAY_TOKEN
          ? { enabled: true, token: process.env.PINCLAW_RELAY_TOKEN, url: process.env.PINCLAW_RELAY_URL }
          : undefined);

      if (relayConfig?.enabled && relayConfig?.token) {
        const relayUrl = relayConfig.url || "wss://api.pinclaw.ai";
        relayClient = new RelayClient({
          relayToken: relayConfig.token,
          relayUrl,
          localPluginPort: account.wsPort,
          localAuthToken: account.authToken,
          log: ctx.log
            ? {
                info: (...args: any[]) => ctx.log.info("[relay]", ...args),
                warn: (...args: any[]) => ctx.log.warn("[relay]", ...args),
                error: (...args: any[]) => ctx.log.error("[relay]", ...args),
              }
            : undefined,
        });
        await relayClient.start();
        ctx.log?.info(`Relay client started → ${relayUrl}`);
      }

      // Block until abort signal fires (keeps gateway alive)
      return new Promise<void>((resolve) => {
        ctx.abortSignal.addEventListener(
          "abort",
          () => {
            relayClient?.stop();
            server.stop();
            setPinclawWsServer(null);
            ctx.setStatus({
              accountId: account.accountId,
              running: false,
            });
            resolve();
          },
          { once: true },
        );
      });
    },
  },
};
