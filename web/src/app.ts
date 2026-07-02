// SpoolID device web UI — main page (brand -> filament -> size -> color -> Write).
// Firmware is the source of truth: constants come from /api/spec, data from /api/db.
// (style.css is served as a static asset via <link>, not imported.)

// Helpers are inlined (not a shared module) so each page bundles to a single
// self-contained file — no shared chunk for the device to route.
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

interface Material {
  id: string;
  brand: string;
  name: string;
  type: string;
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
  inp("custom").oninput = (e) => setColor((e.target as HTMLInputElement).value);
}

async function loadSpec(): Promise<void> {
  try {
    const spec = await (await fetch("/api/spec")).json();
    swatches = (spec.colorSwatches || []).map((h: string) => "#" + h);
    sel("size").innerHTML = (spec.weightLabels || [])
      .map((w: string) => `<option>${w}</option>`).join("");
    buildSwatches();
    if (swatches.length) setColor(swatches[0]);
  } catch {
    setStatus("failed to load device spec", true);
  }
}

function fillFilaments(): void {
  const brand = sel("brand").value;
  const list = items.filter((i) => i.brand === brand);
  sel("filament").innerHTML = list
    .map((i) => `<option value="${i.id}">${i.name} (${i.type})</option>`).join("");
}

function fillBrands(): void {
  const brands = [...new Set(items.map((i) => i.brand))].sort();
  sel("brand").innerHTML = brands.map((b) => `<option>${b}</option>`).join("");
  fillFilaments();
}

async function loadDb(): Promise<void> {
  try {
    const doc = await (await fetch("/api/db")).json(); // gzip auto-decoded
    items = (doc.result?.list || []).map((it: any) => ({
      id: it.base.id, brand: it.base.brand, name: it.base.name, type: it.base.meterialType,
    }));
    fillBrands();
  } catch {
    setStatus("failed to load material DB", true);
  }
}

async function pollStatus(): Promise<void> {
  const st = await (await fetch("/api/status")).json();
  if (st.last && st.last.done) {
    if (polling !== null) { clearInterval(polling); polling = null; }
    btn("write").disabled = false;
    if (st.last.ok) {
      setStatus(`written ✓ UID ${st.last.uid}${st.last.encrypted ? " (re-encrypted)" : ""}`, false);
    } else {
      setStatus(`error: ${st.last.error}`, true);
    }
  }
}

function applyRead(j: any): void {
  const item = items.find((i) => i.id === j.materialId);
  if (item) { sel("brand").value = item.brand; fillFilaments(); sel("filament").value = item.id; }
  if (j.weight) sel("size").value = j.weight;
  if (j.color) { setColor("#" + j.color); inp("custom").value = "#" + j.color; }
  const name = item ? `${item.brand} ${item.name}` : `id ${j.materialId}`;
  $("readout").textContent =
    `read: ${name} · ${j.weight || "?"} · #${j.color}${j.encrypted ? " · locked" : ""}`;
}

async function readTag(): Promise<void> {
  btn("read").disabled = true;
  setStatus("reading… tap a tag on the reader", false);
  try {
    const j = await (await fetch("/api/read")).json();
    if (!j.ok) { setStatus(`read failed: ${j.error}`, true); return; }
    applyRead(j);
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
    color: color.replace("#", ""),
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
