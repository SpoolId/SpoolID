// SpoolID desktop UI. Firmware is the source of truth: on connect we fetch the
// UI lists (swatches, sizes, log levels, baud rates) and current config over
// serial, then drive the same brand -> filament -> size -> color -> Write flow
// as the web UI. Serial calls are bound Go methods (see serial.ts).
import "@spoolid/core/tokens.css";
import "./theme.css";
import {
  checkCompat,
  cmpVer,
  colorName,
  interpretWriteStatus,
  isFilesystemImage,
  readToPrefill,
} from "@spoolid/core";
import type { SpecReply, StatusReply, ReadReply, ConfigReply } from "@spoolid/core";
import {
  appVersion,
  checkUpdate,
  connect,
  disconnect,
  flashFirmware,
  listPorts,
  onOtaProgress,
  pullDb,
  send,
  uploadDb,
} from "./serial";
import type { PortInfo, Release } from "./serial";
import { brands, byBrand, loadDb } from "./db";
import type { Material } from "./db";

const BAUD = 115200;

const $ = <T extends HTMLElement = HTMLElement>(id: string): T =>
  document.getElementById(id) as T;

// ---- state ----
let items: Material[] = [];
let swatches: string[] = []; // hex strings, no leading '#'
let color = "000000";
let polling: number | null = null;
let appVer = "";  // desktop version, for the compat gate
let latest: Release | null = null;  // latest GitHub release (auto-discovery)

// ---- element refs ----
const el = {
  port: $<HTMLSelectElement>("port"),
  rescan: $<HTMLButtonElement>("rescan"),
  connect: $<HTMLButtonElement>("connect"),
  disconnect: $<HTMLButtonElement>("disconnect"),
  connStatus: $("connStatus"),
  gateHint: $("gateHint"),
  spinner: $("spinner"),
  navWrite: $<HTMLButtonElement>("navWrite"),
  navConfig: $<HTMLButtonElement>("navConfig"),
  writeView: $("writeView"),
  configView: $("configView"),
  brand: $<HTMLSelectElement>("brand"),
  filament: $<HTMLSelectElement>("filament"),
  size: $<HTMLSelectElement>("size"),
  swatches: $("swatches"),
  chip: $("chip"),
  hex: $("hex"),
  custom: $<HTMLInputElement>("custom"),
  write: $<HTMLButtonElement>("write"),
  read: $<HTMLButtonElement>("read"),
  readout: $("readout"),
  writeStatus: $("writeStatus"),
  wifiSsid: $<HTMLInputElement>("wifiSsid"),
  wifiPass: $<HTMLInputElement>("wifiPass"),
  hostname: $<HTMLInputElement>("hostname"),
  apSsid: $<HTMLInputElement>("apSsid"),
  apPass: $<HTMLInputElement>("apPass"),
  logLevel: $<HTMLSelectElement>("logLevel"),
  baud: $<HTMLSelectElement>("baud"),
  save: $<HTMLButtonElement>("save"),
  configStatus: $("configStatus"),
  subDevice: $<HTMLButtonElement>("subDevice"),
  subDb: $<HTMLButtonElement>("subDb"),
  subOta: $<HTMLButtonElement>("subOta"),
  paneDevice: $("paneDevice"),
  paneDb: $("paneDb"),
  paneOta: $("paneOta"),
  uploadDb: $<HTMLButtonElement>("uploadDb"),
  dbHost: $<HTMLInputElement>("dbHost"),
  dbPull: $<HTMLButtonElement>("dbPull"),
  dbStatus: $("dbStatus"),
  compat: $("compat"),
  otaUrl: $<HTMLInputElement>("otaUrl"),
  otaTarget: $<HTMLSelectElement>("otaTarget"),
  flash: $<HTMLButtonElement>("flash"),
  otaBar: $("otaBar"),
  otaStatus: $("otaStatus"),
  checkUpdate: $<HTMLButtonElement>("checkUpdate"),
  updateInfo: $("updateInfo"),
  updateActions: $("updateActions"),
  flashLatestApp: $<HTMLButtonElement>("flashLatestApp"),
  flashLatestFs: $<HTMLButtonElement>("flashLatestFs"),
};

