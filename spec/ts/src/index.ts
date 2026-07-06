// @spoolid/core — shared client core: generated wire types (from spec/v2
// schemas; run `pnpm generate`), deduplicated pure logic, design tokens
// (import "@spoolid/core/tokens.css").
export type { Material } from "./db.js";
export { parseMaterialDb, brands, byBrand, nameFor } from "./db.js";
export { normalizeHex, withHash, colorName } from "./color.js";
export type { ReadPrefill } from "./read.js";
export { readToPrefill } from "./read.js";
export type { WriteOutcome } from "./status.js";
export { interpretWriteStatus } from "./status.js";
export type { CompatResult } from "./compat.js";
export { SUPPORTED_PROTOCOL, checkCompat, minorOf, cmpVer } from "./compat.js";
export { isFilesystemImage } from "./ota.js";
export type { ErrorCode, ErrorReply } from "./wire.js";
export { isErrorReply } from "./wire.js";

// Generated wire types (json2ts output, git-ignored):
export type { SpecReply } from "./generated/spec.reply.schema.js";
export type { ReadReply } from "./generated/read.reply.schema.js";
export type { StatusReply } from "./generated/status.reply.schema.js";
export type { ConfigReply } from "./generated/config.reply.schema.js";
export type { WriteReply } from "./generated/write.reply.schema.js";
export type { WriteParams } from "./generated/write.params.schema.js";
export type { DumpReply } from "./generated/dump.reply.schema.js";
