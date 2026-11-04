import { LcarsReadout } from "./components/ui/lcars/lcars-status";
import { LcarsGauge } from "./components/ui/lcars/lcars-gauge";
import type { LcarsReading, LcarsSystemState } from "./components/ui/lcars/lcars-telemetry";
import type { DashboardGroup, DashboardSample, DashboardView } from "./u9gl-telemetry-contract";
import { dashboardField, dashboardFieldText, dashboardScaled } from "./u9gl-field-format";
import { formatMebibytes, U9glMemoryReadout } from "./u9gl-memory-readout";
import { U9glTextReadout } from "./u9gl-text-readout";

export type MetricContext = {
  view: DashboardView | null;
  sample: DashboardSample | null;
  reading: (group: DashboardGroup | null | undefined, field: string, unit: string) => LcarsReading;
};

export type Metric = {
  label: string; group?: string; field?: string;
   mib?: boolean;
  precision?: number; unit?: string;
   divide?: number;
   enumName?: boolean;
  text?: (values: Record<string, string>, raw: string, labels: Record<string, string>) => string;
   when?: [string, string];
  derive?: (values: Record<string, string>) => string | null;
   pending?: string;
};

export const pendingGated = "Sampled only while requested; not in the feed yet.";

export function metricReading(metric: Metric, ctx: MetricContext): LcarsReading {
  if (!metric.group || !metric.field) return { state: "offline", reason: metric.pending ?? "Not available in this telemetry feed yet." };
  const group = ctx.sample?.groups[metric.group];
  if (metric.derive) {
    const base = ctx.reading(group, metric.field, metric.unit ?? "");
    const value = group ? metric.derive(group.values) : null;
    return base.state === "offline" || value === null ? { state: "offline", reason: "No accepted reading for this group." } : { ...base, value };
  }
  const base = ctx.reading(group, metric.field, metric.unit ?? "");
  if (base.state === "offline") return base;
  if (metric.when && group?.values[metric.when[0]] !== "1") return { state: "offline", reason: metric.when[1] };
  if (metric.text || metric.enumName || metric.mib) return base;
  let value = dashboardScaled(String(base.value), dashboardField(ctx.view, metric.group, metric.field)?.scale ?? 1);
  if (metric.divide) value = String(Number(value) / metric.divide);
  return { ...base, value };
}

export function metricNumber(metric: Metric, ctx: MetricContext): number | null {
  const r = metricReading(metric, ctx);
  return r.state === "offline" ? null : Number(r.value);
}

export function MetricReadout({ metric, ctx }: { metric: Metric; ctx: MetricContext }) {
  const value = metricReading(metric, ctx);
  if (metric.mib) return <U9glMemoryReadout label={metric.label} reading={value} />;
  if (metric.text || metric.enumName) {
    const values = (metric.group && ctx.sample?.groups[metric.group]?.values) || {};
    const raw = metric.field ? values[metric.field] : undefined;
    const field = metric.group && metric.field ? dashboardField(ctx.view, metric.group, metric.field) : null;
    const text = raw === undefined ? null
      : metric.text ? metric.text(values, raw, ctx.sample?.labels ?? {}) : dashboardFieldText(field, raw);
    return <U9glTextReadout label={metric.label} reading={value} text={text} />;
  }
  return <LcarsReadout label={metric.label} reading={value} precision={metric.precision ?? 0} />;
}

export function U9glCapacity({ label, used, total, state, count = false }: {
  label: string; used: string | null; total: string | null; state: (percent: number) => LcarsSystemState; count?: boolean;
}) {
  if (used === null || total === null || BigInt(total) === 0n)
    return <div className="u9gl-capacity" data-condition="offline"><p className="lcars-supporting">{label} · Unavailable</p></div>;
  const percent = Number((BigInt(used) * 1000n) / BigInt(total)) / 10;
  const condition = state(percent);
  const tone = condition === "critical" ? "danger" : condition === "warning" ? "warning" : "accent";
  return (
    <div className="u9gl-capacity" data-condition={condition}>
      <LcarsGauge value={Math.round(percent)} label={label} tone={tone} />
      <p className="lcars-supporting">{count
        ? `${BigInt(used).toLocaleString("en-US")} of ${BigInt(total).toLocaleString("en-US")}`
        : `${formatMebibytes(used)} of ${formatMebibytes(total)} MiB`}</p>
    </div>
  );
}
