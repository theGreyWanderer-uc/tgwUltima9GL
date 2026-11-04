export type DashboardLogEvent = {
  seq: number; receivedUtc: string; runPid: number; source: "log" | "crash";
  tag: string; level: "info" | "warning" | "fault"; text: string; detail: string | null;
};
export type DashboardLogSummary = {
  runPid: number; runVersion: string | null; faults: number; warnings: number; lastSeq: number;
  logFile: string; logBytes: number; logGrewAgoMs: number | null; crashReport: string | null;
};
export type DashboardLogPage = { events: DashboardLogEvent[]; next: number; oldest: number; summary: DashboardLogSummary };

const count = (v: unknown) => Number.isSafeInteger(v) && Number(v) >= 0;
const text = (v: unknown, max: number) => typeof v === "string" && v.length <= max;

export function isLogSummary(v: unknown): v is DashboardLogSummary {
  if (typeof v !== "object" || v === null) return false;
  const s = v as Record<string, unknown>;
  return count(s.runPid) && Number(s.runPid) <= 4294967295 && (s.runVersion === null || text(s.runVersion, 64)) &&
    count(s.faults) && count(s.warnings) && count(s.lastSeq) && text(s.logFile, 520) && count(s.logBytes) &&
    (s.logGrewAgoMs === null || (typeof s.logGrewAgoMs === "number" && Number.isFinite(s.logGrewAgoMs) && s.logGrewAgoMs >= 0)) &&
    (s.crashReport === null || text(s.crashReport, 520));
}

function isEvent(v: unknown): v is DashboardLogEvent {
  if (typeof v !== "object" || v === null) return false;
  const e = v as Record<string, unknown>;
  return count(e.seq) && text(e.receivedUtc, 40) && count(e.runPid) && (e.source === "log" || e.source === "crash") &&
    text(e.tag, 16) && (e.level === "info" || e.level === "warning" || e.level === "fault") && text(e.text, 512) &&
    (e.detail === null || text(e.detail, 4096));
}

export function parseLogPage(json: string): DashboardLogPage {
  if (json.length > 3_000_000) throw new Error("U9GL dashboard: log page exceeds the browser limit.");
  const v: unknown = JSON.parse(json);
  if (typeof v !== "object" || v === null) throw new Error("U9GL dashboard: invalid log page.");
  const p = v as Record<string, unknown>;
  if (!Array.isArray(p.events) || p.events.length > 500 || !p.events.every(isEvent) || !count(p.next) || !count(p.oldest) ||
    !isLogSummary(p.summary)) throw new Error("U9GL dashboard: invalid log page.");
  let previous = 0;
  for (const e of p.events as DashboardLogEvent[]) {
    if (e.seq <= previous) throw new Error("U9GL dashboard: log events out of order.");
    previous = e.seq;
  }
  return p as unknown as DashboardLogPage;
}

export function filterLogEvents(events: readonly DashboardLogEvent[], query: string, level: "all" | "warning" | "fault", tag: string) {
  const needle = query.trim().toLowerCase();
  return events.filter(e => (level === "all" || (level === "warning" ? e.level !== "info" : e.level === "fault")) &&
    (tag === "" || e.tag === tag) &&
    (needle === "" || e.text.toLowerCase().includes(needle) || (e.detail?.toLowerCase().includes(needle) ?? false)));
}
