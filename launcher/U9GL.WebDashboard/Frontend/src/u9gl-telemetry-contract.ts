import { isLogSummary, type DashboardLogSummary } from "./u9gl-log.ts";
import type { DashboardArrayLayout } from "./u9gl-heatmap-data.ts";

export type DashboardIdentity = { pid: number; processCreatedFiletime: string; sessionId: string; producerKind: string; producerVersion: string };
export type DashboardGroup = { collectedQpc: string; elapsedMs: number; ageMs: number; quality: "ready" | "stale"; staleMs: number; values: Record<string, string>;
   array?: string[] | null };
export type DashboardSample = {
  identity: DashboardIdentity; qpcFrequency: string; sessionStartQpc: string;
  sampleIndex: string; sampleQpc: string; sampleUtcFiletime: string | null;
  elapsedMs: number; ageMs: number; contractMinor: number; requestedGroups: string[]; skippedGroups: number;
  groups: Record<string, DashboardGroup>;
  labels: Record<string, string>;
};
export type DashboardHistoryPoint = {
  sampleIndex: string; elapsedMs: number; gapBefore: boolean;
  collectedMs: Record<string, number>;
  values: Record<string, string>;
};
export type DashboardSession = { identity: DashboardIdentity; history: DashboardHistoryPoint[]; longHistory: DashboardHistoryPoint[] };
export type DashboardProducerState = "waitingForGame" | "waitingForTelemetry" | "live" | "stale" | "processExited" | "incompatible" | "error";
export type DashboardView = {
  version: 2; state: DashboardProducerState; detail: string; current: DashboardSample | null; sessions: DashboardSession[];
  limits: { sessionLimit: number; samplesPerSession: number; historySeconds: number; historyByteLimit: number;
    longSamplesPerSession: number; longHistorySeconds: number; longIntervalSeconds: number };
  historySeries: string[];
  schema: Record<string, DashboardField[]>;
  game: { executable: string; largeAddressAware: boolean; addressSpaceBytes: string } | null;
  build: string;
  log: DashboardLogSummary | null;
  arrays: Record<string, DashboardArrayLayout>;
};
export type DashboardField = { name: string; kind: "unsigned" | "signed" | "flag" | "enum"; scale: number; unit: string; names: string[] | null };

function record(value: unknown): value is Record<string, unknown> { return typeof value === "object" && value !== null && !Array.isArray(value); }
function exact(value: unknown): value is string {
  return typeof value === "string" && /^(0|[1-9][0-9]{0,19})$/.test(value) && BigInt(value) <= 18446744073709551615n;
}
function word(value: unknown): value is string {
  if (typeof value !== "string" || !/^-?(0|[1-9][0-9]{0,19})$/.test(value) || value === "-0") return false;
  const n = BigInt(value);
  return n >= -9223372036854775808n && n <= 18446744073709551615n;
}
function interval(value: unknown): value is number { return typeof value === "number" && Number.isFinite(value) && value >= 0 && value <= Number.MAX_SAFE_INTEGER; }
function nullableExact(value: unknown) { return value === null || exact(value); }
function identity(value: unknown): value is DashboardIdentity {
  return record(value) && Number.isSafeInteger(value.pid) && Number(value.pid) > 0 && Number(value.pid) <= 4294967295 &&
    exact(value.processCreatedFiletime) && typeof value.sessionId === "string" && /^[0-9a-f]{32}$/.test(value.sessionId) &&
    (value.producerKind === "Game" || value.producerKind === "Synthetic") && typeof value.producerVersion === "string" && value.producerVersion.length <= 31;
}
const groupName = /^[a-z][A-Za-z0-9]{0,31}$/;
const seriesKey = /^[a-z][A-Za-z0-9]{0,31}[.][a-z][A-Za-z0-9]{0,31}$/;
function keyed<T>(value: unknown, key: RegExp, limit: number, item: (v: unknown) => v is T): value is Record<string, T> {
  if (!record(value)) return false;
  const keys = Object.keys(value);
  return keys.length <= limit && keys.every(k => key.test(k) && item(value[k]));
}
function group(value: unknown): value is DashboardGroup {
  return record(value) && exact(value.collectedQpc) && interval(value.elapsedMs) && interval(value.ageMs) &&
    (value.quality === "ready" || value.quality === "stale") && interval(value.staleMs) && value.staleMs > 0 &&
    keyed(value.values, groupName, 64, word) &&
    (value.array === undefined || value.array === null || (Array.isArray(value.array) && value.array.length <= 256 && value.array.every(exact)));
}
function arrayLayout(v: unknown): v is DashboardArrayLayout {
  return record(v) && typeof v.name === "string" && groupName.test(v.name) && typeof v.countField === "string" &&
    Number.isSafeInteger(v.capacity) && Number(v.capacity) > 0 && Number(v.capacity) <= 4096 && Array.isArray(v.parts) &&
    v.parts.length <= 16 && v.parts.every(p => record(p) && typeof p.name === "string" && groupName.test(p.name) &&
      Number.isSafeInteger(p.bits) && Number(p.bits) >= 1 && Number(p.bits) <= 64) &&
    v.parts.reduce((sum: number, p: { bits: number }) => sum + p.bits, 0) <= 64;
}
function field(value: unknown): value is DashboardField {
  return record(value) && typeof value.name === "string" && groupName.test(value.name) &&
    (value.kind === "unsigned" || value.kind === "signed" || value.kind === "flag" || value.kind === "enum") &&
    Number.isSafeInteger(value.scale) && Number(value.scale) >= 1 && typeof value.unit === "string" && value.unit.length <= 32 &&
    (value.names === null || (Array.isArray(value.names) && value.names.length <= 64 &&
      value.names.every(n => typeof n === "string" && n.length <= 32)));
}
function fieldList(value: unknown): value is DashboardField[] { return Array.isArray(value) && value.length <= 64 && value.every(field); }
function sample(value: unknown): value is DashboardSample | null {
  return value === null || (record(value) && identity(value.identity) && exact(value.qpcFrequency) && value.qpcFrequency !== "0" &&
    exact(value.sessionStartQpc) && exact(value.sampleIndex) && exact(value.sampleQpc) && nullableExact(value.sampleUtcFiletime) &&
    interval(value.elapsedMs) && interval(value.ageMs) && Number.isSafeInteger(value.contractMinor) && Number(value.contractMinor) >= 0 &&
    Array.isArray(value.requestedGroups) && value.requestedGroups.length <= 32 &&
    value.requestedGroups.every(name => typeof name === "string" && groupName.test(name)) &&
    Number.isSafeInteger(value.skippedGroups) && Number(value.skippedGroups) >= 0 && Number(value.skippedGroups) <= 32 &&
    keyed(value.groups, groupName, 32, group) &&
    keyed(value.labels, seriesKey, 32, (v: unknown): v is string => typeof v === "string" && v.length <= 128));
}
function historyPoint(value: unknown): value is DashboardHistoryPoint {
  return record(value) && exact(value.sampleIndex) && interval(value.elapsedMs) && typeof value.gapBefore === "boolean" &&
    keyed(value.collectedMs, groupName, 32, interval) && keyed(value.values, seriesKey, 64, word);
}

