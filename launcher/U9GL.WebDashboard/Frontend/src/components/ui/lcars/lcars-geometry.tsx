import type { ComponentProps, ReactNode } from "react";
import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import type { LcarsTone } from "./lcars-theme-tokens";
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

export type LcarsPanelBand = {
  tone?: LcarsTone;
  index?: ReactNode;
  code?: ReactNode;
  codeState?: "nominal" | "warning" | "critical" | "offline";
};

export function LcarsPanel({
  headingLevel: Heading = "h2",
  tone = "secondary",
  color,
  style,
  title,
  id,
  band,
  children,
  className = "",
  ...props
}: Omit<ComponentProps<"section">, "title" | "id" | "color" | "style"> &
  LcarsColorProps & {
    id: string;
    title: ReactNode;
    headingLevel?: "h2" | "h3" | "h4";
    band?: LcarsPanelBand;
  }) {
  const inherit = { "--lcars-element-color": "inherit" };
  return (
    <section
      {...props}
      id={id}
      aria-labelledby={`${id}-title`}
      style={resolveLcarsColorStyle(tone, color, style)}
      className={`lcars-panel ${className}`}
    >
      {band ? (
        <div
          className="lcars-panel-heading"
          data-variant="band"
          style={resolveLcarsColorStyle(band.tone ?? tone)}
        >
          <LcarsCap end="start" style={inherit} className="lcars-band-cap" />
          {band.index != null && (
            <span className="lcars-band-index" aria-hidden="true">
              {band.index}
            </span>
          )}
          <Heading id={`${id}-title`}>{title}</Heading>
          <LcarsBar style={inherit} className="lcars-band-fill" />
          {band.code != null && (
            <span className="lcars-band-code" data-state={band.codeState ?? "nominal"}>
              {band.code}
            </span>
          )}
          <LcarsCap end="end" style={inherit} className="lcars-band-cap" />
        </div>
      ) : (
        <div className="lcars-panel-heading">
          <Heading id={`${id}-title`}>{title}</Heading>
          <LcarsBar style={inherit} />
        </div>
      )}
      {children}
    </section>
  );
}
