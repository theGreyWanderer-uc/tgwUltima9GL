import type { ReactNode } from "react";
import { LcarsPanel } from "./components/ui/lcars/lcars-geometry";
import { LcarsStatus } from "./components/ui/lcars/lcars-status";
import { LcarsBarGraph } from "./components/ui/lcars/lcars-graphs";
import { U9glHeatmap } from "./u9gl-heatmap";
import { U9glMeshList, U9glTextureList } from "./u9gl-lists";
import type { LcarsSystemState } from "./components/ui/lcars/lcars-telemetry";
import { MetricReadout, U9glCapacity, metricNumber, metricReading, pendingGated, type Metric, type MetricContext } from "./u9gl-metrics";
import { formatMebibytes } from "./u9gl-memory-readout";
import { band, type BandCode } from "./u9gl-band";
import { levelAbove, levelBelow, thresholds, worse } from "./u9gl-thresholds";

export type SectionPage = "rendering" | "memory" | "textures" | "world";
type Section = { title: string; capacity?: (ctx: MetricContext) => ReactNode; metrics: readonly Metric[]; note?: string; wide?: boolean };

const pending = (labels: readonly string[], why = pendingGated): Metric[] => labels.map(label => ({ label, pending: why }));

const m = {
  fps: { label: "FPS", group: "pacing", field: "fpsMilli", precision: 1 },
  frameNo: { label: "Engine frames", group: "frames", field: "frameNo" },
  presents: { label: "GL presents", group: "frames", field: "presents" },
  notShown: { label: "Not shown", group: "frames", field: "frameNo", derive: v => {
    const d = BigInt(v.frameNo ?? "0") - BigInt(v.presents ?? "0");
    return (d > 0n ? d : 0n).toString();
  } },
  failed: { label: "Failed presents", group: "frames", field: "presentFailed" },
  draws: { label: "Draw calls / frame", group: "frames", field: "drawCallsLast" },
  vertices: { label: "Vertices / frame", group: "frames", field: "verticesLast" },
  privateBytes: { label: "Private memory", group: "memory", field: "privateBytes", mib: true },
  workingSet: { label: "Working set", group: "memory", field: "workingSet", mib: true },
  floor: { label: "Floor (KB / frame)", group: "memoryTrend", field: "floorSlopeMilli", divide: 1024, precision: 2 },
  verdict: { label: "Leak verdict", group: "memoryTrend", field: "floorRising", text: (v, raw) =>
    raw !== "0" ? "rising" : BigInt(v.windowSamples ?? "0") < BigInt(v.windowCapacity ?? "0") ? "warming up" : "steady" },
  evictionsPerSec: { label: "Evictions / s", group: "textureCache", field: "evictionsPerSec" },
  uploadMax: { label: "Worst upload (ms)", group: "textureCache", field: "uploadMaxUs", precision: 1 },
  cacheUsed: { label: "Cache used", group: "textureCache", field: "cacheBytes", mib: true },
  mapName: { label: "Map", group: "world", field: "map", when: ["mapValid", "No world loaded."],
    text: (v, raw, labels) => v.mapValid === "1" ? labels["world.map"] ?? `Map ${raw}` : "No world loaded" },
} satisfies Record<string, Metric>;
const avatar = (["X", "Y", "Z"] as const).map((axis): Metric => ({ label: `Avatar ${axis}`, group: "world", field: `avatar${axis}`,
  when: ["avatarValid", "No Avatar in the world yet."] }));
const overrideOff: [string, string] = ["enabled", "Overrides are off."];

const value = (ctx: MetricContext, group: string, field: string) => ctx.sample?.groups[group]?.values[field] ?? null;
const privateState = (p: number) => levelAbove(p, thresholds.privateWarning, thresholds.privateCritical);
const cellState = (p: number) => levelAbove(p, thresholds.worstCellWarning, thresholds.worstCellCritical);
const poolState = (p: number) => levelAbove(p, thresholds.itemPoolWarning, thresholds.itemPoolCritical);
const vramState = (p: number) => levelAbove(p, thresholds.vramWarning, thresholds.vramCritical);
const percentOf = (used: string | null, total: string | null) =>
  used && total && BigInt(total) > 0n ? Number(BigInt(used) * 1000n / BigInt(total)) / 10 : null;

