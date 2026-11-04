import { useEffect, useState } from "react";
import { parseDashboardView, type DashboardView } from "./u9gl-telemetry-contract";
import type { DashboardTransport } from "./u9gl-dashboard-state";
import { apiUrl } from "./u9gl-api";

const reloadKey = "u9gl.dashboard.reloadedAt";
let loadedBuild: string | null = null;
function reloadForNewerLauncher(): boolean {
  try {
    const last = Number(sessionStorage.getItem(reloadKey) ?? "0");
    if (Date.now() - last < 30_000) return false;
    sessionStorage.setItem(reloadKey, String(Date.now()));
  } catch { return false;  }
  window.location.reload();
  return true;
}

export function useDashboardTelemetry() {
  const [view, setView] = useState<DashboardView | null>(null);
  const [transport, setTransport] = useState<DashboardTransport>("connecting");
  const [transportDetail, setTransportDetail] = useState("Connecting to the launcher");
  const [receivedAt, setReceivedAt] = useState<number | null>(null);
  const [now, setNow] = useState(performance.now());
  useEffect(() => {
    const source = new EventSource(apiUrl("/api/v1/events"));
    const receive = (event: MessageEvent<string>) => {
      try {
        const accepted = parseDashboardView(event.data);
        if (loadedBuild !== null && accepted.build !== loadedBuild && reloadForNewerLauncher()) return;
        loadedBuild ??= accepted.build;
        setView(accepted);
        setReceivedAt(performance.now());
        setNow(performance.now());
        setTransport("connected");
        setTransportDetail("");
      } catch (error) {
        if (reloadForNewerLauncher()) return;
        setTransport("error");
        setTransportDetail(error instanceof Error ? error.message : "Invalid browser telemetry response");
      }
    };
    source.addEventListener("telemetry", receive);
    source.onerror = () => { setTransport("reconnecting"); setTransportDetail("Connection to the launcher lost; reconnecting. Retained readings continue to age."); };
    const tick = window.setInterval(() => setNow(performance.now()), 250);
    return () => { source.close(); window.clearInterval(tick); };
  }, []);
  return { view, transport, transportDetail, elapsed: receivedAt === null ? 0 : Math.max(0, now - receivedAt) };
}
