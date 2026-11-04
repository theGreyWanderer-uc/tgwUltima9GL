export type LcarsNumericValue = number | bigint | string;

export function isLcarsNumericValue(
  value: unknown,
): value is LcarsNumericValue {
  return (
    typeof value === "bigint" ||
    (typeof value === "number" &&
      Number.isFinite(value) &&
      Math.abs(value) <= Number.MAX_SAFE_INTEGER) ||
    (typeof value === "string" && /^-?(?:0|[1-9]\d*)(?:\.\d+)?$/.test(value))
  );
}

function decimalParts(value: LcarsNumericValue) {
  const text = String(value);
  const negative = text.startsWith("-");
  const [coefficient = "0", exponent = "0"] = text
    .replace(/^-/, "")
    .split(/[eE]/);
  const [integer = "0", fraction = ""] = coefficient.split(".");
  const digits = integer + fraction;
  const point = integer.length + Number(exponent);
  return {
    negative,
    integer: point <= 0 ? "0" : digits.slice(0, point).padEnd(point, "0"),
    fraction: point <= 0 ? "0".repeat(-point) + digits : digits.slice(point),
  };
}

export function formatLcarsReading(
  value: LcarsNumericValue | null,
  unit = "",
  precision = 1,
): string {
  if (!isLcarsNumericValue(value)) return "Unavailable";
  const count = Number.isFinite(precision)
    ? Math.min(20, Math.max(0, Math.round(precision)))
    : 1;
  const parts = decimalParts(value);
  let rounded = BigInt(
    parts.integer + parts.fraction.padEnd(count, "0").slice(0, count),
  );
  if ((parts.fraction[count] ?? "0") >= "5") rounded += 1n;
  const scale = 10n ** BigInt(count);
  const integer = new Intl.NumberFormat("en-US").format(rounded / scale);
  const fraction = count
    ? (rounded % scale).toString().padStart(count, "0").replace(/0+$/, "")
    : "";
  const result = `${parts.negative && rounded !== 0n ? "-" : ""}${integer}${fraction ? `.${fraction}` : ""}`;
  return unit ? `${result} ${unit}` : result;
}

export function compareLcarsNumericValues(
  left: LcarsNumericValue | null,
  right: LcarsNumericValue | null,
): number {
  if (!isLcarsNumericValue(left)) return isLcarsNumericValue(right) ? 1 : 0;
  if (!isLcarsNumericValue(right)) return -1;
  const a = decimalParts(left),
    b = decimalParts(right);
  const length = Math.max(a.fraction.length, b.fraction.length);
  const first = BigInt(
    `${a.negative ? "-" : ""}${a.integer}${a.fraction.padEnd(length, "0")}`,
  );
  const second = BigInt(
    `${b.negative ? "-" : ""}${b.integer}${b.fraction.padEnd(length, "0")}`,
  );
  return first < second ? -1 : first > second ? 1 : 0;
}
