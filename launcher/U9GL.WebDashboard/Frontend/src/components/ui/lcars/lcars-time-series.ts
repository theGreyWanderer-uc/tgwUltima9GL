export type LcarsTimeSample = {
  timeMs: number;
  value: number | null;
  label?: string;
};
export type LcarsTimeSeries =
  | {
      state: "ready" | "stale";
      samples: readonly LcarsTimeSample[];
      unit?: string;
    }
  | { state: "offline"; reason?: string };

export function isLcarsElapsedTime(value: number): boolean {
  return (
    Number.isFinite(value) && value >= 0 && value <= Number.MAX_SAFE_INTEGER
  );
}

export function boundLcarsTimeSamples(
  samples: readonly LcarsTimeSample[],
  windowMs = 60_000,
  maxSamples = 300,
): LcarsTimeSample[] {
  if (
    !isLcarsElapsedTime(windowMs) ||
    windowMs <= 0 ||
    !Number.isInteger(maxSamples) ||
    maxSamples < 1 ||
    maxSamples > 1000
  )
    throw new Error(
      "LCARS history requires a positive finite window and 1–1000 samples.",
    );
  const byTime = new Map<number, LcarsTimeSample>();
  for (const sample of samples)
    if (isLcarsElapsedTime(sample.timeMs)) byTime.set(sample.timeMs, sample);
  const sorted = [...byTime.values()].sort((a, b) => a.timeMs - b.timeMs);
  const end = sorted.at(-1)?.timeMs ?? 0;
  return sorted
    .filter((sample) => sample.timeMs >= end - windowMs)
    .slice(-maxSamples);
}
