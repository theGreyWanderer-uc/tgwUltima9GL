import type { LcarsPanelBand } from "./components/ui/lcars/lcars-geometry";
import type { LcarsSystemState } from "./components/ui/lcars/lcars-telemetry";
import type { LcarsTone } from "./components/ui/lcars/lcars-theme-tokens";

export const pageLetters = {
  overview: "O", player: "P", rendering: "R", memory: "M", textures: "T",
  world: "W", history: "H", events: "E", metrics: "X",
} as const;
export type BandPage = keyof typeof pageLetters;

const rotation: readonly LcarsTone[] = ["primary", "info", "accent", "secondary"];

export type BandCode = { text: string; state?: LcarsSystemState } | null;

export function band(page: BandPage, index: number, code?: BandCode): LcarsPanelBand {
  return {
    tone: rotation[index % rotation.length]!,
    index: `${pageLetters[page]}-${String(index + 1).padStart(2, "0")}`,
    ...(code ? { code: code.text, codeState: code.state ?? "nominal" } : {}),
  };
}
