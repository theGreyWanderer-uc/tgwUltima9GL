import type { ComponentProps } from "react";
import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import "./lcars.css";

export function LcarsButton({
  tone = "primary",
  color,
  style,
  shape = "pill",
  type = "button",
  className = "",
  ...props
}: Omit<ComponentProps<"button">, "color" | "style"> &
  LcarsColorProps & {
    shape?: "pill" | "flat";
  }) {
  return (
    <button
      {...props}
      type={type}
      data-tone={tone}
      data-color={color}
      style={resolveLcarsColorStyle(tone, color, style)}
      data-shape={shape}
      className={`lcars-button ${className}`}
    />
  );
}
