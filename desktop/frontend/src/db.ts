// Material database: the bundled Creality JSON (copied from the repo root by
// the `copy-db` script) parsed by the shared @spoolid/core logic — same code
// path as the web client, which fetches /api/db instead.
import rawDb from "../material_database.json";
import { parseMaterialDb } from "@spoolid/core";
import type { Material } from "@spoolid/core";

export type { Material };
export { brands, byBrand, nameFor } from "@spoolid/core";

export const loadDb = (): Material[] => parseMaterialDb(rawDb);
