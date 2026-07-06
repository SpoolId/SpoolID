// Map a read/tag reply onto write-form prefill values — the shared core of the
// web and desktop "read a tag, review, write back" flow. UIs keep only DOM glue.
import type { Material } from "./db.js";
import { nameFor } from "./db.js";

export interface ReadPrefill {
  material?: Material;   // matched DB row (brand/filament selects)
  weight?: string;       // size select value
  colorHex?: string;     // bare RRGGBB, uppercase
  label: string;         // human line for the readout, e.g. "read: Brand Name · 1KG · #FF0000"
  locked: boolean;       // tag was already encrypted to its derived key
}

interface ReadLike {
  materialId?: unknown;
  color?: unknown;
  weight?: unknown;
  encrypted?: unknown;
}

export function readToPrefill(reply: ReadLike, items: Material[]): ReadPrefill {
  const materialId = String(reply.materialId ?? "");
  const material = items.find((m) => m.id === materialId);
  const weight = reply.weight ? String(reply.weight) : undefined;
  const hex = String(reply.color ?? "").toUpperCase();
  const colorHex = /^[0-9A-F]{6}$/.test(hex) ? hex : undefined;

  const name = material
    ? `${material.brand} ${material.name}`
    : nameFor(items, materialId) || `id ${materialId}`;
  const locked = Boolean(reply.encrypted);
  const label = `read: ${name} · ${weight ?? "?"} · #${hex}${locked ? " · locked" : ""}`;
  return { material, weight, colorHex, label, locked };
}