const a = (label: string, field: string, extra: Partial<Metric> = {}): Metric => ({ label, group: "area", field, ...extra });

const worstCellCapacity = (ctx: MetricContext) => <U9glCapacity label="Fullest cell against its slots" count
  used={value(ctx, "area", "worstCell")} total={value(ctx, "area", "worstCellCapacity")} state={cellState} />;
const meshPoolCapacity = (ctx: MetricContext) => <U9glCapacity label="Mesh pool"
  used={value(ctx, "area", "meshBytesPool")} total={value(ctx, "area", "poolSize")} state={() => "nominal"} />;
const terrainCapacity = (ctx: MetricContext) => <U9glCapacity label="Terrain meshes" count
  used={value(ctx, "area", "terrainMeshes")} total={value(ctx, "area", "terrainMeshMax")} state={() => "nominal"} />;
const itemPoolCapacity = (ctx: MetricContext) => <>
  <U9glCapacity label="Items" used={value(ctx, "area", "nonfixedUsed")} total={value(ctx, "area", "nonfixedMax")} state={poolState} />
  <U9glCapacity label="Fixed items" used={value(ctx, "area", "fixedUsed")} total={value(ctx, "area", "fixedMax")} state={poolState} />
</>;
const vramCapacity = (ctx: MetricContext) => value(ctx, "vram", "source") === "0"
  ? <p className="lcars-supporting u9gl-section-note">Video memory is not reported: DXGI 1.4 (Windows 10 or later) is not available.</p>
  : <U9glCapacity label="The game's video memory against its budget" used={value(ctx, "vram", "localUsageBytes")}
    total={value(ctx, "vram", "localBudgetBytes")} state={vramState} />;
const gpuRegions = [["scene", "Scene (the engine's drawing)"], ["beam", "Light beams"], ["rays", "Sun rays"], ["bloom", "Bloom"],
  ["ao", "Ambient occlusion"], ["poolShadows", "Light-pool shadows"], ["sunShadows", "Sun shadows"], ["water", "Water reflection"],
  ["composite", "Final pass"]] as const;
const gpuBars = (ctx: MetricContext) => {
  const g = ctx.sample?.groups.gpu;
  if (!g) return <LcarsBarGraph label="GPU time by pass" series={{ state: "offline", reason: "Not sampled yet." }} />;
  if (g.values.support === "2") return <LcarsBarGraph label="GPU time by pass" series={{ state: "offline", reason: "This driver has no timer queries." }} />;
  const mask = BigInt(g.values.regionMask ?? "0");
  const samples = gpuRegions.map(([key, label], i) => ({ i, key, label })).filter(r => (mask >> BigInt(r.i)) & 1n)
    .map(r => ({ label: r.label, value: Number(g.values[`${r.key}Us`] ?? "0") }));
  return <>
    <LcarsBarGraph label={`GPU time by pass · frame ${g.values.frameNo}, sampled ${(g.ageMs / 1000).toFixed(1)} s before this view`} series={{ state: g.quality === "stale" ? "stale" : "ready", samples, unit: "µs" }}
      max={Math.max(1, ...samples.map(s => s.value ?? 0))} />
    <p className="lcars-supporting u9gl-section-note">Not drawn in that frame: {gpuRegions.filter((_, i) => !((mask >> BigInt(i)) & 1n))
      .map(([, label]) => label).join(", ") || "none"}.</p>
  </>;
};

const topTypes = (ctx: MetricContext) => {
  const g = ctx.sample?.groups.area;
  if (!g) return <LcarsBarGraph label="Commonest object types" series={{ state: "offline", reason: "Area not sampled yet." }} />;
  const samples = [1, 2, 3].filter(i => g.values[`topCount${i}`] !== "0")
    .map(i => ({ label: `Type ${g.values[`topType${i}`]}`, value: Number(g.values[`topCount${i}`]) }));
  return <LcarsBarGraph label="Commonest object types" series={{ state: g.quality === "stale" ? "stale" : "ready", samples, unit: "objects" }}
    max={Math.max(1, ...samples.map(s => s.value ?? 0))} />;
};

