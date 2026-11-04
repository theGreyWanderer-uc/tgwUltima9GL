import { useState } from "react";
import { LcarsDataTable, type LcarsDataColumn } from "./components/ui/lcars/lcars-data-table";
import { LcarsBarGraph } from "./components/ui/lcars/lcars-graphs";
import type { MetricContext } from "./u9gl-metrics";
import { unpackWords } from "./u9gl-heatmap-data";

function rowsOf(ctx: MetricContext, group: string): Record<string, number>[] | null {
  const g = ctx.sample?.groups[group], layout = ctx.view?.arrays?.[group];
  return g?.array && layout ? unpackWords(g.array, layout.parts) : null;
}
const mib = (bytes: number) => bytes / 1048576;

type TextureRow = { id: string; texture: string; size: string; mib: number; age: number; replaced: string; keyed: string };
const textureColumns: readonly LcarsDataColumn<TextureRow>[] = [
  { id: "texture", header: "Texture · frame", value: r => r.texture },
  { id: "size", header: "Size", value: r => r.size },
  { id: "mib", header: "MiB", value: r => r.mib, numeric: true, precision: 2 },
  { id: "age", header: "Frames since used", value: r => r.age, numeric: true, precision: 0 },
  { id: "replaced", header: "Replacement", value: r => r.replaced },
  { id: "keyed", header: "Chromakey", value: r => r.keyed },
];

export function U9glTextureList({ ctx }: { ctx: MetricContext }) {
  const [selected, setSelected] = useState<readonly string[]>([]);
  const items = rowsOf(ctx, "textureList"), g = ctx.sample?.groups.textureList;
  if (!items || !g) return <p className="lcars-supporting">The texture list has not been sampled yet.</p>;
  const rows: TextureRow[] = items.map((t, rank) => ({
    id: `${rank}:${t.id}/${t.frame}`, texture: `${t.id} · ${t.frame}`, size: `${t.width ?? 0} × ${t.height ?? 0}${t.width === 4095 || t.height === 4095 ? "+" : ""}`,
    mib: mib((t.width ?? 0) * (t.height ?? 0) * 4), age: t.age ?? 0, replaced: t.overridden ? "yes" : "", keyed: t.keyed ? "yes" : "",
  }));
  const total = rows.reduce((s, r) => s + r.mib, 0);
  return <>
    <p className="lcars-supporting">The {rows.length} most recently used of {g.values.cacheEntries} cached textures,
      {" "}{total.toFixed(1)} MiB between them as the cache counts them (width × height × 4). Ages are in the cache's frames, capped at 4,095.</p>
    <LcarsDataTable label="Textures in use" columns={textureColumns} data={{ state: "ready", rows }}
      getRowId={r => r.id} getRowLabel={r => `texture ${r.texture}`} pageSize={15} selection={{ selectedIds: selected, onChange: setSelected }} />
  </>;
}

type MeshRow = { id: string; appearance: number; handles: number; kib: number; pool: string };
const meshColumns: readonly LcarsDataColumn<MeshRow>[] = [
  { id: "appearance", header: "Appearance", value: r => r.appearance, numeric: true, precision: 0 },
  { id: "handles", header: "Handles", value: r => r.handles, numeric: true, precision: 0 },
  { id: "kib", header: "KiB", value: r => r.kib, numeric: true, precision: 1 },
  { id: "pool", header: "Memory", value: r => r.pool },
];

export function U9glMeshList({ ctx }: { ctx: MetricContext }) {
  const [selected, setSelected] = useState<readonly string[]>([]);
  const items = rowsOf(ctx, "meshList"), g = ctx.sample?.groups.meshList;
  if (!items || !g) return <p className="lcars-supporting">The mesh list has not been sampled yet.</p>;
  const rows: MeshRow[] = items.map((m, rank) => ({ id: `${rank}:${m.appearance}`, appearance: m.appearance ?? 0, handles: m.handles ?? 0,
    kib: (m.bytes ?? 0) / 1024, pool: m.inPool ? "mesh pool" : "outside the pool" }));
  const top = rows.slice(0, 10).map(r => ({ label: `Appearance ${r.appearance}`, value: r.handles }));
  return <>
    <LcarsBarGraph label="Most handles" series={{ state: g.quality === "stale" ? "stale" : "ready", samples: top, unit: "handles" }}
      max={Math.max(1, ...top.map(t => t.value))} />
    <p className="lcars-supporting">The {rows.length} appearances with the most handles of {g.values.meshTypes} with a mesh.
      A handle is a reference held, not an object; a count that only climbs is the I-14 leak's shape.</p>
    <LcarsDataTable label="Meshes with the most handles" columns={meshColumns} data={{ state: "ready", rows }}
      getRowId={r => r.id} getRowLabel={r => `appearance ${r.appearance}`} pageSize={15} selection={{ selectedIds: selected, onChange: setSelected }} />
  </>;
}
