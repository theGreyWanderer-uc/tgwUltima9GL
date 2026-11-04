import type { LcarsColorName } from "./lcars-palette";
import type { LcarsStyle, LcarsTone } from "./lcars-theme-tokens";

export type LcarsColorProps = {
  tone?: LcarsTone;
  color?: LcarsColorName;
  style?: LcarsStyle;
};

export function resolveLcarsColorStyle(
  tone: LcarsTone,
  color?: LcarsColorName,
  style?: LcarsStyle,
): LcarsStyle {
  return {
    "--lcars-element-color": color
      ? `var(--lcars-color-${color})`
      : `var(--lcars-${tone})`,
    "--lcars-element-foreground": color
      ? `var(--lcars-color-${color}-foreground)`
      : `var(--lcars-on-${tone})`,
    ...style,
  };
}