const privateCapacity = (ctx: MetricContext) => <U9glCapacity label={`Private memory of ${ctx.view?.game
  ? `${ctx.view.game.executable}'s address space` : "the address space"}`}
  used={value(ctx, "memory", "privateBytes")} total={ctx.view?.game?.addressSpaceBytes ?? null} state={privateState} />;
const textureCapacity = (ctx: MetricContext) => <U9glCapacity label="Texture cache against its budget"
  used={value(ctx, "textureCache", "cacheBytes")} total={value(ctx, "textureCache", "budgetBytes")} state={() => "nominal"} />;

export const sectionPages: Record<SectionPage, readonly Section[]> = {
  rendering: [
    { title: "Frame pacing", metrics: [m.fps, m.frameNo, m.presents, m.notShown, m.failed] },
    { title: "Per frame", metrics: [m.draws, m.vertices] },
    { title: "Settings", metrics: [
      { label: "Mip filter", group: "renderer", field: "mipMode", enumName: true },
      { label: "Anisotropy", group: "renderer", field: "anisotropyMilli", unit: "x", precision: 0 },
      { label: "Depth mode", group: "renderer", field: "depthMode", enumName: true },
      { label: "GL buffers alive", group: "renderer", field: "buffersAlive" },
      { label: "GL buffer memory", group: "renderer", field: "bufferBytes", mib: true },
      { label: "FBOs alive", group: "renderer", field: "fbosAlive" },
    ], note: "Buffers and FBOs are not tracked yet, so they read zero." },
    { title: "GPU time per pass", capacity: gpuBars, metrics: [
      { label: "GPU frame (ms)", group: "gpu", field: "frameUs", precision: 2 },
      { label: "Sampled frame", group: "gpu", field: "frameNo" },
      { label: "Samples taken", group: "gpu", field: "samples" },
      { label: "Samples skipped (ring full)", group: "gpu", field: "skipped" },
      { label: "Timer queries", group: "gpu", field: "support", enumName: true }],
      note: "One sampled frame at a time, about four a second, read once the GPU has finished it: not an average over every frame. The frame runs from the start of the offscreen frame to the end of the final pass; the swap is not included." },
  ],
  memory: [
    { title: "Process", capacity: privateCapacity, metrics: [m.privateBytes, m.workingSet,
      { label: "Peak working set", group: "memory", field: "peakWorkingSet", mib: true },
      { label: "Pagefile", group: "memory", field: "pagefile", mib: true }] },
    { title: "Leak indicator", metrics: [m.floor,
      { label: "First-to-last (KB / frame)", group: "memoryTrend", field: "rawSlopeMilli", divide: 1024, precision: 2 },
      m.verdict,
      { label: "Window samples", group: "memoryTrend", field: "windowSamples" },
      { label: "Floor span (frames)", group: "memoryTrend", field: "spanFrames" }],
      note: "The floor is the low-water mark of the memory window moving; rising needs both measures to agree, after warm-up." },
    { title: "Address space", metrics: [
      { label: "Free address space", group: "addressSpace", field: "freeBytes", mib: true },
      { label: "Largest free block", group: "addressSpace", field: "largestFreeBytes", mib: true },
      { label: "Walk time (ms)", group: "addressSpace", field: "walkUs", precision: 2 }],
      note: "The largest free block decides whether one big allocation succeeds; the total can look healthy while it cannot." },
  ],
  textures: [
    { title: "Texture cache", capacity: textureCapacity, metrics: [m.cacheUsed,
      { label: "Budget", group: "textureCache", field: "budgetBytes", mib: true },
      { label: "Textures cached", group: "textureCache", field: "cached" },
      { label: "Pending uploads", group: "textureCache", field: "pending" },
      { label: "Uploads / s", group: "textureCache", field: "uploadsPerSec" }, m.evictionsPerSec, m.uploadMax,
      { label: "Uploads (session)", group: "textureCache", field: "uploads" },
      { label: "Duplicates (session)", group: "textureCache", field: "duplicates" },
      { label: "Evictions (session)", group: "textureCache", field: "evictions" }],
      note: "The cache runs at its budget by design; sustained evictions are the signal." },
    { title: "Texture overrides", metrics: [
      { label: "Override status", group: "overrides", field: "enabled", text: (_, raw) => raw === "0" ? "off" : "on" },
      { label: "Files", group: "overrides", field: "files", when: overrideOff },
      { label: "Used", group: "overrides", field: "used", when: overrideOff },
      { label: "Rejected", group: "overrides", field: "rejected", when: overrideOff },
      { label: "Worst load (ms)", group: "overrides", field: "loadMaxUs", precision: 1, when: overrideOff },
      { label: "Resident overrides", group: "overrideResident", field: "count" },
      { label: "Resident override memory", group: "overrideResident", field: "bytes", mib: true }] },
    { title: "Video memory", capacity: vramCapacity, metrics: [
      { label: "Reported by", group: "vram", field: "source", enumName: true },
      { label: "Card memory as a 32-bit game sees it", group: "vram", field: "dedicatedBytes", mib: true },
      { label: "Game's use", group: "vram", field: "localUsageBytes", mib: true },
      { label: "Game's budget", group: "vram", field: "localBudgetBytes", mib: true },
      { label: "Shared memory use", group: "vram", field: "sharedUsageBytes", mib: true },
      { label: "Shared memory budget", group: "vram", field: "sharedBudgetBytes", mib: true },
      { label: "Query time (ms)", group: "vram", field: "queryUs", precision: 3 }],
      note: "DXGI's figures for the game's process: what it uses of the budget Windows gives it. The card is the first adapter, which drives the primary display. Its own size is capped when DXGI reports it to a 32-bit process (3 GiB for a 16 GiB card); use and budget are exact." },
    { title: "Textures in use", wide: true, capacity: ctx => <U9glTextureList ctx={ctx} />, metrics: [] },
    { title: "Tracked GL allocations", metrics: [
      { label: "GL textures alive", group: "resources", field: "texturesAlive" },
      { label: "GL texture allocation", group: "resources", field: "textureBytes", mib: true },
      { label: "Textures created", group: "resources", field: "texturesCreated" },
      { label: "Textures destroyed", group: "resources", field: "texturesDestroyed" }],
      note: "Our own accounting of what we allocated; not whole-GPU VRAM." },
  ],
  world: [
    { title: "Location", metrics: [m.mapName, { label: "Map number", group: "world", field: "map", when: ["mapValid", "No world loaded."] }, ...avatar] },
    { title: "Fast area", wide: true, capacity: ctx => <U9glHeatmap ctx={ctx} />, metrics: [],
      note: "The cells the engine keeps live around the camera, sampled with the area walk (every 5 s, or every 30 frames with the Ctrl+F12 objects page open)." },
    { title: "Objects", capacity: worstCellCapacity, metrics: [a("Objects", "objects"), a("Drawn", "drawn"), a("Moving", "moving"),
      a("Fixed", "fixed"), a("Non-fixed", "nonfixed"), a("Unique types", "uniqueTypes"), a("Unresolved", "unresolved"),
      a("Fullest cell (slots)", "worstCell")], note: "The objects the engine keeps live in the fast area near the camera." },
    { title: "Commonest object types", capacity: topTypes, metrics: [] },
    { title: "Streaming", capacity: terrainCapacity, metrics: [a("Cells live", "cellsActive"), a("Cells drawn", "cellsDrawn"),
      a("Cells colliding", "cellsCollide"), a("Cells queued", "cellsQueued"), a("LRU cells", "lruCells"),
      a("Cells with slots", "cellsWithSlots"), a("Slot memory", "slotBytes", { mib: true }),
      a("Queue: sorting", "queueSorting"), a("Queue: waiting for meshes", "queueWaiting"), a("Queue: ready", "queueReady"),
      a("Pending meshes", "pendingMeshes"), a("Pending instances", "pendingInstances"),
      a("Terrain ring: near", "ringNear"), a("Terrain ring: middle", "ringMid"), a("Terrain ring: far", "ringFar"),
      a("Terrain A", "terrainA"), a("Terrain B", "terrainB"), a("Grid width (cells)", "gridW"), a("Grid height (cells)", "gridH")],
      note: "Cells are 1024 x 1024 units. Loading runs sorting, then waiting for meshes, then ready." },
    { title: "Mesh cache", capacity: meshPoolCapacity, metrics: [a("Mesh types", "meshTypes"), a("In use", "meshInUse"),
      a("Idle", "meshIdle"), a("Handles", "meshUsers"),
      a("Handles per mesh", "meshUsers", { precision: 1, derive: v => BigInt(v.meshInUse ?? "0") > 0n
        ? (Number(v.meshUsers) / Number(v.meshInUse)).toFixed(1) : "0" }),
      a("Most-used mesh: handles", "meshMaxUsers"), a("Most-used mesh: appearance", "meshMaxAppearance"),
      a("Pool used", "meshBytesPool", { mib: true }), a("Pool size", "poolSize", { mib: true }),
      a("Outside the pool", "meshBytesOutside", { mib: true }), a("Pool's largest free block", "poolLargestFree", { mib: true }),
      a("Engine's mesh count", "engineMeshTypes"), a("Engine's mesh memory", "engineMeshBytes", { mib: true }),
      a("Idle meshes kept", "idleKeep")],
      note: "One mesh per appearance, shared by every object using it. Handles are references held, not objects." },
    { title: "Meshes with the most handles", wide: true, capacity: ctx => <U9glMeshList ctx={ctx} />, metrics: [] },
    { title: "Item pools", capacity: itemPoolCapacity, metrics: [a("Items used", "nonfixedUsed", { mib: true }),
      a("Items pool", "nonfixedMax", { mib: true }), a("Fixed items used", "fixedUsed", { mib: true }),
      a("Fixed items pool", "fixedMax", { mib: true })], note: "The current map's object records." },
    { title: "Unverified", metrics: [a("Engine camera counter", "inCamera"), a("Sample cost (ms)", "sampleUs", { precision: 2 })],
      note: "The camera counter is the engine's own number and is not understood: it is not an object count." },
  ],
};

export const metricLabels: ReadonlyMap<string, string> = new Map(Object.values(sectionPages).flat()
  .flatMap(section => section.metrics).filter(metric => metric.group && metric.field && !metric.derive && !metric.text)
  .map(metric => [`${metric.group}.${metric.field}`, metric.label] as const).reverse());

const pct = (p: number | null, state: (p: number) => LcarsSystemState): BandCode =>
  p === null ? null : { text: `${Math.round(p)}%`, state: state(p) };
const mib = (bytes: string | null, state?: LcarsSystemState): BandCode =>
  bytes === null ? null : { text: `${formatMebibytes(bytes)} MiB`, ...(state ? { state } : {}) };
const fpsCode = (ctx: MetricContext): BandCode => {
  const fps = metricNumber(m.fps, ctx);
  return fps === null ? null : { text: `${fps.toFixed(0)} fps`, state: levelBelow(fps, thresholds.fpsWarning, thresholds.fpsCritical) };
};
const privateCode = (ctx: MetricContext): BandCode => {
  const used = value(ctx, "memory", "privateBytes"), total = ctx.view?.game?.addressSpaceBytes ?? null;
  const p = percentOf(used, total);
  return mib(used, p === null ? undefined : privateState(p));
};
const floorCode = (ctx: MetricContext): BandCode => {
  const r = metricReading(m.verdict, ctx);
  if (r.state === "offline") return null;
  const g = ctx.sample?.groups.memoryTrend?.values ?? {};
  const word = m.verdict.text(g, String(r.value));
  return { text: word, state: word === "rising" ? "warning" : "nominal" };
};
const largestCode = (ctx: MetricContext): BandCode => {
  const v = value(ctx, "addressSpace", "largestFreeBytes");
  return v === null ? null : mib(v, levelBelow(Number(BigInt(v) >> 20n), thresholds.largestBlockWarningMiB, thresholds.largestBlockCriticalMiB));
};
const cacheCode = (ctx: MetricContext): BandCode => pct(percentOf(value(ctx, "textureCache", "cacheBytes"), value(ctx, "textureCache", "budgetBytes")), () => "nominal");
const vramCode = (ctx: MetricContext): BandCode => value(ctx, "vram", "source") === "0" ? null
  : pct(percentOf(value(ctx, "vram", "localUsageBytes"), value(ctx, "vram", "localBudgetBytes")), vramState);
const objectsCode = (ctx: MetricContext): BandCode => {
  const n = value(ctx, "area", "objects");
  if (n === null) return null;
  const p = percentOf(value(ctx, "area", "worstCell"), value(ctx, "area", "worstCellCapacity"));
  return { text: BigInt(n).toLocaleString("en-US"), state: p === null ? "nominal" : cellState(p) };
};
const poolsCode = (ctx: MetricContext): BandCode => {
  const p = [percentOf(value(ctx, "area", "nonfixedUsed"), value(ctx, "area", "nonfixedMax")),
    percentOf(value(ctx, "area", "fixedUsed"), value(ctx, "area", "fixedMax"))].filter((x): x is number => x !== null);
  return p.length ? pct(Math.max(...p), poolState) : null;
};
const sectionCodes: Record<string, (ctx: MetricContext) => BandCode> = {
  "Frame pacing": fpsCode,
  "Per frame": ctx => { const d = value(ctx, "frames", "drawCallsLast"); return d === null ? null : { text: `${BigInt(d).toLocaleString("en-US")} draws` }; },
  "Process": privateCode, "Process memory": privateCode,
  "Leak indicator": floorCode, "Memory floor": floorCode,
  "Address space": largestCode,
  "Texture cache": cacheCode,
  "Video memory": vramCode,
  "Location": ctx => { const r = metricReading(m.mapName, ctx); if (r.state === "offline") return null;
    const v = ctx.sample?.groups.world?.values ?? {}; return { text: m.mapName.text(v, String(r.value), ctx.sample?.labels ?? {}) }; },
  "Objects": objectsCode, "Nearby objects": objectsCode,
  "Item pools": poolsCode,
  "Mesh cache": ctx => { const n = value(ctx, "area", "meshUsers"); return n === null ? null : { text: `${BigInt(n).toLocaleString("en-US")} handles` }; },
  "Game log": ctx => { const log = ctx.view?.log; return !log ? null
    : { text: log.faults ? `${log.faults} faults` : `${log.warnings} warnings`, state: log.faults ? "critical" : "nominal" }; },
};
const codeFor = (title: string, ctx: MetricContext) => sectionCodes[title]?.(ctx) ?? null;

export function U9glSectionPage({ page, ctx }: { page: SectionPage; ctx: MetricContext }) {
  return (
    <div className="u9gl-overlay-grid u9gl-readouts">
      {sectionPages[page].map((section, index) => (
        <LcarsPanel key={section.title} band={band(page, index, codeFor(section.title, ctx))} id={`${page}-group-${index}`} title={section.title} headingLevel="h3"
          className={section.wide ? "u9gl-wide" : ""}>
          {section.capacity?.(ctx)}
          {section.metrics.map(metric => <MetricReadout key={metric.label} metric={metric} ctx={ctx} />)}
          {section.note && <p className="lcars-supporting u9gl-section-note">{section.note}</p>}
        </LcarsPanel>
      ))}
    </div>
  );
}

type Tile = { title: string; href: string; state: (ctx: MetricContext) => LcarsSystemState; detail?: (ctx: MetricContext) => string;
  capacity?: (ctx: MetricContext) => ReactNode; metrics: readonly Metric[] };

const tiles: readonly Tile[] = [
  { title: "Frame pacing", href: "#rendering", metrics: [m.fps, m.failed],
    state: ctx => worse(levelBelow(metricNumber(m.fps, ctx), thresholds.fpsWarning, thresholds.fpsCritical),
      levelAbove(metricNumber(m.failed, ctx), 1, 1)) },
  { title: "Process memory", href: "#memory", capacity: privateCapacity, metrics: [m.privateBytes],
    state: ctx => {
      const used = value(ctx, "memory", "privateBytes"), total = ctx.view?.game?.addressSpaceBytes;
      return used && total ? privateState(Number(BigInt(used) * 1000n / BigInt(total)) / 10) : "offline";
    } },
  { title: "Memory floor", href: "#memory", metrics: [m.floor, m.verdict],
    state: ctx => { const v = value(ctx, "memoryTrend", "floorRising"); return v === null ? "offline" : v === "0" ? "nominal" : "warning"; } },
  { title: "Texture cache", href: "#textures", capacity: textureCapacity, metrics: [m.evictionsPerSec, m.uploadMax],
    state: ctx => worse(levelAbove(metricNumber(m.evictionsPerSec, ctx), thresholds.evictionsPerSecWarning),
      levelAbove(metricNumber(m.uploadMax, ctx), thresholds.uploadMsWarning)) },
  { title: "Address space", href: "#memory",
    metrics: [{ label: "Largest free block", group: "addressSpace", field: "largestFreeBytes", mib: true },
      { label: "Free address space", group: "addressSpace", field: "freeBytes", mib: true }],
    state: ctx => { const v = value(ctx, "addressSpace", "largestFreeBytes");
      return levelBelow(v === null ? null : Number(BigInt(v) >> 20n), thresholds.largestBlockWarningMiB, thresholds.largestBlockCriticalMiB); } },
  { title: "Nearby objects", href: "#world", capacity: worstCellCapacity, metrics: [a("Objects", "objects")],
    state: ctx => { const p = percentOf(value(ctx, "area", "worstCell"), value(ctx, "area", "worstCellCapacity")); return p === null ? "offline" : cellState(p); } },
  { title: "Mesh cache", href: "#world", metrics: [a("Handles", "meshUsers"),
      a("Handles per mesh", "meshUsers", { precision: 1, derive: v => BigInt(v.meshInUse ?? "0") > 0n ? (Number(v.meshUsers) / Number(v.meshInUse)).toFixed(1) : "0" })],
    state: ctx => value(ctx, "area", "meshUsers") === null ? "offline" : "nominal", detail: () => "watch the trend in History" },
  { title: "Game log", href: "#events", metrics: [],
    capacity: ctx => { const log = ctx.view?.log;
      return <p className="lcars-copy">{log ? `${log.faults} faults · ${log.warnings} warnings this run` : "Not followed by this launcher."}</p>; },
    state: ctx => { const log = ctx.view?.log; return !log || !log.lastSeq ? "offline" : log.faults > 0 ? "critical" : "nominal"; },
    detail: ctx => ctx.view?.log?.faults ? "see Events" : "render log and crash report" },
  { title: "Item pools", href: "#world", capacity: itemPoolCapacity, metrics: [],
    state: ctx => { const p = [percentOf(value(ctx, "area", "nonfixedUsed"), value(ctx, "area", "nonfixedMax")),
      percentOf(value(ctx, "area", "fixedUsed"), value(ctx, "area", "fixedMax"))].filter((x): x is number => x !== null);
      return p.length ? poolState(Math.max(...p)) : "offline"; } },
  { title: "Video memory", href: "#textures", capacity: vramCapacity,
    metrics: [{ label: "Game's use", group: "vram", field: "localUsageBytes", mib: true }],
    state: ctx => { const p = percentOf(value(ctx, "vram", "localUsageBytes"), value(ctx, "vram", "localBudgetBytes")); return p === null ? "offline" : vramState(p); } },
];

const tileCode = (tile: Tile, ctx: MetricContext): BandCode => {
  const code = codeFor(tile.title, ctx);
  if (!code) return null;
  const state = tile.state(ctx);
  return { text: code.text, state: state === "offline" ? "nominal" : state };
};

export function U9glOverview({ ctx }: { ctx: MetricContext }) {
  const game = ctx.view?.game;
  return (
    <>
      <div className="u9gl-overview-grid u9gl-readouts">
        {tiles.map((tile, index) => (
          <LcarsPanel key={tile.title} band={band("overview", index, tileCode(tile, ctx))} id={`overview-tile-${index}`} title={tile.title} headingLevel="h3" className="u9gl-tile">
            <LcarsStatus label="Status" state={tile.state(ctx)} {...(tile.detail ? { detail: tile.detail(ctx) } : {})} />
            {tile.capacity?.(ctx)}
            {tile.metrics.map(metric => <MetricReadout key={metric.label} metric={metric} ctx={ctx} />)}
            <a className="u9gl-tile-link" href={tile.href}>Details</a>
          </LcarsPanel>
        ))}
      </div>
      <LcarsPanel band={band("overview", tiles.length, codeFor("Location", ctx))} id="overview-location" title="Location" headingLevel="h3" className="u9gl-location u9gl-readouts">
        <div className="u9gl-location-strip">
          {[m.mapName, ...avatar].map(metric => <MetricReadout key={metric.label} metric={metric} ctx={ctx} />)}
        </div>
        <p className="lcars-supporting">{game ? `${game.executable} · ${game.largeAddressAware ? "large address aware, " : ""}`
          + `${Number(BigInt(game.addressSpaceBytes) >> 20n).toLocaleString("en-US")} MiB address space` : "No game selected yet."}</p>
      </LcarsPanel>
    </>
  );
}
