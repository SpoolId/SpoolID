// SpoolID device web UI — config page: Device / Material database / OTA sub-tabs.
// Firmware is the source of truth: the selects come from /api/spec. Shared
// logic lives in @spoolid/core; this file is DOM glue only.
import "@spoolid/core/tokens.css";
import "./style.css";
import { isFilesystemImage } from "@spoolid/core";

const $ = <T extends HTMLElement = HTMLElement>(id: string): T =>
  document.getElementById(id) as T;
const sel = (id: string) => $<HTMLSelectElement>(id);
const inp = (id: string) => $<HTMLInputElement>(id);
const btn = (id: string) => $<HTMLButtonElement>(id);

function setStatus(node: HTMLElement, msg: string, err = false): void {
  node.textContent = msg;
  node.className = "status" + (err ? " err" : msg ? " ok" : "");
}

const statusEl = () => $("status");
const dbStatusEl = () => $("dbStatus");
const otaStatusEl = () => $("otaStatus");

// ---- sub-tabs ----
function showPane(which: "device" | "db" | "ota"): void {
  $("paneDevice").hidden = which !== "device";
  $("paneDb").hidden = which !== "db";
  $("paneOta").hidden = which !== "ota";
  btn("subDevice").classList.toggle("active", which === "device");
  btn("subDb").classList.toggle("active", which === "db");
  btn("subOta").classList.toggle("active", which === "ota");
}
btn("subDevice").onclick = () => showPane("device");
btn("subDb").onclick = () => showPane("db");
btn("subOta").onclick = () => showPane("ota");
showPane("device");

// ---- device settings ----
async function loadSpec(): Promise<void> {
  const spec = await (await fetch("/api/spec")).json();
  sel("logLevel").innerHTML = (spec.logLevels || [])
    .map((l: string, i: number) => `<option value="${i}">${l}</option>`).join("");
  sel("baud").innerHTML = (spec.baudRates || [])
    .map((b: number) => `<option>${b}</option>`).join("");
}

async function load(): Promise<void> {
  const c = await (await fetch("/api/config")).json();
  inp("wifiSsid").value = c.wifiSsid || "";
  inp("hostname").value = c.hostname || "";
  inp("apSsid").value = c.apSsid || "";
  sel("logLevel").value = String(c.logLevel ?? 3);
  sel("baud").value = String(c.baud ?? 115200);
}

btn("save").onclick = async () => {
  const body: Record<string, unknown> = {
    apSsid: inp("apSsid").value,
    wifiSsid: inp("wifiSsid").value,
    hostname: inp("hostname").value,
    logLevel: Number(sel("logLevel").value),
    baud: Number(sel("baud").value),
  };
  if (inp("wifiPass").value) body.wifiPass = inp("wifiPass").value;
  if (inp("apPass").value) body.apPass = inp("apPass").value;
  const j = await (await fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  })).json();
  setStatus(statusEl(), j.ok ? "saved ✓ reboot to apply WiFi changes" : "save failed", !j.ok);
};

// ---- material database upload ----
btn("upload").onclick = async () => {
  const f = inp("dbfile").files?.[0];
  if (!f) return setStatus(dbStatusEl(), "pick a file first", true);
  setStatus(dbStatusEl(), "gzipping…", false);
  // Compress in the browser; the ESP32 has no runtime gzip compressor.
  const gz = await new Response(
    f.stream().pipeThrough(new CompressionStream("gzip"))
  ).blob();
  const res = await fetch("/api/db", {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: gz,
  });
  setStatus(dbStatusEl(), (await res.json()).ok ? "DB uploaded ✓" : "upload failed", !res.ok);
};

// ---- firmware OTA (upload a .bin; the device self-flashes via Update) ----
// Auto-pick the image type from the file name (littlefs.bin -> fs, else app).
inp("otaFile").onchange = () => {
  const name = inp("otaFile").files?.[0]?.name || "";
  if (name) sel("otaTarget").value = isFilesystemImage(name) ? "fs" : "app";
};

// XHR (not fetch) for upload progress. MD5 is optional firmware-side, so the
// browser path skips it (SubtleCrypto has no MD5).
btn("flash").onclick = () => {
  const f = inp("otaFile").files?.[0];
  if (!f) return setStatus(otaStatusEl(), "pick a .bin file first", true);
  const target = sel("otaTarget").value;
  const prog = $<HTMLProgressElement>("otaProg");
  prog.hidden = false;
  prog.value = 0;
  btn("flash").disabled = true;
  setStatus(otaStatusEl(), "uploading + flashing — do not power off the device…", false);

  const xhr = new XMLHttpRequest();
  xhr.open("POST", `/api/ota?target=${target}`);
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) prog.value = (e.loaded / e.total) * 100;
  };
  xhr.onload = () => {
    btn("flash").disabled = false;
    prog.hidden = true;
    let ok = false, error = "";
    try { const j = JSON.parse(xhr.responseText); ok = j.ok; error = j.error || ""; } catch { /* non-JSON */ }
    if (ok) setStatus(otaStatusEl(), "flashed ✓ device rebooting — reconnect when it's back", false);
    else setStatus(otaStatusEl(), `flash failed: ${error || "HTTP " + xhr.status}`, true);
  };
  xhr.onerror = () => {
    btn("flash").disabled = false;
    prog.hidden = true;
    setStatus(otaStatusEl(), "upload failed (connection lost?)", true);
  };
  xhr.send(f);
};

void loadSpec().then(load);

export {};
