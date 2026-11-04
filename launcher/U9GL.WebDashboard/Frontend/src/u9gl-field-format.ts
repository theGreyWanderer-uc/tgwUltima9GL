import type { DashboardField, DashboardView } from "./u9gl-telemetry-contract.ts";

export function dashboardField(view: DashboardView | null, group: string, name: string): DashboardField | null {
  return view?.schema[group]?.find(f => f.name === name) ?? null;
}

export function dashboardScaled(value: string, scale: number): string {
  if (scale === 1) return value;
  const digits = Math.round(Math.log10(scale));
  if (10 ** digits !== scale) return String(Number(value) / scale);
  const negative = value.startsWith("-");
  const magnitude = (negative ? value.slice(1) : value).padStart(digits + 1, "0");
  const whole = magnitude.slice(0, -digits), fraction = magnitude.slice(-digits).replace(/0+$/, "");
  return `${negative ? "-" : ""}${whole}${fraction ? "." + fraction : ""}`;
}

export function dashboardFieldText(field: DashboardField | null, value: string): string {
  if (field?.kind === "flag") return value === "0" ? "no" : "yes";
  if (field?.kind === "enum") return field.names?.[Number(value)] ?? `unknown (${value})`;
  return value;
}
