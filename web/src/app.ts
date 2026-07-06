// SpoolID device web UI — main page (brand -> filament -> size -> color -> Write).
// Firmware is the source of truth: constants come from /api/spec, data from
// /api/db. Shared parsing/prefill/status logic lives in @spoolid/core; this file
// is DOM glue only.
import "@spoolid/core/tokens.css";
import "./style.css";
import {
  brands,
  byBrand,
  interpretWriteStatus,
  normalizeHex,
  parseMaterialDb,
  readToPrefill,
  withHash,
} from "@spoolid/core";
import type { Material, ReadReply, SpecReply, StatusReply } from "@spoolid/core";

const $ = <T extends HTMLElement = HTMLElement>(id: string): T =>
  document.getElementById(id) as T;
const sel = (id: string) => $<HTMLSelectElement>(id);
const inp = (id: string) => $<HTMLInputElement>(id);
const btn = (id: string) => $<HTMLButtonElement>(id);

function setStatus(msg: string, err = false): void {
  const s = $("status");
  s.textContent = msg;
  s.className = "status" + (err ? " err" : msg ? " ok" : "");
}

let items: Material[] = [];
let swatches: string[] = []; // hex strings with a leading '#'
let color = "#1200F6";
let polling: number | null = null;

function setColor(hex: string): void {
  color = hex.toUpperCase();
  $("hex").textContent = color;
  $("chip").style.background = color;
  document.querySelectorAll<HTMLElement>(".swatch").forEach((s) =>
    s.classList.toggle("sel", s.dataset.hex === color));
}

function buildSwatches(): void {
  const wrap = $("swatches");
  wrap.innerHTML = "";
  swatches.forEach((hex) => {
    const d = document.createElement("div");
    d.className = "swatch";
    d.dataset.hex = hex;
    d.style.background = hex;
    d.title = hex;
    d.onclick = () => { setColor(hex); inp("custom").value = hex; };
    wrap.appendChild(d);
  });
  inp("custom").oninput = (e) => {
    const hex = normalizeHex((e.target as HTMLInputElement).value);
    if (hex) setColor(withHash(hex));
  };
}

async function loadSpec(): Promise<void> {
  try {
    const spec = (await (await fetch("/api/spec")).json()) as SpecReply;
    swatches = (spec.colorSwatches || []).map(withHash);
    sel("size").innerHTML = (spec.weightLabels || [])
      .map((w: string) => `<option>${w}</option>`).join("");
    buildSwatches();
    if (swatches.length) setColor(swatches[0]);
  } catch {
    setStatus("failed to load device spec", true);
  }
}

function fillFilaments(): void {
  const list = byBrand(items, sel("brand").value);
  sel("filament").innerHTML = list
    .map((i) => `<option value="${i.id}">${i.name} (${i.type})</option>`).join("");
}

function fillBrands(): void {
  sel("brand").innerHTML = brands(items).map((b) => `<option>${b}</option>`).join("");
  fillFilaments();
}

async function loadDb(): Promise<void> {
  try {
    items = parseMaterialDb(await (await fetch("/api/db")).json()); // gzip auto-decoded
    fillBrands();
  } catch {
    setStatus("failed to load material DB", true);
  }
}

async function pollStatus(): Promise<void> {
  const st = (await (await fetch("/api/status")).json()) as StatusReply;
  const outcome = interpretWriteStatus(st);
  if (!outcome.done) return;
  if (polling !== null) { clearInterval(polling); polling = null; }
  btn("write").disabled = false;
  setStatus(outcome.message ?? "", !outcome.ok);
}

function applyRead(j: ReadReply): void {
  const pre = readToPrefill(j, items);
  if (pre.material) {
    sel("brand").value = pre.material.brand;
    fillFilaments();
    sel("filament").value = pre.material.id;
  }
  if (pre.weight) sel("size").value = pre.weight;
  if (pre.colorHex) { setColor(withHash(pre.colorHex)); inp("custom").value = withHash(pre.colorHex); }
  $("readout").textContent = pre.label;
}

async function readTag(): Promise<void> {
  btn("read").disabled = true;
  setStatus("reading… tap a tag on the reader", false);
  try {
    const j = await (await fetch("/api/read")).json();
    if (!j.ok) { setStatus(`read failed: ${j.error}`, true); return; }
    applyRead(j as ReadReply);
    setStatus("tag read ✓ — review values and Write", false);
  } catch {
    setStatus("read error", true);
  } finally {
    btn("read").disabled = false;
  }
}

async function write(): Promise<void> {
  btn("write").disabled = true;
  setStatus("staging… tap a tag on the reader", false);
  const body = {
    materialId: sel("filament").value,
    color: normalizeHex(color) ?? "",
    weight: sel("size").value,
  };
  const j = await (await fetch("/api/write", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  })).json();
  if (!j.ok) { btn("write").disabled = false; setStatus(`error: ${j.error}`, true); return; }
  if (polling === null) polling = window.setInterval(pollStatus, 700);
}

sel("brand").onchange = fillFilaments;
btn("write").onclick = write;
btn("read").onclick = readTag;
void loadSpec();
void loadDb();

export {};
