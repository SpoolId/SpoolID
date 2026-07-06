// Interpret a status reply while polling for a staged-write outcome — the
// shared core of the two pollStatus loops.
interface StatusLike {
  last?: { done?: unknown; ok?: unknown; encrypted?: unknown; uid?: unknown; error?: unknown };
}

export interface WriteOutcome {
  done: boolean;         // false -> keep polling
  ok?: boolean;
  message?: string;      // ready-to-display status line
  uid?: string;
  encrypted?: boolean;
}

export function interpretWriteStatus(reply: StatusLike): WriteOutcome {
  const last = reply.last;
  if (!last || !last.done) return { done: false };
  if (last.ok) {
    const uid = String(last.uid ?? "");
    const encrypted = Boolean(last.encrypted);
    return {
      done: true,
      ok: true,
      uid,
      encrypted,
      message: `written ✓ UID ${uid}${encrypted ? " (re-encrypted)" : ""}`,
    };
  }
  return { done: true, ok: false, message: `error: ${String(last.error ?? "unknown")}` };
}
