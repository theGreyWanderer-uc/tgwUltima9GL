import type { ComponentProps, ReactNode } from "react";
import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import "./lcars.css";

export function LcarsCap({
  end = "both",
  tone = "primary",
  color,
  style,
  className = "",
  ...props
}: Omit<ComponentProps<"div">, "color" | "style"> &
  LcarsColorProps & { end?: "start" | "end" | "both" }) {
  return (
    <div
      {...props}
      aria-hidden="true"
      data-end={end}
      style={resolveLcarsColorStyle(tone, color, style)}
      className={`lcars-cap ${className}`}
    />
  );
}

export function LcarsDivider({
  orientation = "horizontal",
  tone = "secondary",
  color,
  style,
  className = "",
  ...props
}: Omit<ComponentProps<"div">, "color" | "style" | "role"> &
  LcarsColorProps & { orientation?: "horizontal" | "vertical" }) {
  return (
    <div
      {...props}
      role="separator"
      aria-orientation={orientation}
      style={resolveLcarsColorStyle(tone, color, style)}
      className={`lcars-divider ${className}`}
    />
  );
}

export function LcarsElbow({
  tone = "primary",
  color,
  style,
  corner = "top-left",
  className = "",
  ...props
}: Omit<ComponentProps<"div">, "color" | "style"> &
  LcarsColorProps & {
    corner?: "top-left" | "top-right" | "bottom-left" | "bottom-right";
  }) {
  return (
    <div
      {...props}
      aria-hidden="true"
      data-corner={corner}
      style={resolveLcarsColorStyle(tone, color, style)}
      className={`lcars-elbow ${className}`}
    />
  );
}

export function LcarsBar({
  tone = "secondary",
  color,
  style,
  orientation = "horizontal",
  className = "",
  ...props
}: Omit<ComponentProps<"div">, "color" | "style"> &
  LcarsColorProps & { orientation?: "horizontal" | "vertical" }) {
  return (
    <div
      {...props}
      aria-hidden="true"
      data-orientation={orientation}
      style={resolveLcarsColorStyle(tone, color, style)}
      className={`lcars-bar ${className}`}
    />
  );
}

export function LcarsFrame({
  tone = "primary",
  color,
  style,
  className = "",
  children,
  ...props
}: Omit<ComponentProps<"div">, "color" | "style"> & LcarsColorProps) {
  return (
    <div
      {...props}
      style={resolveLcarsColorStyle(tone, color, style)}
      className={`lcars-frame ${className}`}
    >
      <LcarsElbow style={{ "--lcars-element-color": "inherit" }} />
      <div className="lcars-frame-content">{children}</div>
      <LcarsElbow
        corner="bottom-left"
        style={{ "--lcars-element-color": "inherit" }}
      />
    </div>
  );
}

export function LcarsPanel({
  headingLevel: Heading = "h2",
  tone = "secondary",
  color,
  style,
  title,
  id,
  children,
  className = "",
  ...props
}: Omit<ComponentProps<"section">, "title" | "id" | "color" | "style"> &
  LcarsColorProps & {
    id: string;
    title: ReactNode;
    headingLevel?: "h2" | "h3" | "h4";
  }) {
  return (
    <section
      {...props}
      id={id}
      aria-labelledby={`${id}-title`}
      style={resolveLcarsColorStyle(tone, color, style)}
      className={`lcars-panel ${className}`}
    >
      <div className="lcars-panel-heading">
        <Heading id={`${id}-title`}>{title}</Heading>
        <LcarsBar style={{ "--lcars-element-color": "inherit" }} />
      </div>
      {children}
    </section>
  );
}
