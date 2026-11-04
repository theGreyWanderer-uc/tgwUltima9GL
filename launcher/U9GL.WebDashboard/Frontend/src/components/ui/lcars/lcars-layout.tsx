import type { ComponentProps, ReactNode } from "react";
import { LcarsBar } from "./lcars-geometry";
import { resolveLcarsColorStyle } from "./lcars-color";
import type { LcarsColorName } from "./lcars-palette";
import type { LcarsTone } from "./lcars-theme-tokens";
import "./lcars.css";

export function LcarsHeader({
  title,
  eyebrow,
  code,
  actions,
  headingLevel: Heading = "h2",
  className = "",
  ...props
}: Omit<ComponentProps<"header">, "title"> & {
  title: ReactNode;
  eyebrow?: ReactNode;
  code?: ReactNode;
  actions?: ReactNode;
  headingLevel?: "h1" | "h2" | "h3";
}) {
  return (
    <header {...props} className={`lcars-header ${className}`}>
      <div className="lcars-header-copy">
        {eyebrow && <p className="lcars-eyebrow">{eyebrow}</p>}
        <Heading>{title}</Heading>
      </div>
      {actions && <div className="lcars-header-actions">{actions}</div>}
      <div className="lcars-header-rule">
        <LcarsBar tone="primary" />
        {code && <span>{code}</span>}
        <LcarsBar tone="accent" />
      </div>
    </header>
  );
}

export type LcarsNavItem = {
  href: string;
  label: string;
  code?: string;
  active?: boolean;
  tone?: LcarsTone;
  color?: LcarsColorName;
};

export function LcarsSidebar({
  items,
  label = "Console navigation",
  brand = "LCARS",
  onNavigate,
  className = "",
  ...props
}: ComponentProps<"nav"> & {
  items: readonly LcarsNavItem[];
  label?: string;
  brand?: ReactNode;
  onNavigate?: (item: LcarsNavItem) => void;
}) {
  return (
    <nav
      {...props}
      aria-label={label}
      className={`lcars-navigation ${className}`}
    >
      <div className="lcars-sidebar-brand">{brand}</div>
      <div className="lcars-navigation-controls">
        <div className="lcars-navigation-items">
          {items.map((item) => (
            <a
              key={item.href}
              href={item.href}
              aria-current={item.active ? "location" : undefined}
              style={resolveLcarsColorStyle(
                item.tone ?? (item.active ? "primary" : "secondary"),
                item.color,
              )}
              onClick={onNavigate ? () => onNavigate(item) : undefined}
            >
              <span>{item.label}</span>
              {item.code && <small>{item.code}</small>}
            </a>
          ))}
        </div>
      </div>
      <div className="lcars-sidebar-tail" aria-hidden="true" />
    </nav>
  );
}

export function LcarsGrid({
  columns = 2,
  balance = "equal",
  className = "",
  ...props
}: ComponentProps<"div"> & {
  columns?: 1 | 2 | 3 | 4;
  balance?: "equal" | "wide-start";
}) {
  return (
    <div
      {...props}
      data-columns={columns}
      data-balance={balance}
      className={`lcars-grid ${className}`}
    />
  );
}
