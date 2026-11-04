import type { LcarsReading } from "./components/ui/lcars/lcars-telemetry";
import { resolveLcarsColorStyle } from "./components/ui/lcars/lcars-color";

export function formatMebibytes(bytes: string): string {
  const hundredths = (BigInt(bytes) * 100n + 524288n) / 1048576n;
  const whole = new Intl.NumberFormat("en-US").format(hundredths / 100n);
  return `${whole}.${(hundredths % 100n).toString().padStart(2, "0")}`;
}

export function U9glMemoryReadout({ label, reading }: { label: string; reading: LcarsReading }) {
  const bytes = reading.state !== "offline" && typeof reading.value === "string" && /^(0|[1-9][0-9]*)$/.test(reading.value)
    ? reading.value : null;
  const quality = bytes === null ? "Unavailable" : reading.state === "stale" ? "Stale" : "Current";
  return (
    <dl className="lcars-numeric-readout" data-quality={bytes === null ? "offline" : reading.state}
      style={resolveLcarsColorStyle("accent")}>
      <dt>{label} (MiB)</dt>
      <dd>
        <strong>{bytes === null ? "Unavailable" : formatMebibytes(bytes)}</strong>
        <span>{quality}{"reason" in reading && reading.reason ? ` · ${reading.reason}` : ""}</span>
      </dd>
    </dl>
  );
}
