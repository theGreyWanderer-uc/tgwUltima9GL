import { useEffect, useState } from "react";
import { LcarsThemeProvider } from "./components/ui/lcars/lcars-theme";
import { isLcarsTheme, lcarsThemeOptions } from "./components/ui/lcars/lcars-theme-tokens";
import { LcarsDisplayFrame } from "./components/ui/lcars/lcars-display-frame";
import { LcarsSidebar } from "./components/ui/lcars/lcars-layout";
import { LcarsPanel } from "./components/ui/lcars/lcars-geometry";
import { LcarsFeedStatus } from "./components/ui/lcars/lcars-feed-status";
import { LcarsTimeTrend } from "./components/ui/lcars/lcars-time-trend";
import { LcarsDataTable, type LcarsDataColumn } from "./components/ui/lcars/lcars-data-table";
import type { LcarsReading } from "./components/ui/lcars/lcars-telemetry";
import type { DashboardGroup } from "./u9gl-telemetry-contract";
import { dashboardSessionKey, dashboardGroupStale, dashboardCollectedTrend, dashboardPublicationTrend, dashboardFeedState } from "./u9gl-dashboard-state";
import { useDashboardTelemetry } from "./u9gl-telemetry-client";
import { U9glOverview, U9glSectionPage, metricLabels, type SectionPage } from "./u9gl-pages";
import type { MetricContext } from "./u9gl-metrics";
import { dashboardFieldText, dashboardScaled } from "./u9gl-field-format";
import { useDashboardAppearance } from "./u9gl-dashboard-preferences";
import { U9glEvents } from "./u9gl-events";
import { U9glPageGuard } from "./u9gl-page-guard";
import { U9glPlayerPage } from "./u9gl-player";
import { band } from "./u9gl-band";

type DashboardPage = "overview" | "player" | SectionPage | "history" | "events" | "metrics";
const dashboardPages: readonly { id: DashboardPage; title: string; code: string }[] = [
  { id: "overview", title: "Overview", code: "01" },
  { id: "player", title: "Player", code: "02" },
  { id: "rendering", title: "Rendering", code: "03" },
  { id: "memory", title: "Memory", code: "04" },
  { id: "textures", title: "Textures", code: "05" },
  { id: "world", title: "World", code: "06" },
  { id: "history", title: "History", code: "07" },
  { id: "events", title: "Events", code: "08" },
  { id: "metrics", title: "Exact values", code: "09" },
];
const legacyPages: Record<string, DashboardPage> = { "#general": "overview", "#objects": "world" };
const sectionPageIds: readonly string[] = ["rendering", "memory", "textures", "world"];
const isSectionPage = (page: DashboardPage): page is SectionPage => sectionPageIds.includes(page);
function pageFromHash(hash: string): DashboardPage | null {
  return legacyPages[hash] ?? dashboardPages.find(page => "#" + page.id === hash)?.id ?? null;
}

