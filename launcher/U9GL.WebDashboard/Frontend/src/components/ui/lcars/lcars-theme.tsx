import type { ComponentProps } from "react";
import type { LcarsStyle, LcarsTheme } from "./lcars-theme-tokens";
import "./lcars.css";

export { lcarsThemes, type LcarsTheme } from "./lcars-theme-tokens";

export function LcarsThemeProvider({
  theme = "tng",
  density = "comfortable",
  className = "",
  ...props
}: Omit<ComponentProps<"div">, "style"> & {
  theme?: LcarsTheme;
  density?: "comfortable" | "compact";
  style?: LcarsStyle;
}) {
  return (
    <div
      {...props}
      className={`lcars ${className}`}
      data-lcars-theme={theme}
      data-lcars-density={density}
    />
  );
}
