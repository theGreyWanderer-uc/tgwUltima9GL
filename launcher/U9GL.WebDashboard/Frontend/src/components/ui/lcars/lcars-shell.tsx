import type { ComponentProps, ReactNode } from "react";
import "./lcars.css";

export function LcarsShell({
  sidebar,
  header,
  stickySidebar = false,
  children,
  className = "",
  ...props
}: ComponentProps<"div"> & {
  sidebar: ReactNode;
  header?: ReactNode;
  stickySidebar?: boolean;
}) {
  return (
    <div
      {...props}
      data-sticky-sidebar={stickySidebar || undefined}
      className={`lcars-shell ${className}`}
    >
      {header != null && <div className="lcars-shell-header">{header}</div>}
      <aside className="lcars-sidebar">{sidebar}</aside>
      <div className="lcars-shell-content">{children}</div>
    </div>
  );
}
