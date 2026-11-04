import { useState } from "react";
import { LcarsDataTable, type LcarsDataColumn } from "./components/ui/lcars/lcars-data-table";
import type { MetricContext } from "./u9gl-metrics";
import { cellFlagNames, fitWindow, heatLevel, unpackWords, type HeatCell } from "./u9gl-heatmap-data";
import { thresholds } from "./u9gl-thresholds";

const columns: readonly LcarsDataColumn<HeatCell & { id: string; fill: number }>[] = [
  { id: "cell", header: "Cell", value: r => `${r.x},${r.y}` },
  { id: "objects", header: "Objects", value: r => r.objects, numeric: true, precision: 0 },
  { id: "fill", header: "Slot fill %", value: r => r.fill, numeric: true, precision: 1 },
  { id: "drawn", header: "Drawn", value: r => r.drawn, numeric: true, precision: 0 },
  { id: "moving", header: "Moving", value: r => r.moving, numeric: true, precision: 0 },
  { id: "flags", header: "State", value: r => cellFlagNames(r.flags) },
];

export function U9glHeatmap({ ctx }: { ctx: MetricContext }) {
  const [mode, setMode] = useState<"objects" | "slots">("slots");
  const [focus, setFocus] = useState<HeatCell | null>(null);
  const [selected, setSelected] = useState<readonly string[]>([]);
  const g = ctx.sample?.groups.areaCells, layout = ctx.view?.arrays?.areaCells;
  if (!g || !layout || !g.array) return <p className="lcars-supporting">The fast area has not been sampled yet.</p>;
  const cells = unpackWords(g.array, layout.parts) as HeatCell[];
  const grid = { w: Number(g.values.gridW) || 128, h: Number(g.values.gridH) || 128 };
  const capacity = Number(g.values.slotCapacity) || 2045;
  const avatar = g.values.avatarValid === "1" ? { x: Number(g.values.avatarX), y: Number(g.values.avatarY) } : null;
  const win = fitWindow(cells, avatar, grid);
  const at = new Map(cells.map(c => [`${c.x},${c.y}`, c]));
  const busiest = Math.max(0, ...cells.map(c => c.objects));
  const level = (c: HeatCell | undefined) => heatLevel(c, mode, busiest, capacity, thresholds.worstCellWarning, thresholds.worstCellCritical);
  const describe = (x: number, y: number, c: HeatCell | undefined) => c
    ? `cell ${x},${y} · ${c.objects} objects (${(c.objects / capacity * 100).toFixed(1)} % of slots) · ${c.drawn} drawn · ${c.moving} moving · ${cellFlagNames(c.flags)}`
    : `cell ${x},${y} · no objects`;
  const S = 10, rows: React.ReactNode[] = [];
  for (let row = 0; row < win.size; row++) {
    const y = win.y0 + win.size - 1 - row;
    for (let x = win.x0; x < win.x0 + win.size; x++) {
      const c = at.get(`${x},${y}`), px = (x - win.x0) * S, py = row * S;
      rows.push(
        <g key={`${x},${y}`} className="u9gl-heat-cell" data-level={level(c)} data-drawn={c && c.flags & 1 ? "1" : "0"}
          data-colliding={c && c.flags & 4 ? "1" : "0"} tabIndex={c ? 0 : -1} aria-label={describe(x, y, c)}
          onMouseEnter={() => setFocus(c ?? { x, y, objects: 0, drawn: 0, moving: 0, flags: 0 })} onFocus={() => setFocus(c ?? null)}>
          <title>{describe(x, y, c)}</title>
          <rect x={px + 0.6} y={py + 0.6} width={S - 1.2} height={S - 1.2} rx={1.6} />
          {c && c.flags & 8 ? <path className="u9gl-heat-queued" d={`M${px + S - 3.6} ${py + 0.6}H${px + S - 0.6}V${py + 3.6}Z`} /> : null}
          {avatar && avatar.x === x && avatar.y === y ? <circle className="u9gl-heat-avatar" cx={px + S / 2} cy={py + S / 2} r={1.8} /> : null}
        </g>);
    }
  }
  const tableRows = [...cells].sort((a, b) => b.objects - a.objects).slice(0, 12)
    .map(c => ({ ...c, id: `${c.x},${c.y}`, fill: c.objects / capacity * 100 }));
  return (
    <div className="u9gl-heatmap" data-quality={g.quality}>
      <div className="u9gl-heatmap-controls">
        <label className="u9gl-session-picker">Colour by<select value={mode} onChange={e => setMode(e.target.value === "objects" ? "objects" : "slots")}>
          <option value="slots">Slot fill (against {capacity} slots)</option><option value="objects">Objects (against the busiest cell shown)</option>
        </select></label>
        <p className="lcars-supporting">Cells {win.x0}-{win.x0 + win.size - 1} across, {win.y0}-{win.y0 + win.size - 1} up, of {grid.w} x {grid.h};
          1024 units each. {cells.length} cells hold objects{g.values.cellsOutside !== "0" ? `; ${g.values.cellsOutside} could not be placed` : ""}.</p>
      </div>
      <svg className="u9gl-heat-grid" viewBox={`0 0 ${win.size * S} ${win.size * S}`} role="group"
        aria-label={`Fast area heat map, ${win.size} by ${win.size} cells around the camera`}>
        {rows}
      </svg>
      <p className="lcars-copy u9gl-heat-readout" aria-live="polite">{focus ? describe(focus.x, focus.y, focus.objects ? focus : undefined) : "Point at or focus a cell."}</p>
      <ul className="u9gl-heat-legend" aria-label="Legend">
        {(mode === "slots"
          ? ["empty", "under 10 %", `under ${thresholds.worstCellWarning / 2} %`, `under ${thresholds.worstCellWarning} %`,
             `${thresholds.worstCellWarning} % or more`, `${thresholds.worstCellCritical} % or more`]
          : ["empty", "up to 20 % of the busiest", "40 %", "60 %", "80 %", `the busiest (${busiest})`]).map((label, i) =>
          <li key={label}><span className="u9gl-heat-swatch" data-level={i} />{label}</li>)}
        <li><span className="u9gl-heat-swatch u9gl-heat-swatch-drawn" />drawn</li>
        <li><span className="u9gl-heat-swatch u9gl-heat-swatch-colliding" />colliding</li>
        <li><span className="u9gl-heat-swatch u9gl-heat-swatch-avatar" />the Avatar</li>
      </ul>
      <LcarsDataTable label="Busiest cells" columns={columns} data={{ state: "ready", rows: tableRows }}
        getRowId={r => r.id} getRowLabel={r => `cell ${r.id}`} pageSize={12} selection={{ selectedIds: selected, onChange: setSelected }} />
    </div>
  );
}
