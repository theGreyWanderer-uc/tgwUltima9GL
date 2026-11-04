import type { DashboardGroup, DashboardHistoryPoint, DashboardIdentity, DashboardView } from "./u9gl-telemetry-contract.ts";

export type DashboardTransport = "connecting" | "connected" | "reconnecting" | "error";
export type DashboardChartPoint = { timeMs: number; value: number | null };

export function dashboardSessionKey(identity: DashboardIdentity): string {
  return `${identity.pid}/${identity.processCreatedFiletime}/${identity.sessionId}`;
}

export const dashboardGroupStaleMs = 2000;

export function dashboardGroupStale(group: DashboardGroup, elapsedSinceReceive: number, producerState: string, transport: DashboardTransport): boolean {
  return transport !== "connected" || producerState !== "live" || group.quality === "stale" || group.ageMs + elapsedSinceReceive >= group.staleMs;
}

export function dashboardCollectedTrend(history: readonly DashboardHistoryPoint[], key: string, scale = 1): DashboardChartPoint[] {
  const groupName = key.slice(0, key.indexOf("."));
  const result: DashboardChartPoint[] = [];
  let previousCollection: number | null = null;
  let gap = false;
  for (const point of history) {
    gap ||= point.gapBefore;
    const value = point.values[key];
    const collected = point.collectedMs[groupName];
    if (collected === undefined || value === undefined) { gap = true; continue; }
    if (collected === previousCollection) continue;
    if (gap && result.length) result.push({ timeMs: (result.at(-1)!.timeMs + collected) / 2, value: null });
    result.push({ timeMs: collected, value: Number(value) * scale });
    previousCollection = collected;
    gap = false;
  }
  return result;
}

export function dashboardPublicationTrend(history: readonly DashboardHistoryPoint[], key: string): DashboardChartPoint[] {
  const result: DashboardChartPoint[] = [];
  for (const point of history) {
    if (point.gapBefore && result.length) result.push({ timeMs: (result.at(-1)!.timeMs + point.elapsedMs) / 2, value: null });
    const value = point.values[key];
    result.push({ timeMs: point.elapsedMs, value: value === undefined ? null : Number(value) });
  }
  return result;
}

export function dashboardFeedState(view: DashboardView | null, transport: DashboardTransport, elapsed: number) {
  if (transport === "error") return "error";
  if (transport === "reconnecting" || (view !== null && elapsed > 3000)) return "reconnecting";
  if (!view || transport === "connecting") return "connecting";
  if (view.state === "processExited") return "disconnected";
  if (view.state === "waitingForGame" || view.state === "waitingForTelemetry") return "connecting";
  if (view.state === "live" && view.current && view.current.ageMs + elapsed >= 2000) return "stale";
  return view.state;
}
