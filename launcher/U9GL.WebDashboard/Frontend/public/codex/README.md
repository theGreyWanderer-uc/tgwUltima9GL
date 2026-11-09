# Codex symbol layers

Our own artwork, rendered in Blender from `codex_symbol.blend` in the tgwBlender `codex`
workspace (scene "Codex | Muted matte", glow pieces from "Codex Glow | 03 High"). Copied here
on 2026-11-08 so the dashboard depends only on files in this repository.

- `base-muted.png`: the muted symbol, every piece unlit; Humility is the dark outer ring.
- `<id>-core.png` and `<id>-glow.png` for `honesty`, `compassion`, `valor`, `justice`, `sacrifice`,
  `honor`, `spirituality`, `truth`, `love`, `courage`.

All 1024 x 1024 straight-alpha RGBA with one centre (512, 512). Draw the base, then each lit
core with normal compositing, then each lit glow additively. `src/u9gl-codex.tsx` does this on
the player page, lighting one virtue per cleansed shrine.