const historyCharts = {
  privateBytes: { label: "Private memory (MiB)", key: "memory.privateBytes", scale: 1 / 1048576, unit: "MiB", collected: true },
  workingSet: { label: "Working set (MiB)", key: "memory.workingSet", scale: 1 / 1048576, unit: "MiB", collected: true },
  glTextureBytes: { label: "GL texture allocation (MiB)", key: "resources.textureBytes", scale: 1 / 1048576, unit: "MiB", collected: false },
  glTexturesAlive: { label: "GL textures alive", key: "resources.texturesAlive", scale: 1, unit: "", collected: false },
  floor: { label: "Memory floor (KB / frame)", key: "memoryTrend.floorSlopeMilli", scale: 1 / 1024000, unit: "KB/frame", collected: true },
  fps: { label: "FPS", key: "pacing.fpsMilli", scale: 1e-3, unit: "fps", collected: true },
  textureCache: { label: "Texture cache (MiB)", key: "textureCache.cacheBytes", scale: 1 / 1048576, unit: "MiB", collected: true },
  evictions: { label: "Texture evictions / s", key: "textureCache.evictionsPerSec", scale: 1, unit: "", collected: true },
  draws: { label: "Draw calls · sampled last frame", key: "frames.drawCallsLast", scale: 1, unit: "", collected: false },
  meshHandles: { label: "Mesh handles", key: "area.meshUsers", scale: 1, unit: "", collected: true },
  objects: { label: "Objects in the fast area", key: "area.objects", scale: 1, unit: "", collected: true },
  worstCell: { label: "Fullest cell (slots)", key: "area.worstCell", scale: 1, unit: "", collected: true },
  items: { label: "Items pool used (MiB)", key: "area.nonfixedUsed", scale: 1 / 1048576, unit: "MiB", collected: true },
  fixedItems: { label: "Fixed items pool used (MiB)", key: "area.fixedUsed", scale: 1 / 1048576, unit: "MiB", collected: true },
  largestBlock: { label: "Largest free address block (MiB)", key: "addressSpace.largestFreeBytes", scale: 1 / 1048576, unit: "MiB", collected: true },
  vramUse: { label: "Game's video memory (MiB)", key: "vram.localUsageBytes", scale: 1 / 1048576, unit: "MiB", collected: true },
  gpuFrame: { label: "GPU frame, sampled (ms)", key: "gpu.frameUs", scale: 1e-3, unit: "ms", collected: true },
  gpuWater: { label: "GPU water reflection, sampled (ms)", key: "gpu.waterUs", scale: 1e-3, unit: "ms", collected: true },
  gpuAo: { label: "GPU ambient occlusion, sampled (ms)", key: "gpu.aoUs", scale: 1e-3, unit: "ms", collected: true },
  gpuSun: { label: "GPU sun shadows, sampled (ms)", key: "gpu.sunShadowsUs", scale: 1e-3, unit: "ms", collected: true },
  gpuPools: { label: "GPU light-pool shadows, sampled (ms)", key: "gpu.poolShadowsUs", scale: 1e-3, unit: "ms", collected: true },
  health: { label: "Avatar hit points", key: "player.health", scale: 1, unit: "hp", collected: true },
  mana: { label: "Avatar mana", key: "player.mana", scale: 1, unit: "", collected: true },
} as const;
type HistoryChart = keyof typeof historyCharts;
const isHistoryChart = (value: string): value is HistoryChart => value in historyCharts;

type MetricRow = { id: string; label: string; value: string | null; reads: string; unit: string; quality: string };
const metricColumns: readonly LcarsDataColumn<MetricRow>[] = [
  { id: "label", header: "Metric", value: row => row.label },
  { id: "value", header: "Exact value", value: row => row.value, numeric: true, precision: 0 },
  { id: "reads", header: "Reads as", value: row => row.reads },
  { id: "unit", header: "Unit", value: row => row.unit },
  { id: "quality", header: "Quality", value: row => row.quality },
];

