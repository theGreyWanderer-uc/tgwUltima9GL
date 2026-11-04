import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import {
  formatLcarsReading,
  isLcarsNumericValue,
  type LcarsReading,
  type LcarsSystemState,
} from "./lcars-telemetry";
import type { LcarsTone } from "./lcars-theme-tokens";
import "./lcars.css";

const systemConditions = {
  nominal: { title: "Nominal", tone: "success" },
  warning: { title: "Attention", tone: "warning" },
  critical: { title: "Critical", tone: "danger" },
  offline: { title: "Offline", tone: "secondary" },
} as const satisfies Record<
  LcarsSystemState,
  { title: string; tone: LcarsTone }
>;

export function LcarsStatus({
  label,
  state,
  detail,
  tone,
  color,
  style,
}: {
  label: string;
  state: LcarsSystemState;
  detail?: string;
} & LcarsColorProps) {
  const condition = systemConditions[state];
  return (
    <div
      className="lcars-system-status"
      role="group"
      aria-label={label}
      data-condition={state}
      style={resolveLcarsColorStyle(tone ?? condition.tone, color, style)}
    >
      <span className="lcars-status-dot" aria-hidden="true" />
      <div>
        <strong>{label}</strong>
        <span>
          {condition.title}
          {detail ? ` · ${detail}` : ""}
        </span>
      </div>
    </div>
  );
}

export function LcarsReadout({
  label,
  reading,
  precision = 1,
  tone = "accent",
  color,
  style,
}: {
  label: string;
  reading: LcarsReading;
  precision?: number;
} & LcarsColorProps) {
  const valid =
    reading.state !== "offline" && isLcarsNumericValue(reading.value);
  const quality = !valid
    ? "Unavailable"
    : reading.state === "stale"
      ? "Stale"
      : "Current";
  return (
    <dl
      className="lcars-numeric-readout"
      data-quality={valid ? reading.state : "offline"}
      style={resolveLcarsColorStyle(tone, color, style)}
    >
      <dt>{label}</dt>
      <dd>
        <strong>
          {valid
            ? formatLcarsReading(reading.value, reading.unit, precision)
            : "Unavailable"}
        </strong>
        <span>
          {quality}
          {"reason" in reading && reading.reason ? ` · ${reading.reason}` : ""}
        </span>
      </dd>
    </dl>
  );
}
