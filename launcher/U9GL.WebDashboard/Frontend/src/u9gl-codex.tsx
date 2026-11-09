import { useEffect, useRef, useState } from "react";

const SIZE = 1024;
const pieces = [
  { id: "honesty", bit: 0 }, { id: "compassion", bit: 1 }, { id: "valor", bit: 2 }, { id: "justice", bit: 3 },
  { id: "sacrifice", bit: 4 }, { id: "honor", bit: 5 }, { id: "spirituality", bit: 6 },
  { id: "truth", needs: [0, 3, 5, 6] }, { id: "love", needs: [1, 3, 4, 6] }, { id: "courage", needs: [2, 4, 5, 6] },
] as const;

type Layers = { base: HTMLImageElement; core: HTMLImageElement[]; glow: HTMLImageElement[] };
let layers: Promise<Layers> | null = null;

function loadImage(file: string): Promise<HTMLImageElement> {
  const image = new Image();
  image.decoding = "async";
  image.src = `${import.meta.env.BASE_URL}codex/${file}`;
  return image.decode().then(() => image);
}

function loadLayers(): Promise<Layers> {
  layers ??= Promise.all([
    loadImage("base-muted.png"),
    Promise.all(pieces.map(p => loadImage(`${p.id}-core.png`))),
    Promise.all(pieces.map(p => loadImage(`${p.id}-glow.png`))),
  ]).then(([base, core, glow]) => ({ base, core, glow }));
  layers.catch(() => { layers = null; });
  return layers;
}

export function codexLit(shrineBits: number): boolean[] {
  const has = (bit: number) => ((shrineBits >> bit) & 1) === 1;
  return pieces.map(p => "bit" in p ? has(p.bit) : p.needs.every(has));
}

export function Codex({ shrineBits }: { shrineBits: number }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [failed, setFailed] = useState(false);

  useEffect(() => {
    let live = true;
    loadLayers().then(({ base, core, glow }) => {
      const c = canvas.current?.getContext("2d");
      if (!live || !c) return;
      const lit = codexLit(shrineBits);
      c.clearRect(0, 0, SIZE, SIZE);
      c.globalCompositeOperation = "source-over";
      c.drawImage(base, 0, 0);
      lit.forEach((on, i) => { if (on) c.drawImage(core[i]!, 0, 0); });
      c.globalCompositeOperation = "lighter";
      lit.forEach((on, i) => { if (on) c.drawImage(glow[i]!, 0, 0); });
      c.globalCompositeOperation = "source-over";
    }, () => { if (live) setFailed(true); });
    return () => { live = false; };
  }, [shrineBits]);

  const names = ["Honesty", "Compassion", "Valor", "Justice", "Sacrifice", "Honor", "Spirituality", "Humility"];
  const cleansed = names.filter((_, i) => (shrineBits >> i) & 1);
  const label = cleansed.length
    ? `The Codex symbol, lit for the shrines cleansed: ${cleansed.join(", ")}.`
    : "The Codex symbol, unlit: no shrine cleansed yet.";

  return failed
    ? <p className="lcars-supporting">The Codex image could not be loaded.</p>
    : <canvas ref={canvas} className="u9gl-codex" width={SIZE} height={SIZE} role="img" aria-label={label} />;
}
