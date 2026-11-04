import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import {
  formatLcarsReading,
  getLcarsPercent,
  type LcarsTelemetrySeries,
} from "./lcars-telemetry";
import "./lcars.css";

export type LcarsGraphProps = {
  label: string;
  series: LcarsTelemetrySeries;
  min?: number;
  max?: number;
} & LcarsColorProps;

export function LcarsBarGraph({
  label,
  series,
  min = 0,
  max = 100,
  tone = "accent",
  color,
  style,
}: LcarsGraphProps) {
  getLcarsPercent(null, min, max);
  return (
    <figure
      className="lcars-bar-graph"
      aria-label={label}
      style={resolveLcarsColorStyle(tone, color, style)}
    >
      <figcaption>
        {label}{" "}
        <span>
          {series.state === "stale"
            ? "Stale data"
            : series.state === "offline"
              ? "Offline"
              : "Current data"}
        </span>
      </figcaption>
      {series.state === "offline" ? (
        <p className="lcars-supporting">
          {series.reason || "Telemetry unavailable."}
        </p>
      ) : series.samples.length === 0 ? (
        <p className="lcars-supporting">No samples.</p>
      ) : (
        <div className="lcars-bar-samples">
          {series.samples.map((sample, index) => {
            const percent = getLcarsPercent(sample.value, min, max);
            const clipped =
              percent !== null &&
              sample.value !== null &&
              (sample.value < min || sample.value > max);
            const text = formatLcarsReading(sample.value, series.unit);
            return (
              <div
                className="lcars-bar-sample"
                key={`${sample.label}-${index}`}
              >
                <div>
                  <span>{sample.label}</span>
                  <span>
                    {text}
                    {clipped ? " · outside range" : ""}
                  </span>
                </div>
                {percent === null ? (
                  <div className="lcars-bar-missing">No reading</div>
                ) : (
                  <div
                    className="lcars-meter-track"
                    role="meter"
                    aria-label={sample.label}
                    aria-valuemin={min}
                    aria-valuemax={max}
                    aria-valuenow={Math.min(max, Math.max(min, sample.value!))}
                    aria-valuetext={`${text}${clipped ? ", outside displayed range" : ""}`}
                  >
                    <span style={{ width: `${percent}%` }} />
                  </div>
                )}
              </div>
            );
          })}
        </div>
      )}
    </figure>
  );
}

export function LcarsTrendGraph({
  label,
  series,
  min = 0,
  max = 100,
  tone = "secondary",
  color,
  style,
}: LcarsGraphProps) {
  getLcarsPercent(null, min, max);
  const samples = series.state === "offline" ? [] : series.samples;
  const segments: string[][] = [];
  let segment: string[] = [];
  samples.forEach((sample, index) => {
    const percent = getLcarsPercent(sample.value, min, max);
    if (percent === null) {
      if (segment.length) segments.push(segment);
      segment = [];
      return;
    }
    const x =
      samples.length === 1 ? 160 : 12 + (index / (samples.length - 1)) * 296;
    segment.push(`${x.toFixed(2)},${(108 - percent * 0.96).toFixed(2)}`);
  });
  if (segment.length) segments.push(segment);
  const count = samples.filter(
    (sample) => getLcarsPercent(sample.value, min, max) !== null,
  ).length;
  return (
    <figure
      className="lcars-trend-graph"
      style={resolveLcarsColorStyle(tone, color, style)}
    >
      <figcaption>
        {label}{" "}
        <span>
          {series.state === "stale"
            ? "Stale data"
            : series.state === "offline"
              ? "Offline"
              : "Current data"}
        </span>
      </figcaption>
      {count === 0 ? (
        <p className="lcars-supporting">
          {series.state === "offline"
            ? series.reason || "Telemetry unavailable."
            : "No available samples."}
        </p>
      ) : (
        <svg
          viewBox="0 0 320 120"
          role="img"
          aria-label={`${label}. ${count} available samples. Display range ${min} to ${max}${series.state !== "offline" && series.unit ? ` ${series.unit}` : ""}. Open the data table for exact values.`}
        >
          <path
            className="lcars-graph-grid"
            d="M12 12H308M12 60H308M12 108H308M12 12V108M160 12V108M308 12V108"
          />
          {segments.map((points, index) => (
            <g key={index}>
              <polyline
                className="lcars-graph-line"
                points={points.join(" ")}
              />
              {points.map((point) => {
                const [cx, cy] = point.split(",");
                return (
                  <circle
                    key={point}
                    className="lcars-graph-point"
                    cx={cx}
                    cy={cy}
                    r="3"
                  />
                );
              })}
            </g>
          ))}
        </svg>
      )}
      {series.state !== "offline" && samples.length > 0 && (
        <details className="lcars-data-details">
          <summary>View {label} data</summary>
          <div className="lcars-table-wrap">
            <table className="lcars-table">
              <caption>{label} · exact samples</caption>
              <thead>
                <tr>
                  <th>Sample</th>
                  <th>Value</th>
                </tr>
              </thead>
              <tbody>
                {samples.map((sample, index) => (
                  <tr key={`${sample.label}-${index}`}>
                    <th scope="row">{sample.label}</th>
                    <td>{formatLcarsReading(sample.value, series.unit)}</td>
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
