import { useEffect, useState } from "react";

const names = new Map<number, string | null>();
const pending = new Set<number>();
const listeners = new Set<() => void>();

function request(ids: number[]) {
  const wanted = ids.filter(id => id > 0 && !names.has(id) && !pending.has(id));
  if (wanted.length === 0) return;
  wanted.forEach(id => pending.add(id));
  fetch(`/api/v1/typenames?ids=${wanted.join(",")}`, { cache: "force-cache" })
    .then(r => (r.ok ? r.json() : {}))
    .then((got: Record<string, string>) => { wanted.forEach(id => names.set(id, got[String(id)] ?? null)); })
    .catch(() => { wanted.forEach(id => names.set(id, null)); })
    .finally(() => { wanted.forEach(id => pending.delete(id)); listeners.forEach(fn => fn()); });
}

function useTypeNames(ids: number[]): (id: number) => string {
  const [, setTick] = useState(0);
  useEffect(() => {
    const fn = () => setTick(t => t + 1);
    listeners.add(fn);
    return () => { listeners.delete(fn); };
  }, []);
  useEffect(() => { request(ids); }, [ids.join(",")]);
  return id => names.get(id) ?? (names.has(id) ? `unnamed item (type ${id})` : `type ${id}`);
}

export const slotFields = ["equipLeftHand", "equipRightHand", "equipScabbard", "equipLeftShoulder",
  "equipRightShoulder", "equipQuiver", "equipLeftForearm"] as const;

export function Equipment({ values }: { values: Record<string, string> }) {
  const ids = slotFields.map(f => Number(values[f] ?? "0") || 0);
  const name = useTypeNames(ids);
  const at = (i: number) => ids[i] ?? 0;
  const left = at(0), right = at(1), scabbard = at(2), lShoulder = at(3), rShoulder = at(4), quiver = at(5), forearm = at(6);
  const rows: [string, number][] = [];
  const handsClass = Number(values.equipHandsClass ?? "0");
  if ((handsClass === 2 || handsClass === 3) && (left === 0) !== (right === 0)) {
    rows.push(["Both hands", left || right]);
  } else {
    rows.push(["Left hand", left], ["Right hand", right]);
  }
  rows.push(["Shield (left forearm)", forearm], ["Quiver", quiver], ["Scabbard", scabbard],
    ["Left shoulder", lShoulder], ["Right shoulder", rShoulder]);

  return (
    <ul className="u9gl-player-shrines" aria-label="Equipment">
      {rows.map(([label, id]) => (
        <li key={label} data-state={id ? "done" : "open"}>
          <strong>{label}</strong><span>{id ? name(id) : "empty"}</span>
        </li>
      ))}
    </ul>
  );
}
