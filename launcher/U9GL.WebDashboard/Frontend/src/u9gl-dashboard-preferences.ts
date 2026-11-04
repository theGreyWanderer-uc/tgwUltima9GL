import { useEffect, useState } from "react";
import { isLcarsTheme, type LcarsTheme } from "./components/ui/lcars/lcars-theme-tokens";

type DashboardAppearance = { theme: LcarsTheme; density: "comfortable" | "compact"; freezeNavigation: boolean };
const appearanceKey = "u9gl.dashboard.appearance.v1";
const defaultAppearance: DashboardAppearance = { theme: "tng", density: "comfortable", freezeNavigation: true };

function loadDashboardAppearance(): DashboardAppearance {
  try {
    const saved: unknown = JSON.parse(window.localStorage.getItem(appearanceKey) ?? "null");
    if (typeof saved !== "object" || saved === null || !("version" in saved) || saved.version !== 1)
      return defaultAppearance;
    return {
      theme: "theme" in saved && isLcarsTheme(saved.theme) ? saved.theme : defaultAppearance.theme,
      density: "density" in saved && (saved.density === "comfortable" || saved.density === "compact") ? saved.density : defaultAppearance.density,
      freezeNavigation: "freezeNavigation" in saved && typeof saved.freezeNavigation === "boolean" ? saved.freezeNavigation : defaultAppearance.freezeNavigation,
    };
  } catch { return defaultAppearance; }
}

export function useDashboardAppearance() {
  const [appearance, setAppearance] = useState(loadDashboardAppearance);
  useEffect(() => {
    try { window.localStorage.setItem(appearanceKey, JSON.stringify({ version: 1, ...appearance })); }
    catch {  }
  }, [appearance]);
  return [appearance, (change: Partial<DashboardAppearance>) => setAppearance(previous => ({ ...previous, ...change }))] as const;
}
