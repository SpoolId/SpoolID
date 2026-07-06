#!/usr/bin/env node
// Validates the wire-contract spec: every schema compiles (Ajv strict mode is
// the schema lint), every fixture matches its schema, and the repo-root
// material_database.json matches the v2 DB schema.
//
// Fixture layout: spec/<version>/fixtures/<schema-stem>/<case>.json validates
// against spec/<version>/schemas/<schema-stem>.schema.json. Files named
// err-*.json validate against that version's common ErrorReply instead.
import { readFileSync, readdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { Ajv2020 } from "ajv/dist/2020.js";
import addFormats from "ajv-formats";

const SPEC = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
const BASE = "https://spoolid.github.io/schemas";

const readJson = (p) => JSON.parse(readFileSync(p, "utf8"));
const list = (dir, filter) => {
  try {
    return readdirSync(dir, { withFileTypes: true }).filter(filter).map((e) => e.name);
  } catch {
    return [];
  }
};

let failures = 0;
const fail = (what, errors) => {
  failures++;
  console.error(`FAIL ${what}`);
  for (const e of errors ?? []) console.error(`  ${e.instancePath || "/"} ${e.message}`);
};

// getSchema compiles lazily and throws on unresolvable $refs; report, don't crash.
const compiled = (ajv, id) => {
  try {
    return ajv.getSchema(id);
  } catch {
    return undefined;
  }
};

const checkFixture = (ajv, version, dir, stem, name) => {
  const schemaId = name.startsWith("err-")
    ? `${BASE}/${version}/common.schema.json#/$defs/ErrorReply`
    : `${BASE}/${version}/${stem}.schema.json`;
  const validate = compiled(ajv, schemaId);
  if (!validate) return fail(`${dir}/${stem}/${name}: no schema ${schemaId}`);
  if (!validate(readJson(join(dir, stem, name)))) fail(`${dir}/${stem}/${name}`, validate.errors);
};

for (const version of ["v1", "v2"]) {
  const ajv = new Ajv2020({ strict: true, allErrors: true });
  addFormats(ajv);

  const schemasDir = join(SPEC, version, "schemas");
  for (const name of list(schemasDir, (e) => e.name.endsWith(".schema.json"))) {
    ajv.addSchema(readJson(join(schemasDir, name)));
  }
  // Compiling every schema up front is the lint step: bad $refs, bad keywords,
  // and strict-mode violations all surface here.
  for (const id of Object.keys(ajv.schemas).filter((k) => k.startsWith(BASE))) {
    try {
      ajv.getSchema(id) ?? fail(`${version} compile ${id}`);
    } catch (e) {
      fail(`${version} compile ${id}: ${e.message}`);
    }
  }

  // Hand-captured fixtures, plus fixtures/generated/ written by the firmware
  // native contract test (git-ignored) — same <stem>/<case>.json layout.
  const fixturesDir = join(SPEC, version, "fixtures");
  for (const dir of [fixturesDir, join(fixturesDir, "generated")]) {
    for (const stem of list(dir, (e) => e.isDirectory() && e.name !== "generated")) {
      for (const name of list(join(dir, stem), (e) => e.name.endsWith(".json"))) {
        checkFixture(ajv, version, dir, stem, name);
      }
    }
  }

  if (version === "v2") {
    const validate = compiled(ajv, `${BASE}/v2/material-database.schema.json`);
    const db = readJson(join(SPEC, "..", "material_database.json"));
    if (!validate) fail("material-database schema failed to compile");
    else if (!validate(db)) fail("material_database.json", validate.errors);
  }
}

if (failures) {
  console.error(`\n${failures} failure(s)`);
  process.exit(1);
}
console.log("spec OK: schemas compile, fixtures + material_database.json validate");
