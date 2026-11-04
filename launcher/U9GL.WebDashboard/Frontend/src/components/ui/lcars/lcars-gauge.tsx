import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import "./lcars.css";

export function LcarsGauge({
  value,
  label,
  tone = "accent",
  color,
  style,
}: { value: number; label: string } & LcarsColorProps) {
  const percent = Number.isFinite(value)
    ? Math.min(100, Math.max(0, value))
    : 0;
  return (
    <div
      className="lcars-gauge"
      style={resolveLcarsColorStyle(tone, color, style)}
      role="meter"
      aria-label={label}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={percent}
      aria-valuetext={`${percent}%`}
    >
      <svg viewBox="0 0 120 120" aria-hidden="true">
        <circle className="lcars-gauge-track" cx="60" cy="60" r="49" />
        <circle
          className="lcars-gauge-fill"
          cx="60"
          cy="60"
          r="49"
          pathLength="100"
          strokeDasharray={`${percent} 100`}
          transform="rotate(-90 60 60)"
        />
        <text x="60" y="66" textAnchor="middle">
          {percent}%
        </text>
      </svg>
      <span>{label}</span>
    </div>
  );
}
