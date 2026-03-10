import type { PinclawWsServer } from "./ws-server.js";

let runtime: any = null;
let wsServer: PinclawWsServer | null = null;

export function setPinclawRuntime(next: any): void {
  runtime = next;
}

export function getPinclawRuntime(): any {
  if (!runtime) throw new Error("Pinclaw runtime not initialized");
  return runtime;
}

export function setPinclawWsServer(server: PinclawWsServer | null): void {
  wsServer = server;
}

export function getPinclawWsServer(): PinclawWsServer | null {
  return wsServer;
}