// Controls that require an open connection.
const gated = (): (HTMLInputElement | HTMLSelectElement | HTMLButtonElement)[] => [
  el.brand, el.filament, el.size, el.logLevel, el.baud, el.custom,
  el.write, el.read, el.save, el.uploadDb, el.dbHost, el.dbPull,
  el.wifiSsid, el.wifiPass, el.hostname, el.apSsid, el.apPass,
  el.otaUrl, el.otaTarget, el.flash, el.flashLatestApp, el.flashLatestFs,
];

// ---- helpers ----
function setConn(msg: string, err: boolean): void {
  el.connStatus.textContent = msg;
  el.connStatus.className = "conn " + (err ? "err" : "ok");
}

function setStatus(node: HTMLElement, msg: string, kind: "" | "ok" | "err" = ""): void {
  node.textContent = msg;
  node.className = "status" + (kind ? " " + kind : "");
}

function options(node: HTMLSelectElement, values: string[]): void {
  node.innerHTML = values.map((v) => `<option>${v}</option>`).join("");
}

function setEnabled(on: boolean): void {
  for (const node of gated()) node.disabled = !on;
}

// ---- view switch ----
function show(which: "write" | "config"): void {
  el.writeView.hidden = which !== "write";
  el.configView.hidden = which !== "config";
  el.navWrite.classList.toggle("active", which === "write");
  el.navConfig.classList.toggle("active", which === "config");
}

// ---- color ----
function colorLabel(hex: string): string {
  return `${colorName(hex) ?? "Custom"}  #${hex}`;
}

function setColor(hex: string): void {
  color = hex.toUpperCase();
  el.chip.style.background = "#" + color;
  el.hex.textContent = "#" + color;
  document.querySelectorAll<HTMLElement>(".swatch").forEach((s) =>
    s.classList.toggle("sel", !s.dataset.custom && s.dataset.hex === color));
}

function buildSwatches(): void {
  el.swatches.innerHTML = "";
  swatches.forEach((hex) => {
    const d = document.createElement("div");
    d.className = "swatch";
    d.dataset.hex = hex;
    d.style.background = "#" + hex;
    d.title = colorLabel(hex);
    d.onclick = () => selectSwatch(hex);
    el.swatches.appendChild(d);
  });
  // Trailing "custom" tile reveals the hex input.
  const c = document.createElement("div");
  c.className = "swatch";
  c.dataset.custom = "1";
  c.title = "Custom hex";
  c.textContent = "＋";
  c.style.display = "grid";
  c.style.placeItems = "center";
  c.style.border = "2px dashed var(--border)";
  c.style.color = "var(--muted)";
  c.onclick = () => selectCustom();
  el.swatches.appendChild(c);
}

function selectSwatch(hex: string): void {
  el.custom.hidden = true;
  setColor(hex);
}

function selectCustom(): void {
  el.custom.hidden = false;
  document.querySelectorAll<HTMLElement>(".swatch").forEach((s) => s.classList.remove("sel"));
  el.custom.focus();
  applyCustom();
}

