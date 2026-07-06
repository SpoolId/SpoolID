import { describe, expect, it } from "vitest";
import rawDb from "../../../material_database.json";
import {
  brands,
  byBrand,
  checkCompat,
  cmpVer,
  colorName,
  interpretWriteStatus,
  isFilesystemImage,
  normalizeHex,
  parseMaterialDb,
  readToPrefill,
  withHash,
} from "../src/index.js";

describe("parseMaterialDb", () => {
  const items = parseMaterialDb(rawDb);
  it("parses the real repo database", () => {
    expect(items.length).toBeGreaterThan(10);
    for (const m of items.slice(0, 5)) {
      expect(m.id).toBeTruthy();
      expect(m.brand).toBeTruthy();
      expect(m.type).toBeTruthy(); // came from the misspelled meterialType key
    }
  });
  it("brands are unique and sorted", () => {
    const b = brands(items);
    expect(b).toEqual([...new Set(b)].sort());
    expect(byBrand(items, b[0]).length).toBeGreaterThan(0);
  });
  it("tolerates junk", () => {
    expect(parseMaterialDb(null)).toEqual([]);
    expect(parseMaterialDb({})).toEqual([]);
    expect(parseMaterialDb({ result: { list: [{ base: {} }] } })).toEqual([]);
  });
});

describe("color", () => {
  it("normalizes", () => {
    expect(normalizeHex("#ff00aa")).toBe("FF00AA");
    expect(normalizeHex("  1200F6 ")).toBe("1200F6");
    expect(normalizeHex("nope")).toBeNull();
    expect(normalizeHex("#12345")).toBeNull();
  });
  it("names known swatches only", () => {
    expect(colorName("#1200f6")).toBe("Blue");
    expect(colorName("123456")).toBeUndefined();
    expect(withHash("FF0000")).toBe("#FF0000");
  });
});

describe("checkCompat", () => {
  it("protocol exact match wins", () => {
    expect(checkCompat("2.0.0", { version: "2.0.0", protocol: 2 }).compatible).toBe(true);
    const r = checkCompat("2.0.0", { version: "9.9.9", protocol: 3 });
    expect(r.compatible).toBe(false);
    expect(r.reason).toBe("protocol");
  });
  it("legacy firmware falls back to major.minor", () => {
    expect(checkCompat("1.2.3", { version: "1.2.9" }).compatible).toBe(true);
    const r = checkCompat("1.3.0", { version: "1.2.0" });
    expect(r.compatible).toBe(false);
    expect(r.reason).toBe("minor");
  });
  it("dev builds skip the legacy gate", () => {
    expect(checkCompat("0.0.0-dev", { version: "1.2.0" }).compatible).toBe(true);
    expect(checkCompat("1.2.0", { version: "0.0.0-dev" }).compatible).toBe(true);
  });
});

describe("readToPrefill", () => {
  const items = [{ id: "01001", brand: "Hyper", name: "PLA", type: "PLA" }];
  it("matches a known material", () => {
    const p = readToPrefill(
      { materialId: "01001", color: "f38e24", weight: "1KG", encrypted: true }, items);
    expect(p.material?.brand).toBe("Hyper");
    expect(p.colorHex).toBe("F38E24");
    expect(p.label).toBe("read: Hyper PLA · 1KG · #F38E24 · locked");
    expect(p.locked).toBe(true);
  });
  it("unknown material still renders", () => {
    const p = readToPrefill({ materialId: "99999", color: "000000", weight: "1KG" }, items);
    expect(p.material).toBeUndefined();
    expect(p.label).toContain("id 99999");
  });
});

describe("interpretWriteStatus", () => {
  it("keeps polling until done", () => {
    expect(interpretWriteStatus({}).done).toBe(false);
    expect(interpretWriteStatus({ last: { done: false } }).done).toBe(false);
  });
  it("reports success + failure", () => {
    const ok = interpretWriteStatus({ last: { done: true, ok: true, uid: "AA", encrypted: true } });
    expect(ok.message).toBe("written ✓ UID AA (re-encrypted)");
    const err = interpretWriteStatus({ last: { done: true, ok: false, error: "boom" } });
    expect(err).toMatchObject({ done: true, ok: false, message: "error: boom" });
  });
});

describe("misc", () => {
  it("ota target autodetect", () => {
    expect(isFilesystemImage("Littlefs.bin")).toBe(true);
    expect(isFilesystemImage("https://x/firmware.bin")).toBe(false);
  });
  it("cmpVer", () => {
    expect(cmpVer("1.10.0", "1.9.9")).toBeGreaterThan(0);
    expect(cmpVer("2.0.0", "2.0.0")).toBe(0);
  });
});
