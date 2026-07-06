// Typed wrappers around the Wails-generated Go bindings. The generated files in
// ../wailsjs/ are produced by `wails dev` / `wails build` / `wails generate module`.
// The Go backend is serial-only; the material DB is parsed in the frontend (db.ts).
import {
  ListPorts,
  Connect,
  Disconnect,
  Send,
  Version,
  FlashFirmware,
  CheckUpdate,
  UploadDB,
  PullDBFromPrinter,
} from "../wailsjs/go/main/App";
import { EventsOn } from "../wailsjs/runtime/runtime";

import type { ErrorReply } from "@spoolid/core";

export type Cmd = Record<string, unknown>;
// Every device reply carries the ok envelope (spec/v2/PROTOCOL.md): success
// replies have ok: true, errors are ErrorReply (ok: false + error + code) — a
// discriminated union, so `if (!r.ok)` narrows. Callers pass the concrete
// success type: send<SpecReply>(...).
export interface Reply { ok: true }
export type Result<T> = T | ErrorReply;

export interface Release {
  version: string;
  tag: string;
  url: string;
  firmware: string;
  filesystem: string;
}

export interface PortInfo {
  name: string;
  label: string;
  isDevice: boolean;
}

export const listPorts = (): Promise<PortInfo[]> =>
  ListPorts() as Promise<PortInfo[]>;

export const connect = (port: string, baud: number): Promise<void> =>
  Connect(port, baud);

export const disconnect = (): Promise<void> => Disconnect();

export const send = <T extends Reply = Reply>(cmd: Cmd): Promise<Result<T>> =>
  Send(cmd as { [key: string]: any }) as Promise<Result<T>>;

// Desktop app version (for the firmware/desktop compatibility gate).
export const appVersion = (): Promise<string> => Version();

// Download a firmware/filesystem image and relay it to the device over serial.
export const flashFirmware = (url: string, filesystem: boolean): Promise<void> =>
  FlashFirmware(url, filesystem);

// Subscribe to OTA progress (0..1) emitted during flashFirmware.
export const onOtaProgress = (cb: (pct: number) => void): void => {
  EventsOn("ota:progress", cb);
};

// Query the GitHub Releases API for the latest release + asset URLs.
export const checkUpdate = (): Promise<Release> => CheckUpdate() as Promise<Release>;

// Pick a material_database.json and upload it to the device (returns its name,
// or "" if cancelled).
export const uploadDb = (): Promise<string> => UploadDB();

// Fetch the material DB from a printer host and push it to the device.
export const pullDb = (host: string): Promise<void> => PullDBFromPrinter(host);
