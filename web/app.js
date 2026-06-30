// SpoolID main page: load UI spec + material DB from the device, drive the pickers,
// write a tag. Firmware is the source of truth; this page fetches constants at load.
const $ = (id) => document.getElementById(id);
let items = [];          // flattened {id, brand, name, type}
let swatches = [];       // hex strings with leading '#'
let color = "#1200F6";

function setColor(hex) {
  color = hex.toUpperCase();
  $("hex").textContent = color;
  $("chip").style.background = color;
  document.querySelectorAll(".swatch").forEach((s) =>
    s.classList.toggle("sel", s.dataset.hex === color));
}

function buildSwatches() {
  const wrap = $("swatches");
  wrap.innerHTML = "";
  swatches.forEach((hex) => {
    const d = document.createElement("div");
    d.className = "swatch";
    d.dataset.hex = hex;
    d.style.background = hex;
    d.title = hex;
    d.onclick = () => { setColor(hex); $("custom").value = hex; };
    wrap.appendChild(d);
  });
  $("custom").oninput = (e) => setColor(e.target.value);
}

async function loadSpec() {
  try {
    const spec = await (await fetch("/api/spec")).json();
    swatches = (spec.colorSwatches || []).map((h) => "#" + h);
    $("size").innerHTML = (spec.weightLabels || [])
      .map((w) => `<option>${w}</option>`).join("");
    buildSwatches();
    if (swatches.length) setColor(swatches[0]);
  } catch (e) {
    setStatus("failed to load device spec", true);
  }
}

function fillBrands() {
  const brands = [...new Set(items.map((i) => i.brand))].sort();
  $("brand").innerHTML = brands.map((b) => `<option>${b}</option>`).join("");
  fillFilaments();
}

function fillFilaments() {
  const brand = $("brand").value;
  const list = items.filter((i) => i.brand === brand);
  $("filament").innerHTML = list
    .map((i) => `<option value="${i.id}">${i.name} (${i.type})</option>`)
    .join("");
}

async function loadDb() {
  try {
    const res = await fetch("/api/db");          // gzip auto-decoded by the browser
    const doc = await res.json();
    items = (doc.result?.list || []).map((it) => ({
      id: it.base.id,
      brand: it.base.brand,
      name: it.base.name,
      type: it.base.meterialType,
    }));
    fillBrands();
  } catch (e) {
    setStatus("failed to load material DB", true);
  }
}

function setStatus(msg, err) {
  const s = $("status");
  s.textContent = msg;
  s.className = "status" + (err ? " err" : msg ? " ok" : "");
}

let polling = null;
async function pollStatus() {
  const res = await fetch("/api/status");
  const st = await res.json();
  if (st.last && st.last.done) {
    clearInterval(polling);
    polling = null;
    $("write").disabled = false;
    if (st.last.ok) setStatus(`written ✓ UID ${st.last.uid}${st.last.encrypted ? " (re-encrypted)" : ""}`, false);
    else setStatus(`error: ${st.last.error}`, true);
  }
}

async function write() {
  $("write").disabled = true;
  setStatus("staging… tap a tag on the reader", false);
  const body = {
    materialId: $("filament").value,
    color: color.replace("#", ""),
    weight: $("size").value,
  };
  const res = await fetch("/api/write", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const j = await res.json();
  if (!j.ok) {
    $("write").disabled = false;
    setStatus(`error: ${j.error}`, true);
    return;
  }
  if (!polling) polling = setInterval(pollStatus, 700);
}

$("brand").onchange = fillFilaments;
$("write").onclick = write;
loadSpec();
loadDb();