export function parseDashboardView(json: string): DashboardView {
  if (json.length > 2_200_000) throw new Error("U9GL dashboard: response exceeds the browser view limit.");
  const value: unknown = JSON.parse(json);
  const states: readonly string[] = ["waitingForGame", "waitingForTelemetry", "live", "stale", "processExited", "incompatible", "error"];
  if (!record(value) || value.version !== 2 || typeof value.state !== "string" || !states.includes(value.state) ||
    typeof value.detail !== "string" || value.detail.length > 256 || !sample(value.current) ||
    !Array.isArray(value.sessions) || value.sessions.length > 3 || !record(value.limits) ||
    value.limits.sessionLimit !== 3 || value.limits.samplesPerSession !== 600 || value.limits.historySeconds !== 120 || value.limits.historyByteLimit !== 2097152 ||
    value.limits.longSamplesPerSession !== 360 || value.limits.longHistorySeconds !== 1800 || value.limits.longIntervalSeconds !== 5 ||
    !Array.isArray(value.historySeries) || value.historySeries.length > 64 ||
    !value.historySeries.every(key => typeof key === "string" && seriesKey.test(key)) ||
    !keyed(value.schema, groupName, 32, fieldList) ||
    !(value.game === null || (record(value.game) && typeof value.game.executable === "string" && value.game.executable.length <= 64 &&
      typeof value.game.largeAddressAware === "boolean" && exact(value.game.addressSpaceBytes))) ||
    typeof value.build !== "string" || !/^([0-9a-f]{16}|none)$/.test(value.build) ||
    !(value.log === null || isLogSummary(value.log)) || !keyed(value.arrays, groupName, 16, arrayLayout))
    throw new Error("U9GL dashboard: unsupported or invalid browser telemetry contract.");
  for (const session of value.sessions) {
    if (!record(session) || !identity(session.identity) || !Array.isArray(session.history) || session.history.length > 600 ||
      !session.history.every(historyPoint) || !Array.isArray(session.longHistory) || session.longHistory.length > 360 ||
      !session.longHistory.every(historyPoint)) throw new Error("U9GL dashboard: invalid session history.");
    for (const tier of [session.history, session.longHistory]) {
      let previous = -1;
      for (const point of tier) {
        if (point.elapsedMs < previous) throw new Error("U9GL dashboard: history time moved backwards.");
        previous = point.elapsedMs;
      }
    }
  }
  return value as DashboardView;
}
