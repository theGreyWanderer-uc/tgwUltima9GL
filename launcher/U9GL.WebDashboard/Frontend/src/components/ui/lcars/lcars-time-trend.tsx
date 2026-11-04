import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import { formatLcarsReading, getLcarsPercent } from "./lcars-telemetry";
import {
  boundLcarsTimeSamples,
  isLcarsElapsedTime,
  type LcarsTimeSeries,
} from "./lcars-time-series";
import "./lcars.css";

function timeAxisLabels(startMs: number, endMs: number): string[] {
  const ticks = [startMs, startMs + (endMs - startMs) / 2, endMs];
  const divisor =
    endMs >= 1000
      ? 1000
      : endMs >= 1 || endMs === 0
        ? 1
        : endMs >= 0.001
          ? 0.001
          : 0.000001;
  let unit =
    divisor === 1000
      ? "s"
      : divisor === 1
        ? "ms"
        : divisor === 0.001
          ? "µs"
          : "ns";
  let values = ticks.map((value) => value / divisor);
  if (new Set(values).size < new Set(ticks).size) {
    unit = "ms";
    values = ticks;
  }
  const count = new Set(values).size;
  const step = (values[2]! - values[0]!) / 2;
  const initial =
    step > 0 ? Math.max(3, Math.min(20, Math.ceil(-Math.log10(step)) + 1)) : 3;
  for (let precision = initial; precision <= 20; precision++) {
    const labels = values.map((value) =>
      formatLcarsReading(value, unit, precision),
    );
    if (new Set(labels).size === count) return labels;
  }
  return values.map((value) => `${value} ${unit}`);
}

export type LcarsTimeTrendProps = {
  label: string;
  series: LcarsTimeSeries;
  min?: number;
  max?: number;
  windowMs?: number;
  maxSamples?: number;
  gapAfterMs?: number;
} & LcarsColorProps;

export function LcarsTimeTrend({
  label,
  series,
  min = 0,
  max = 100,
  windowMs = 60_000,
  maxSamples = 300,
  gapAfterMs = 5000,
  tone = "accent",
  color,
  style,
}: LcarsTimeTrendProps) {
  getLcarsPercent(null, min, max);
  if (!Number.isFinite(gapAfterMs) || gapAfterMs <= 0)
    throw new Error(
      "LCARS time trend requires a positive finite gap interval.",
    );
  const samples = boundLcarsTimeSamples(
    series.state === "offline" ? [] : series.samples,
    windowMs,
    maxSamples,
  );
  const end = samples.at(-1)?.timeMs ?? 0;
  const start = Math.max(0, end - windowMs);
  const span = end > start ? end - start : 1;
  const axisLabels = timeAxisLabels(start, end);
  const segments: string[][] = [];
  let segment: string[] = [],
    previous: number | null = null;
  for (const sample of samples) {
    const percent = getLcarsPercent(sample.value, min, max);
    if (
      percent === null ||
      (previous !== null && sample.timeMs - previous > gapAfterMs)
    ) {
      if (segment.length) segments.push(segment);
      segment = [];
    }
    if (percent !== null)
      segment.push(
        `${(8 + ((sample.timeMs - start) / span) * 584).toFixed(2)},${(152 - percent * 1.44).toFixed(2)}`,
      );
    previous = sample.timeMs;
  }
  if (segment.length) segments.push(segment);
  const invalidTimes =
    series.state === "offline"
      ? 0
      : series.samples.filter((sample) => !isLcarsElapsedTime(sample.timeMs))
          .length;
  return (
    <figure
      className="lcars-time-trend"
      aria-label={label}
      style={resolveLcarsColorStyle(tone, color, style)}
    >
      <figcaption>
        <strong>{label}</strong>
        <span>
          {series.state === "ready"
            ? "Current data"
            : series.state === "stale"
              ? "Stale data"
              : "Offline"}
          {series.state !== "offline" && series.unit ? ` · ${series.unit}` : ""}
        </span>
      </figcaption>
      {segments.length > 0 ? (
        <>
          <div className="lcars-time-plot">
            <div className="lcars-time-y" aria-label="Value axis">
              <span>{formatLcarsReading(max)}</span>
              <span>{formatLcarsReading(min + (max - min) / 2)}</span>
              <span>{formatLcarsReading(min)}</span>
            </div>
            <svg
              viewBox="0 0 600 160"
              preserveAspectRatio="none"
              role="img"
              aria-label={`${label}, ${samples.length} retained samples, value range ${min} to ${max}. Elapsed time ${start / 1000} to ${end / 1000} seconds. Open the table for sample values.`}
            >
              <path
                className="lcars-graph-grid"
                d="M8 8H592M8 80H592M8 152H592M8 8V152M300 8V152M592 8V152"
              />
              {segments.map((points, index) => (
                <g key={index}>
                  <polyline
                    className="lcars-graph-line"
                    vectorEffect="non-scaling-stroke"
                    points={points.join(" ")}
                  />
                  {points.map((point) => {
                    const [cx, cy] = point.split(",");
                    return (
                      <circle
                        className="lcars-graph-point"
                        key={point}
                        cx={cx}
                        cy={cy}
                        r="2.5"
                      />
                    );
                  })}
                </g>
              ))}
            </svg>
          </div>
          <div className="lcars-time-x">
            {axisLabels.map((label, index) => (
              <span key={index}>{label}</span>
            ))}
          </div>
          <p className="lcars-supporting">
            Elapsed time · gaps remain unconnected · geometry clips to the
            displayed range.
          </p>
        </>
      ) : (
        <p className="lcars-supporting">
          {series.state === "offline"
            ? series.reason || "Telemetry unavailable."
            : "No available samples."}
        </p>
      )}
      <p className="lcars-supporting">
        {samples.length} retained samples · window{" "}
        {timeAxisLabels(0, windowMs).at(-1)} · limit {maxSamples}
        {invalidTimes ? ` · ${invalidTimes} invalid timestamps omitted` : ""}
      </p>
      {samples.length > 0 && (
        <details className="lcars-data-details">
          <summary>View {label} data</summary>
          <div className="lcars-table-wrap">
            <table className="lcars-table">
              <caption>{label} · supplied values</caption>
              <thead>
                <tr>
                  <th scope="col">Elapsed time</th>
                  <th scope="col">Value</th>
                </tr>
              </thead>
              <tbody>
                {samples.map((sample) => (
                  <tr key={sample.timeMs}>
                    <th scope="row">
                      {sample.timeMs} ms
                      {sample.label ? ` · ${sample.label}` : ""}
                    </th>
                    <td>
                      {getLcarsPercent(sample.value, min, max) === null
                        ? "Unavailable"
                        : `${sample.value}${series.state !== "offline" && series.unit ? ` ${series.unit}` : ""}`}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </details>
      )}
    </figure>
  );
}
