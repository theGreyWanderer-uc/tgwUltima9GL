import type { CSSProperties } from "react";
import type { LcarsColorName, LcarsPaletteName } from "./lcars-palette";

export const lcarsTones = [
  "primary",
  "secondary",
  "accent",
  "success",
  "warning",
  "danger",
  "info",
] as const;

export type LcarsTone = (typeof lcarsTones)[number];

export const lcarsThemeOptions = [
  { value: "tng", label: "TNG Classic" },
  { value: "voyager", label: "Voyager" },
  { value: "lcars-2357", label: "LCARS 2357" },
  { value: "lcars-2369", label: "LCARS 2369" },
  { value: "lcars-2375", label: "LCARS 2375" },
  { value: "lcars-2379", label: "LCARS 2379" },
  { value: "lcars-alt", label: "LCARS Alternate" },
  { value: "first-contact", label: "First Contact" },
  { value: "nemesis", label: "Nemesis" },
  { value: "nx01", label: "NX-01" },
  { value: "lcars-23c", label: "23rd Century" },
  { value: "lcars-29c", label: "29th Century" },
  { value: "red-alert", label: "Red Alert" },
  { value: "cardassian", label: "Cardassian" },
] as const;

export type LcarsTheme = (typeof lcarsThemeOptions)[number]["value"];

export const lcarsThemes: readonly LcarsTheme[] = lcarsThemeOptions.map(
  (option) => option.value,
);

export function isLcarsTheme(value: unknown): value is LcarsTheme {
  return (
    typeof value === "string" && lcarsThemes.some((theme) => theme === value)
  );
}

export type LcarsThemeColorRole =
  | LcarsTone
  | "background"
  | "surface"
  | "text"
  | "heading"
  | "muted"
  | "ink"
  | "line"
  | "focus";

export type LcarsThemeColorValue = LcarsColorName | `#${string}`;

export type LcarsThemeTokens = {
  palette?: LcarsPaletteName;
  colors: Record<LcarsThemeColorRole, LcarsThemeColorValue>;
  geometry: {
    radius: string;
    elbowWidth: string;
    barHeight: string;
    gap: string;
    space: string;
  };
  typography: { font: string };
};

const lcarsFontStack =
  '"Antonio Variable", "Arial Narrow", "Aptos Narrow", sans-serif';

const lcarsBaseThemes = {
  tng: {
    colors: {
      background: "#000000",
      surface: "#121019",
      text: "#F4E9DD",
      muted: "blue-bell",
      primary: "golden-tanoi",
      heading: "golden-tanoi",
      secondary: "lilac",
      accent: "anakiwa",
      success: "#99FF66",
      warning: "neon-carrot",
      danger: "red-damask",
      info: "anakiwa",
      ink: "#000000",
      line: "eggplant",
      focus: "pale-canary",
    },
    geometry: {
      radius: "2rem",
      elbowWidth: "4.5rem",
      barHeight: "1rem",
      gap: "0.5rem",
      space: "1.5rem",
    },
    typography: { font: lcarsFontStack },
  },
  voyager: {
    colors: {
      background: "#040606",
      surface: "#10191B",
      text: "#EDF5F2",
      muted: "blue-bell",
      primary: "tanoi",
      heading: "tanoi",
      secondary: "blue-bell",
      accent: "melrose",
      success: "#99FF66",
      warning: "golden-tanoi",
      danger: "red-damask",
      info: "anakiwa",
      ink: "#000000",
      line: "eggplant",
      focus: "pale-canary",
    },
    geometry: {
      radius: "1.25rem",
      elbowWidth: "4.5rem",
      barHeight: "0.75rem",
      gap: "0.5rem",
      space: "1.5rem",
    },
    typography: { font: lcarsFontStack },
  },
} as const satisfies Record<"tng" | "voyager", LcarsThemeTokens>;

function createLcarsPaletteTheme(
  palette: LcarsPaletteName,
  primary: LcarsThemeColorValue,
  secondary: LcarsThemeColorValue,
  accent: LcarsThemeColorValue,
  options: {
    heading?: LcarsThemeColorValue;
    geometry?: Partial<LcarsThemeTokens["geometry"]>;
  } = {},
): LcarsThemeTokens {
  return {
    palette,
    colors: {
      ...lcarsBaseThemes.tng.colors,
      primary,
      secondary,
      accent,
      info: accent,
      heading: options.heading ?? primary,
    },
    geometry: { ...lcarsBaseThemes.tng.geometry, ...options.geometry },
    typography: lcarsBaseThemes.tng.typography,
  };
}

export const lcarsThemeTokens = {
  ...lcarsBaseThemes,
  "lcars-2357": createLcarsPaletteTheme(
    "2357",
    "pale-canary",
    "golden-tanoi",
    "anakiwa",
  ),
  "lcars-2369": createLcarsPaletteTheme(
    "2369",
    "atomic-tangerine",
    "melrose",
    "hopbush",
  ),
  "lcars-2375": createLcarsPaletteTheme(
    "2375",
    "sandy-brown",
    "lavender-purple",
    "danub",
  ),
  "lcars-2379": createLcarsPaletteTheme(
    "2379",
    "periwinkle",
    "dodger-pale",
    "husk",
  ),
  "lcars-alt": createLcarsPaletteTheme("alt", "#FF9C00", "#9C9CFF", "#DDFFFF"),
  "first-contact": createLcarsPaletteTheme(
    "first-contact",
    "#C198B0",
    "#BEBCDF",
    "#C2B74B",
  ),
  nemesis: createLcarsPaletteTheme("nemesis", "#4BB0FF", "#87EEFF", "#DFAF71", {
    geometry: { radius: "0.75rem", barHeight: "0.75rem" },
  }),
  nx01: createLcarsPaletteTheme("nx01", "#BDEFFF", "#DEFFB5", "#CD6363", {
    geometry: { radius: "0.75rem" },
  }),
  "lcars-23c": createLcarsPaletteTheme("23c", "anakiwa", "#66CCFF", "#99FF66"),
  "lcars-29c": createLcarsPaletteTheme("29c", "#39C924", "#72E2E4", "#BC956E", {
    geometry: { radius: "2.5rem", barHeight: "0.75rem" },
  }),
  "red-alert": createLcarsPaletteTheme(
    "red-alert",
    "#BF2D42",
    "#F1DFBF",
    "#D9D5B8",
    { heading: "#F1DFBF" },
  ),
  cardassian: createLcarsPaletteTheme(
    "cardassian",
    "#F9AB3C",
    "#FFE705",
    "#66FFFF",
    { geometry: { radius: "0.5rem", barHeight: "0.75rem" } },
  ),
} as const satisfies Record<LcarsTheme, LcarsThemeTokens>;

export type LcarsStyle = CSSProperties & {
  [token: `--lcars-${string}`]: string | number;
};
