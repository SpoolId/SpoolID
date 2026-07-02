// Material database: parsed in the frontend from the bundled Creality JSON
// (mirrors the web client, which fetches /api/db and parses in the browser).
// The JSON is copied from the repo root into frontend/ by the `copy-db` script.
import rawDb from "../material_database.json";

export interface Material {
  id: string;
  brand: string;
  name: string;
  type: string;
}

interface DbDoc {
  result?: {
    list?: Array<{
      base?: { id?: string; brand?: string; name?: string; meterialType?: string };
    }>;
  };
}

// loadDb flattens result.list[].base into Material rows. Note the DB's
// misspelled type key, "meterialType".
export function loadDb(): Material[] {
  const doc = rawDb as DbDoc;
  const out: Material[] = [];
  for (const it of doc.result?.list ?? []) {
    const b = it.base;
    if (!b?.id) continue;
    out.push({ id: b.id, brand: b.brand ?? "", name: b.name ?? "", type: b.meterialType ?? "" });
  }
  return out;
}

export const brands = (items: Material[]): string[] =>
  [...new Set(items.map((m) => m.brand))].sort();

export const byBrand = (items: Material[], brand: string): Material[] =>
  items.filter((m) => m.brand === brand);

export const nameFor = (items: Material[], id: string): string => {
  const m = items.find((x) => x.id === id);
  return m ? `${m.brand} ${m.name}` : "";
};
