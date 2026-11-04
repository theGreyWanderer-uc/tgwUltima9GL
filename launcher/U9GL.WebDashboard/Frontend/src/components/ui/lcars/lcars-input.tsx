import type { ComponentProps } from "react";
import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import "./lcars.css";

export function LcarsInput({
  tone = "secondary",
  color,
  style,
  id,
  label,
  description,
  error,
  "aria-describedby": describedBy,
  "aria-invalid": invalid,
  className = "",
  ...props
}: Omit<ComponentProps<"input">, "color" | "style"> &
  LcarsColorProps & {
    id: string;
    label: string;
    description?: string;
    error?: string;
  }) {
  const descriptions =
    [
      describedBy,
      description ? `${id}-description` : "",
      error ? `${id}-error` : "",
    ]
      .filter(Boolean)
      .join(" ") || undefined;
  return (
    <div className="lcars-field">
      <label htmlFor={id}>{label}</label>
      <input
        {...props}
        id={id}
        aria-describedby={descriptions}
        aria-invalid={error ? true : invalid}
        style={resolveLcarsColorStyle(tone, color, style)}
        className={`lcars-input ${className}`}
      />
      {description && (
        <p id={`${id}-description`} className="lcars-field-description">
          {description}
        </p>
      )}
      {error && (
        <p id={`${id}-error`} className="lcars-field-error" role="alert">
          {error}
        </p>
      )}
    </div>
  );
}
