import type { LcarsNumericValue } from "./lcars-numeric";
import { isLcarsNumericValue } from "./lcars-numeric";
export {
  formatLcarsReading,
  isLcarsNumericValue,
  compareLcarsNumericValues,
  type LcarsNumericValue,
} from "./lcars-numeric";

export type LcarsReading =
  | { state: "ready"; value: LcarsNumericValue; unit?: string }
  | { state: "stale"; value: LcarsNumericValue; unit?: string; reason?: string }
  | { state: "offline"; reason?: string };

export type LcarsSystemState = "nominal" | "warning" | "critical" | "offline";

export type LcarsGraphSample = { label: string; value: number | null };

export type LcarsTelemetrySeries =
  | {
      state: "ready" | "stale";
      samples: readonly LcarsGraphSample[];
      unit?: string;
    }
  | { state: "offline"; reason?: string };

export type LcarsContact = {
  id: string;
  label: string;
  bearing: number;
  range: number;
  kind: "vessel" | "beacon" | "anomaly";
};

export type LcarsCascadeRow = {
  id: string;
  label: string;
  value: string | number;
  unit?: string;
};

export function getLcarsPercent(
  value: number | null,
  min = 0,
  max = 100,
): number | null {
  if (!isLcarsNumericValue(min) || !isLcarsNumericValue(max) || max <= min)
    throw new Error(
      "LCARS telemetry requires a finite safe numeric domain with min < max.",
    );
  if (!isLcarsNumericValue(value)) return null;
  return Math.min(100, Math.max(0, ((value - min) / (max - min)) * 100));
}