export function U9glDiagnosticsView() {
  const { view, transport, transportDetail, elapsed } = useDashboardTelemetry();
  const [{ theme, density, freezeNavigation }, updateAppearance] = useDashboardAppearance();
  const [page, setPage] = useState<DashboardPage>(() => pageFromHash(window.location.hash) ?? "overview");
  const [historyChart, setHistoryChart] = useState<HistoryChart>("privateBytes");
  const [historyWindow, setHistoryWindow] = useState<"short" | "long">("short");
  useEffect(() => {
    const updatePage = () => {
      const next = window.location.hash ? pageFromHash(window.location.hash) : "overview";
      if (next) { setPage(next); requestAnimationFrame(() => window.scrollTo(0, 0)); }
    };
    window.addEventListener("hashchange", updatePage);
    return () => window.removeEventListener("hashchange", updatePage);
  }, []);
  const [sessionChoice, setSessionChoice] = useState("current");
  const [selectedIds, setSelectedIds] = useState<readonly string[]>([]);
  const current = view?.current ?? null;
  const feed = dashboardFeedState(view, transport, elapsed);
  const sessions = view?.sessions ?? [];
  const selectedSession = sessions.find(s => dashboardSessionKey(s.identity) === sessionChoice);
  const activeChoice = selectedSession ? sessionChoice : "current";
  const session = selectedSession ?? sessions.at(-1);
  const history = (historyWindow === "long" ? session?.longHistory : session?.history) ?? [];
  const chart = historyCharts[historyChart];
  const chartTrend = chart.collected ? dashboardCollectedTrend(history, chart.key, chart.scale) : dashboardPublicationTrend(history, chart.key);
  const stale = (group: DashboardGroup) => dashboardGroupStale(group, elapsed, view?.state ?? "waitingForGame", transport) || feed !== "live";
  const reading = (group: DashboardGroup | null | undefined, field: string, unit: string): LcarsReading => {
    const value = group?.values[field];
    if (!group || value === undefined) return { state: "offline", reason: "No accepted reading for this group." };
    return stale(group) ? { state: "stale", value, unit, reason: `${((group.ageMs + elapsed) / 1000).toFixed(1)} s old` } : { state: "ready", value, unit };
  };
  const rows: MetricRow[] = [];
  for (const [groupName, fields] of Object.entries(view?.schema ?? {})) {
    const group = current?.groups[groupName];
    for (const field of fields) {
      const id = `${groupName}.${field.name}`, raw = group?.values[field.name] ?? null;
      const reads = raw === null ? "" : field.kind === "enum" || field.kind === "flag" ? dashboardFieldText(field, raw) : dashboardScaled(raw, field.scale);
      rows.push({ id, label: metricLabels.get(id) ?? id, value: raw, reads, unit: field.unit,
        quality: !group ? "Unavailable" : stale(group) ? "Stale" : "Current" });
    }
  }
  const ctx: MetricContext = { view, sample: current, reading };
  const groups = current?.groups;
  const trendState = session && current && dashboardSessionKey(session.identity) === dashboardSessionKey(current.identity) && feed === "live" ? "ready" : "stale";
  const chartGroup = groups?.[chart.key.slice(0, chart.key.indexOf("."))];
  const chartState = trendState === "ready" && (!chart.collected || (chartGroup && !stale(chartGroup))) ? "ready" : "stale";
  const chartMax = (points: readonly { value: number | null }[]) => Math.max(1, ...points.map(p => p.value ?? 0)) * 1.15;
  const chartMin = (points: readonly { value: number | null }[]) => Math.min(0, ...points.map(p => p.value ?? 0)) * 1.15;

  return (
    <LcarsThemeProvider theme={theme} density={density}>
      <a className="u9gl-skip-link" href="#dashboard-main">Skip to diagnostics</a>
      <LcarsDisplayFrame title="U9GL diagnostics" code="READ ONLY" caption="Ultima IX · Renderer telemetry"
        className="u9gl-display-frame" headingLevel="h1" stickySidebar={freezeNavigation} sidebar={<LcarsSidebar brand="U9GL" label="Dashboard navigation" items={[
        ...dashboardPages.map(item => ({ href: "#" + item.id, label: item.title, code: item.code, active: page === item.id })),
      ]} />}>
        <main className="u9gl-dashboard" id="dashboard-main" tabIndex={-1} data-dashboard-page={page}>
          {
}
          {page === "overview" && <>
          <section className="u9gl-controls" aria-label="Dashboard appearance">
            <label>Theme<select value={theme} onChange={e => { if (isLcarsTheme(e.target.value)) updateAppearance({ theme: e.target.value }); }}>{lcarsThemeOptions.map(t => <option key={t.value} value={t.value}>{t.label}</option>)}</select></label>
            <label>Density<select value={density} onChange={e => updateAppearance({ density: e.target.value === "compact" ? "compact" : "comfortable" })}><option value="comfortable">Comfortable</option><option value="compact">Compact</option></select></label>
            <label className="u9gl-freeze-control"><input type="checkbox" checked={freezeNavigation} onChange={e => updateAppearance({ freezeNavigation: e.target.checked })} />Freeze navigation</label>
          </section>
          <LcarsFeedStatus label="Game telemetry" state={feed} ageMs={current ? current.ageMs + elapsed : null}
            retained={current !== null && feed !== "live"} detail={transportDetail || (feed === "reconnecting" ? "No recent launcher update; retaining readings." : view?.detail ?? "Connecting to the launcher")}
            {...(current ? { sessionLabel: `PID ${current.identity.pid} · ${current.identity.sessionId.slice(0, 8)} · ${current.identity.producerKind}` } : {})} />
          </>}
          <section className="u9gl-page" aria-labelledby="dashboard-page-title">
          <h2 id="dashboard-page-title">{dashboardPages.find(item => item.id === page)!.title}</h2>
          <U9glPageGuard page={page}>
          {page === "overview" && <U9glOverview ctx={ctx} />}
          {page === "player" && <U9glPlayerPage ctx={ctx} />}
          {isSectionPage(page) && <U9glSectionPage page={page} ctx={ctx} />}
          {page === "history" && <LcarsPanel band={band("history", 0)} title="Session history" id="history-panel" headingLevel="h3">
            <div className="u9gl-history-controls">
            <label className="u9gl-session-picker">Viewing history<select value={activeChoice} onChange={e => setSessionChoice(e.target.value)}>
              <option value="current">Latest session</option>
              {sessions.map((s, i) => <option key={dashboardSessionKey(s.identity)} value={dashboardSessionKey(s.identity)}>Session {i + 1} · PID {s.identity.pid} · {s.identity.sessionId.slice(0, 8)}</option>)}
            </select></label>
            <label className="u9gl-session-picker">Window<select value={historyWindow} onChange={e => setHistoryWindow(e.target.value === "long" ? "long" : "short")}>
              <option value="short">Last 2 minutes · every publication</option><option value="long">Last 30 minutes · every 5 seconds</option>
            </select></label>
            <label className="u9gl-session-picker">Chart<select value={historyChart} onChange={e => { if (isHistoryChart(e.target.value)) setHistoryChart(e.target.value); }}>
              {Object.entries(historyCharts).map(([id, c]) => <option key={id} value={id}>{c.label}</option>)}
            </select></label>
            </div>
            <p className="lcars-copy">Two windows per session: every publication of the last 2 minutes (up to 600), or one point every 5 seconds for the last 30 minutes (up to 360). 3 sessions retained. Gaps break the line. Points of a slowly collected group represent distinct collections.</p>
            <LcarsTimeTrend label={chart.label}
              series={history.length ? { state: chartState, samples: chartTrend, ...(chart.unit ? { unit: chart.unit } : {}) } : { state: "offline", reason: "No history yet" }}
              min={chartMin(chartTrend)} max={chartMax(chartTrend)} windowMs={historyWindow === "long" ? 1_800_000 : 120_000} maxSamples={1000}
              gapAfterMs={historyWindow === "long" ? 15_000 : 2000} />
          </LcarsPanel>}
          {page === "events" && <U9glEvents />}
          {page === "metrics" && <LcarsPanel band={band("metrics", 0)} title="Exact current values" id="metrics-panel" headingLevel="h3">
            <LcarsDataTable label="Telemetry metrics" columns={metricColumns} data={{ state: "ready", rows }} getRowId={row => row.id} getRowLabel={row => row.label} pageSize={8} selection={{ selectedIds, onChange: setSelectedIds }} />
            <p className="lcars-copy">Every field of every telemetry group, as the raw 64-bit word and as its schema reads it. Texture allocation is tracked renderer memory, not total GPU VRAM.</p>
          </LcarsPanel>}
          </U9glPageGuard>
          </section>
          <footer className="u9gl-footer">Keep the launcher open while playing. Closing it stops this feed and leaves the game running.</footer>
        </main>
      </LcarsDisplayFrame>
    </LcarsThemeProvider>
  );
}
