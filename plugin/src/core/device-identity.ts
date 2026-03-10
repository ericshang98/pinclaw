import { generateKeyPairSync, sign, createHash, createPrivateKey } from "node:crypto";
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";
import { homedir } from "node:os";

export interface DeviceIdentity {
  version: number;
  deviceId: string;
  publicKeyB64: string;
  privateKeyDer: string;
}

const DEVICE_IDENTITY_PATH = join(homedir(), ".openclaw", "pinclaw-device-identity.json");

export function base64url(buf: Buffer | Uint8Array): string {
  return Buffer.from(buf).toString("base64url");
}

export function getOrCreateDeviceIdentity(): DeviceIdentity {
  try {
    const data = JSON.parse(readFileSync(DEVICE_IDENTITY_PATH, "utf-8"));
    if (data.version === 1 && data.deviceId && data.publicKeyB64 && data.privateKeyDer) {
      return data;
    }
  } catch {}

  const { publicKey, privateKey } = generateKeyPairSync("ed25519");
  const pubRaw = publicKey.export({ type: "spki", format: "der" }).subarray(-32);
  const deviceId = createHash("sha256").update(pubRaw).digest("hex");
  const publicKeyB64 = base64url(pubRaw);
  const privateKeyDer = privateKey.export({ type: "pkcs8", format: "der" }).toString("base64");

  const identity: DeviceIdentity = { version: 1, deviceId, publicKeyB64, privateKeyDer };
  try {
    mkdirSync(join(homedir(), ".openclaw"), { recursive: true });
    writeFileSync(DEVICE_IDENTITY_PATH, JSON.stringify(identity, null, 2));
  } catch {}

  return identity;
}

export function signDevicePayload(identity: DeviceIdentity, payload: string): string {
  const privKey = createPrivateKey({
    key: Buffer.from(identity.privateKeyDer, "base64"),
    format: "der",
    type: "pkcs8",
  });
  const signature = sign(null, Buffer.from(payload), privKey);
  return base64url(signature);
}