function applyCustom(): void {
  const hex = el.custom.value.trim().replace(/^#/, "");
  if (/^[0-9a-fA-F]{6}$/.test(hex)) setColor(hex);
}

// ---- device spec + config ----
function applySpec(spec: SpecReply): void {
  swatches = spec.colorSwatches ?? [];
  options(el.size, spec.weightLabels ?? []);
  options(el.logLevel, spec.logLevels ?? []);
  options(el.baud, (spec.baudRates ?? []).map(String));
  buildSwatches();
  if (swatches.length) selectSwatch(swatches[0]);
  // Shared compatibility gate: protocol match on v2+ firmware, major.minor on 1.x.
  const compat = checkCompat(appVer, spec);
  el.compat.hidden = compat.compatible;
  if (!compat.compatible) el.compat.textContent = `⚠ ${compat.message}`;
}

function applyConfig(cfg: ConfigReply): void {
  el.wifiSsid.value = cfg.wifiSsid;
  el.hostname.value = cfg.hostname;
  el.apSsid.value = cfg.apSsid;
  if (cfg.logLevel != null) el.logLevel.selectedIndex = Number(cfg.logLevel);
  if (cfg.baud != null) el.baud.value = String(cfg.baud);
}

// ---- brand/filament ----
function fillBrands(): void {
  options(el.brand, brands(items));
  fillFilaments();
}

function fillFilaments(): void {
  const list = byBrand(items, el.brand.value);
  el.filament.innerHTML = list
    .map((m) => `<option value="${m.id}">${m.name} (${m.type})</option>`)
    .join("");
}

// ---- connection ----
type State = "disconnected" | "connecting" | "connected";
let pollTimer: number | null = null;
let autoConnect = true; // re-armed when no device is present (unplug -> replug)

function setState(s: State): void {
  document.body.dataset.state = s;
  el.disconnect.hidden = s !== "connected";
  el.spinner.hidden = s !== "connecting";
  const busy = s === "connecting";
  el.connect.disabled = busy;
  el.port.disabled = busy;
  el.rescan.disabled = busy;
}

// Refresh the port list, keeping the current pick (or preferring a device), and
// update the gate hint. Returns the discovered ports.
async function refreshPorts(): Promise<PortInfo[]> {
  let ports: PortInfo[];
  try {
    ports = await listPorts();
  } catch {
    return [];
  }
  const prev = el.port.value;
  el.port.innerHTML = ports
    .map((p) => `<option value="${p.name}">${p.label}</option>`).join("");
  const device = ports.find((p) => p.isDevice);
  if (ports.some((p) => p.name === prev)) el.port.value = prev;
  else if (device) el.port.value = device.name;

  el.gateHint.textContent = ports.length === 0
    ? "Scanning for a SpoolID device… plug one in over USB."
    : device
      ? (autoConnect ? "Found a SpoolID device — connecting…" : "SpoolID device found — click Connect.")
      : "Select a serial port, then Connect.";
  return ports;
}

// While disconnected, poll for ports and auto-connect a lone SpoolID device.
// autoConnect re-arms once no device is present, so a manual Disconnect doesn't
// immediately reconnect, but unplugging + replugging does.
async function poll(): Promise<void> {
  if (document.body.dataset.state !== "disconnected") return;
  const ports = await refreshPorts();
  const devices = ports.filter((p) => p.isDevice);
  if (devices.length === 0) {
    autoConnect = true;
    return;
  }
  if (devices.length === 1 && autoConnect) {
    el.port.value = devices[0].name;
    await doConnect();
  }
}

function startPolling(): void {
  if (pollTimer === null) pollTimer = window.setInterval(() => void poll(), 2000);
}

function stopPolling(): void {
  if (pollTimer !== null) { clearInterval(pollTimer); pollTimer = null; }
}

async function doConnect(): Promise<void> {
  const port = el.port.value;
  if (!port) return setConn("pick a serial port", true);
  stopPolling();
  setState("connecting");
  setConn("", false);
  try {
    await connect(port, BAUD);
  } catch (e) {
    setState("disconnected");
    setConn(`open failed: ${e}`, true);
    startPolling();
    return;
  }
  try {
    const spec = await send<SpecReply>({ cmd: "getspec" });
    if (!spec.ok) throw new Error(spec.error);
    applySpec(spec);
    const cfg = await send<ConfigReply>({ cmd: "getconfig" });
    if (!cfg.ok) throw new Error(cfg.error);
    applyConfig(cfg);
    fillBrands();
  } catch (e) {
    setState("disconnected");
    setConn(`serial error: ${e}`, true);
    startPolling();
    return;
  }
  setEnabled(true);
  setState("connected");
}

async function doDisconnect(): Promise<void> {
  try {
    await disconnect();
  } catch {
    /* already gone */
  }
  autoConnect = false; // don't immediately reconnect the same device
  setEnabled(false);
  el.compat.hidden = true;
  setState("disconnected");
  setConn("", false);
  startPolling();
  void refreshPorts();
}

// ---- write / read ----
async function doWrite(): Promise<void> {
  const materialId = el.filament.value;
  const weight = el.size.value;
  if (!materialId || !weight) {
    return setStatus(el.writeStatus, "pick a filament and size first", "err");
  }
  el.write.disabled = true;
  setStatus(el.writeStatus, "staging — tap a tag on the reader…");
  try {
    const resp = await send({ cmd: "write", materialId, color, weight });
    if (!resp.ok) {
      setStatus(el.writeStatus, `error: ${resp.error}`, "err");
      el.write.disabled = false;
      return;
    }
    if (polling == null) polling = window.setInterval(pollStatus, 700);
  } catch (e) {
    setStatus(el.writeStatus, `serial error: ${e}`, "err");
    el.write.disabled = false;
  }
}

async function pollStatus(): Promise<void> {
  try {
    const st = await send<StatusReply>({ cmd: "status" });
    const outcome = interpretWriteStatus(st.ok ? st : {});
    if (!outcome.done) return;
    if (polling != null) { clearInterval(polling); polling = null; }
    el.write.disabled = false;
    setStatus(el.writeStatus, outcome.message ?? "", outcome.ok ? "ok" : "err");
  } catch (e) {
    if (polling != null) { clearInterval(polling); polling = null; }
    el.write.disabled = false;
    setStatus(el.writeStatus, `serial error: ${e}`, "err");
  }
}

async function doRead(): Promise<void> {
  el.read.disabled = true;
  setStatus(el.writeStatus, "reading — tap a tag on the reader…");
  try {
    const j = await send<ReadReply>({ cmd: "read" });
    if (!j.ok) {
      setStatus(el.writeStatus, `read failed: ${j.error}`, "err");
      return;
    }
    applyRead(j);
    setStatus(el.writeStatus, "tag read ✓ — review values and Write", "ok");
  } catch (e) {
    setStatus(el.writeStatus, `serial error: ${e}`, "err");
  } finally {
    el.read.disabled = false;
  }
}

function applyRead(j: ReadReply): void {
  const pre = readToPrefill(j, items);
  if (pre.material) {
    el.brand.value = pre.material.brand;
    fillFilaments();
    el.filament.value = pre.material.id;
  }
  if (pre.weight) el.size.value = pre.weight;
  if (pre.colorHex) {
    if (swatches.some((s) => s.toUpperCase() === pre.colorHex)) {
      selectSwatch(pre.colorHex);
    } else {
      selectCustom();
      el.custom.value = pre.colorHex;
      applyCustom();
    }
  }
  el.readout.textContent = pre.label;
}

// ---- config ----
async function doSave(): Promise<void> {
  const cmd: Record<string, unknown> = {
    cmd: "setconfig",
    wifiSsid: el.wifiSsid.value,
    hostname: el.hostname.value,
    apSsid: el.apSsid.value,
    logLevel: el.logLevel.selectedIndex,
  };
  if (el.baud.value) cmd.baud = Number(el.baud.value);
  if (el.wifiPass.value) cmd.wifiPass = el.wifiPass.value;
  if (el.apPass.value) cmd.apPass = el.apPass.value;
  await simple(cmd, el.configStatus, "saved ✓ reboot device to apply WiFi", "save failed");
}

// ---- config sub-tabs (Device / Material database / OTA) ----
function showConfigPane(which: "device" | "db" | "ota"): void {
  el.paneDevice.hidden = which !== "device";
  el.paneDb.hidden = which !== "db";
  el.paneOta.hidden = which !== "ota";
  el.subDevice.classList.toggle("active", which === "device");
  el.subDb.classList.toggle("active", which === "db");
  el.subOta.classList.toggle("active", which === "ota");
}

// ---- material database: upload a file, or pull from a printer ----
async function doUploadDb(): Promise<void> {
  el.uploadDb.disabled = true;
  setStatus(el.dbStatus, "uploading to device…");
  try {
    const name = await uploadDb();
    if (name) setStatus(el.dbStatus, `uploaded ${name} ✓`, "ok");
    else setStatus(el.dbStatus, ""); // cancelled
  } catch (e) {
    setStatus(el.dbStatus, `upload failed: ${e}`, "err");
  } finally {
    el.uploadDb.disabled = false;
  }
}

async function doPullDb(): Promise<void> {
  const host = el.dbHost.value.trim();
  if (!host) return setStatus(el.dbStatus, "enter the printer host/IP", "err");
  el.dbPull.disabled = true;
  setStatus(el.dbStatus, "fetching from printer + uploading…");
  try {
    await pullDb(host);
    setStatus(el.dbStatus, "pulled from printer ✓", "ok");
  } catch (e) {
    setStatus(el.dbStatus, `pull failed: ${e}`, "err");
  } finally {
    el.dbPull.disabled = false;
  }
}

async function simple(cmd: Record<string, unknown>, node: HTMLElement, okMsg: string, failMsg: string): Promise<void> {
  try {
    const resp = await send(cmd);
    if (resp.ok) setStatus(node, okMsg, "ok");
    else setStatus(node, `${failMsg}: ${resp.error}`, "err");
  } catch (e) {
    setStatus(node, `serial error: ${e}`, "err");
  }
}

// ---- firmware OTA (desktop downloads an image + relays it to the device) ----
let flashing = false;

function setProgress(pct: number): void {
  const wrap = el.otaBar.parentElement as HTMLElement;
  wrap.hidden = pct <= 0 || pct >= 1;
  el.otaBar.style.width = `${Math.round(pct * 100)}%`;
}

async function runFlash(url: string, fs: boolean): Promise<void> {
  if (flashing) return;
  if (!url) return setStatus(el.otaStatus, "no image URL", "err");
  flashing = true;
  el.flash.disabled = true;
  setProgress(0.001);
  setStatus(el.otaStatus, "downloading + flashing — do not unplug the device…");
  try {
    await flashFirmware(url, fs);
    setStatus(el.otaStatus, "flashed ✓ device rebooting — reconnect when it's back", "ok");
    setConn("device rebooting after update", true);
    setEnabled(false);
  } catch (e) {
    setStatus(el.otaStatus, `flash failed: ${e}`, "err");
  } finally {
    flashing = false;
    el.flash.disabled = false;
    setProgress(0);
  }
}

function doFlash(): void {
  void runFlash(el.otaUrl.value.trim(), el.otaTarget.value === "fs");
}

// ---- update auto-discovery (GitHub Releases) ----
async function doCheckUpdate(): Promise<void> {
  el.checkUpdate.disabled = true;
  setStatus(el.updateInfo, "checking GitHub for the latest release…");
  try {
    latest = await checkUpdate();
    const newer = appVer && latest.version && cmpVer(latest.version, appVer) > 0;
    const tag = latest.tag || `v${latest.version}`;
    setStatus(
      el.updateInfo,
      newer ? `${tag} available (you have v${appVer})` : `up to date (latest ${tag})`,
      newer ? "ok" : "",
    );
    el.updateActions.hidden = false;
    el.flashLatestApp.disabled = !latest.firmware;
    el.flashLatestFs.disabled = !latest.filesystem;
  } catch (e) {
    latest = null;
    el.updateActions.hidden = true;
    setStatus(el.updateInfo, `update check failed: ${e}`, "err");
  } finally {
    el.checkUpdate.disabled = false;
  }
}

// ---- boot ----
async function boot(): Promise<void> {
  el.rescan.onclick = () => void refreshPorts();
  el.connect.onclick = () => void doConnect();
  el.disconnect.onclick = () => void doDisconnect();
  el.navWrite.onclick = () => show("write");
  el.navConfig.onclick = () => show("config");
  el.brand.onchange = fillFilaments;
  el.custom.oninput = applyCustom;
  el.write.onclick = () => void doWrite();
  el.read.onclick = () => void doRead();
  el.save.onclick = () => void doSave();
  el.subDevice.onclick = () => showConfigPane("device");
  el.subDb.onclick = () => showConfigPane("db");
  el.subOta.onclick = () => showConfigPane("ota");
  el.uploadDb.onclick = () => void doUploadDb();
  el.dbPull.onclick = () => void doPullDb();
  el.flash.onclick = () => doFlash();
  // Auto-pick the image type from the URL (littlefs.bin -> fs, else app).
  el.otaUrl.oninput = () => {
    const u = el.otaUrl.value.trim();
    if (u) el.otaTarget.value = isFilesystemImage(u) ? "fs" : "app";
  };
  el.checkUpdate.onclick = () => void doCheckUpdate();
  el.flashLatestApp.onclick = () => void runFlash(latest?.firmware ?? "", false);
  el.flashLatestFs.onclick = () => void runFlash(latest?.filesystem ?? "", true);
  onOtaProgress((pct) => setProgress(pct));

  setEnabled(false);
  show("write");
  showConfigPane("device");
  setState("disconnected");

  appVer = await appVersion().catch(() => "");

  try {
    items = loadDb();
  } catch (e) {
    setConn(`failed to load material DB: ${e}`, true);
  }

  // Discover ports, auto-connect a lone device, and keep scanning otherwise.
  await poll();
  startPolling();
}

void boot();
