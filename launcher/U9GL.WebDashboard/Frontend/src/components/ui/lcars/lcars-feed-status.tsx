import { LcarsStatus } from "./lcars-status";
import { formatLcarsReading } from "./lcars-numeric";
import type { LcarsColorProps } from "./lcars-color";
import "./lcars.css";

export type LcarsFeedState =
  | "connecting"
  | "live"
  | "stale"
  | "reconnecting"
  | "disconnected"
  | "incompatible"
  | "error";
const feedConditions = {
  connecting: { title: "Connecting", condition: "warning" },
  live: { title: "Live", condition: "nominal" },
  stale: { title: "Stale", condition: "warning" },
  reconnecting: { title: "Reconnecting", condition: "warning" },
  disconnected: { title: "Disconnected", condition: "offline" },
  incompatible: { title: "Incompatible", condition: "critical" },
  error: { title: "Error", condition: "critical" },
} as const;

export function LcarsFeedStatus({
  label,
  state,
  ageMs,
  retained = false,
  detail,
  sessionLabel,
  ...color
}: {
  label: string;
  state: LcarsFeedState;
  ageMs: number | null;
  retained?: boolean;
  detail?: string;
  sessionLabel?: string;
} & LcarsColorProps) {
  const info = feedConditions[state];
  const validAge =
    ageMs !== null &&
    Number.isFinite(ageMs) &&
    ageMs >= 0 &&
    ageMs <= Number.MAX_SAFE_INTEGER;
  return (
    <section
      className="lcars-feed-status"
      aria-label={label}
      data-feed-state={state}
    >
      <LcarsStatus
        label={label}
        state={info.condition}
        detail={info.title}
        {...color}
      />
      <dl className="lcars-feed-metadata">
        <div>
          <dt>Sample age</dt>
          <dd>
            {validAge
              ? formatLcarsReading(ageMs / 1000, "s", 3)
              : "Unavailable"}
          </dd>
        </div>
        <div>
          <dt>Readings</dt>
          <dd>
            {retained
              ? "Retained snapshot"
              : state === "live"
                ? "Current snapshot"
                : "No retained readings"}
          </dd>
        </div>
        {sessionLabel && (
          <div>
            <dt>Session</dt>
            <dd>{sessionLabel}</dd>
          </div>
        )}
      </dl>
      {detail && <p className="lcars-copy">{detail}</p>}
    </section>
  );
}
