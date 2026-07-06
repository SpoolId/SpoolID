// Material database parsing — single owner of the `result.list[].base` walk
// and of the DB's misspelled type key, "meterialType".
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

// Flattens result.list[].base into Material rows (entries without an id are skipped).
export function parseMaterialDb(doc: unknown): Material[] {
  const d = doc as DbDoc;
  const out: Material[] = [];
  for (const it of d?.result?.list ?? []) {
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
