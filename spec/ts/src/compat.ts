// Client <-> firmware compatibility gate. v2+ firmware advertises `protocol`
// (exact match required); 1.x firmware has only `version` (major.minor must
// match, the legacy lockstep rule). Dev builds skip the gate.
export const SUPPORTED_PROTOCOL = 2;

export interface CompatResult {
  compatible: boolean;
  reason?: "protocol" | "minor";
  message?: string;
}

export function minorOf(v: string): string | null {
  const m = /^(\d+)\.(\d+)\./.exec(v);
  return m ? `${m[1]}.${m[2]}` : null;
}

// Compare dotted numeric versions (prerelease suffixes ignored). >0 if a > b.
export function cmpVer(a: string, b: string): number {
  const pa = a.split(/[.\-+]/).map(Number);
  const pb = b.split(/[.\-+]/).map(Number);
  for (let i = 0; i < 3; i++) {
    const d = (pa[i] || 0) - (pb[i] || 0);
    if (d) return d;
  }
  return 0;
}

interface SpecLike {
  version?: unknown;
  protocol?: unknown;
}

// 0.0.0[-suffix] is the version.h fallback for local/dev builds.
const isDevBuild = (v: string): boolean => !v || v.startsWith("0.0.0");

export function checkCompat(appVersion: string, spec: SpecLike): CompatResult {
  const fwVer = String(spec.version ?? "");
  if (isDevBuild(appVersion) || isDevBuild(fwVer)) return { compatible: true };
  if (typeof spec.protocol === "number") {
    return spec.protocol === SUPPORTED_PROTOCOL
      ? { compatible: true }
      : {
          compatible: false,
          reason: "protocol",
          message:
            `firmware speaks protocol ${spec.protocol}, this app expects ` +
            `${SUPPORTED_PROTOCOL} — update firmware and app to matching releases`,
        };
  }
  // Legacy 1.x firmware: major.minor lockstep. Dev builds on either side skip.
  const app = minorOf(appVersion);
  const fw = minorOf(fwVer);
  if (!app || !fw) return { compatible: true };
  return app === fw
    ? { compatible: true }
    : {
        compatible: false,
        reason: "minor",
        message:
          `version mismatch — app v${appVersion} vs firmware v${fwVer}. ` +
          `Update so both share the same minor.`,
      };
}
