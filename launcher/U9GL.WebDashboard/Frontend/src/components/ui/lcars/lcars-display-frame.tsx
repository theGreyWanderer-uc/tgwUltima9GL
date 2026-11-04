import type { ComponentProps, ReactNode } from "react";
import { LcarsShell } from "./lcars-shell";
import { resolveLcarsColorStyle, type LcarsColorProps } from "./lcars-color";
import "./lcars.css";

export type LcarsDisplayFrameProps = Omit<
  ComponentProps<"div">,
  "title" | "color" | "style"
> &
  LcarsColorProps & {
    title: ReactNode;
    code?: ReactNode;
    caption?: ReactNode;
    sidebar: ReactNode;
    stickySidebar?: boolean;
    headingLevel?: "h1" | "h2" | "h3";
  };

export function LcarsDisplayFrame({
  title,
  code,
  caption,
  sidebar,
  stickySidebar = false,
  headingLevel: Heading = "h2",
  tone = "primary",
  color,
  style,
  className = "",
  children,
  ...props
}: LcarsDisplayFrameProps) {
  return (
    <LcarsShell
      {...props}
      className={`lcars-display-frame ${className}`}
      style={resolveLcarsColorStyle(tone, color, style)}
      stickySidebar={stickySidebar}
      header={
        <header className="lcars-display-header">
          <div className="lcars-display-title-band">
            <Heading className="lcars-display-title">{title}</Heading>
            <span className="lcars-display-header-fill" aria-hidden="true" />
            {code != null && <p className="lcars-display-code">{code}</p>}
          </div>
        </header>
      }
      sidebar={sidebar}
    >
      <div className="lcars-display-body">
        {caption != null && <p className="lcars-display-caption">{caption}</p>}
        {children}
      </div>
    </LcarsShell>
  );
}
