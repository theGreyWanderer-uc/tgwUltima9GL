export type DashboardArrayLayout = { name: string; countField: string; capacity: number; parts: { name: string; bits: number }[] };

export function unpackWords(words: readonly string[], parts: DashboardArrayLayout["parts"]): Record<string, number>[] {
  return words.map(word => {
    let v = BigInt(word);
    const out: Record<string, number> = {};
    for (const part of parts) {
      const mask = (1n << BigInt(part.bits)) - 1n;
      out[part.name] = Number(v & mask);
      v >>= BigInt(part.bits);
    }
    return out;
  });
}

export type HeatCell = { x: number; y: number; objects: number; drawn: number; moving: number; flags: number };
export type HeatWindow = { x0: number; y0: number; size: number };

export function fitWindow(cells: readonly HeatCell[], avatar: { x: number; y: number } | null, grid: { w: number; h: number }, min = 8): HeatWindow {
  const xs = cells.map(c => c.x).concat(avatar ? [avatar.x] : []), ys = cells.map(c => c.y).concat(avatar ? [avatar.y] : []);
  if (!xs.length) return { x0: Math.max(0, Math.floor(grid.w / 2 - min / 2)), y0: Math.max(0, Math.floor(grid.h / 2 - min / 2)), size: Math.min(min, grid.w, grid.h) };
  const minX = Math.min(...xs) - 1, maxX = Math.max(...xs) + 1, minY = Math.min(...ys) - 1, maxY = Math.max(...ys) + 1;
  const size = Math.min(Math.max(min, maxX - minX + 1, maxY - minY + 1), grid.w, grid.h);
  const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
  const clamp = (v: number, hi: number) => Math.max(0, Math.min(hi - size, Math.round(v - (size - 1) / 2)));
  return { x0: clamp(cx, grid.w), y0: clamp(cy, grid.h), size };
}

export function heatLevel(cell: HeatCell | undefined, mode: "objects" | "slots", busiest: number, capacity: number, warn: number, critical: number): number {
  if (!cell || cell.objects === 0) return 0;
  if (mode === "objects") return Math.max(1, Math.ceil((cell.objects / Math.max(1, busiest)) * 5));
  const pct = (cell.objects / Math.max(1, capacity)) * 100;
  return pct >= critical ? 5 : pct >= warn ? 4 : pct >= warn / 2 ? 3 : pct >= 10 ? 2 : 1;
}

export const cellFlagNames = (flags: number) =>
  [flags & 1 ? "drawn" : "", flags & 2 ? "active" : "", flags & 4 ? "colliding" : "", flags & 8 ? "queued" : ""].filter(Boolean).join(", ") || "no flags";
