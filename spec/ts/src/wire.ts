// Wire envelope types (mirrors $defs in spec/v2/schemas/common.schema.json —
// json2ts only emits schema roots, so these are declared by hand).
export type ErrorCode =
  | "bad_json"
  | "invalid_params"
  | "no_tag"
  | "db_failed"
  | "ota_failed"
  | "size_required"
  | "bad_chunk"
  | "unknown_cmd"
  | "internal";

// `code` is optional client-side: v1 firmware errors carry only `error`.
export interface ErrorReply {
  ok: false;
  error: string;
  code?: ErrorCode;
}

export const isErrorReply = (r: { ok: boolean }): r is ErrorReply => !r.ok;
