import type { LcarsReading } from "./components/ui/lcars/lcars-telemetry";
import { resolveLcarsColorStyle } from "./components/ui/lcars/lcars-color";

export function U9glTextReadout({ label, reading, text }: { label: string; reading: LcarsReading; text: string | null }) {
  const available = reading.state !== "offline" && text !== null;
  const quality = !available ? "Unavailable" : reading.state === "stale" ? "Stale" : "Current";
  return (
    <dl className="lcars-numeric-readout" data-quality={available ? reading.state : "offline"} style={resolveLcarsColorStyle("accent")}>
      <dt>{label}</dt>
      <dd>
        <strong>{available ? text : "Unavailable"}</strong>
        <span>{quality}{"reason" in reading && reading.reason ? ` · ${reading.reason}` : ""}</span>
      </dd>
    </dl>
  );
}
