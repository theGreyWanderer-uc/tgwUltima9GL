import type { LcarsSystemState } from "./components/ui/lcars/lcars-telemetry";

export const thresholds = {
  fpsWarning: 20, fpsCritical: 10,
  privateWarning: 75, privateCritical: 90,
  evictionsPerSecWarning: 30,
  uploadMsWarning: 50,
  largestBlockWarningMiB: 256, largestBlockCriticalMiB: 64,
  worstCellWarning: 80, worstCellCritical: 95,
  itemPoolWarning: 85, itemPoolCritical: 95,
  vramWarning: 90, vramCritical: 97,
} as const;

export function worse(a: LcarsSystemState, b: LcarsSystemState): LcarsSystemState {
  const rank: Record<LcarsSystemState, number> = { offline: 0, nominal: 1, warning: 2, critical: 3 };
  return rank[b] > rank[a] ? b : a;
}

export function levelAbove(value: number | null, warning: number, critical = Infinity): LcarsSystemState {
  if (value === null || !Number.isFinite(value)) return "offline";
  return value >= critical ? "critical" : value >= warning ? "warning" : "nominal";
}

export function levelBelow(value: number | null, warning: number, critical = -Infinity): LcarsSystemState {
  if (value === null || !Number.isFinite(value)) return "offline";
  return value <= critical ? "critical" : value <= warning ? "warning" : "nominal";
}
