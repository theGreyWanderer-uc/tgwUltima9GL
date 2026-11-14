import { LcarsPanel } from "./components/ui/lcars/lcars-geometry";
import type { LcarsSystemState } from "./components/ui/lcars/lcars-telemetry";
import { MetricReadout, type Metric, type MetricContext } from "./u9gl-metrics";
import { Codex } from "./u9gl-codex";
import { Equipment, slotFields } from "./u9gl-equipment";
import { band, type BandCode } from "./u9gl-band";

const recordGate: [string, string] = ["recordValid", "The Avatar's record was not readable."];
const datumGate: [string, string] = ["datumsValid", "The shared datums were not readable."];

const levelWords = {
  strength: ["Weak", "Strong", "Powerful", "Heroic"],
  dexterity: ["Clumsy", "Nimble", "Proficient", "Adept"],
  intelligence: ["Dim", "Smart", "Brilliant", "Genius"],
  skill: ["Novice", "Apprentice", "Journeyman", "Master"],
} as const;
const levelWord = (words: readonly string[], raw: string) => words[Math.max(0, Math.min(3, Number(raw)))]!;

const classes = [
  ["Mage", "Honesty"], ["Bard", "Compassion"], ["Fighter", "Valor"], ["Druid", "Justice"],
  ["Tinker", "Sacrifice"], ["Paladin", "Honor"], ["Ranger", "Spirituality"], ["Shepherd", "Humility"],
] as const;
const virtues = ["Honesty", "Compassion", "Valor", "Justice", "Sacrifice", "Honor", "Spirituality", "Humility"] as const;
const circles = ["Linear", "First Circle", "Second Circle", "Third Circle", "Fourth Circle", "Fifth Circle",
  "Sixth Circle", "Seventh Circle", "Eighth Circle"] as const;
const bitCount = (n: number) => { let c = 0; for (let v = n; v; v &= v - 1) c++; return c; };

const skills = [
  { field: "skillFists", name: "Fists", moves: [["Kick", "Phineus, Buccaneer's Den"], ["Head Butt", "Phineus"], ["Back-Flip", "Phineus"]] },
  { field: "skillOneHanded", name: "1-handed", moves: [["Hammer Chop", "Keller, Castle Britannia"], ["Slashing", "Old Jak, Valoria"], ["Shield Bash", "Lyssa, Serpent's Hold"]] },
  { field: "skillTwoHanded", name: "2-handed", moves: [["Spin-Shot", "Duncan, Janus Island"], ["Low-Fake-Shot", "Duncan"], ["Throw and Return", "Sir William Lett's book, his tomb west of Yew"]] },
  { field: "skillStaff", name: "Staffs", moves: [["Vault", "Peg Leg Joe, Paws"], ["Sweep", "Raphael, Trinsic"], ["Defensive Spin", "Bakura, west of Yew"]] },
  { field: "skillRanged", name: "Range", moves: [["Triple Arrow", "Leon, Britain"], ["Fire Arrow", "Raliegh, Yew"], ["Cloud of Death", "Iolo, east of Britain"]] },
] as const;
const moveKeys = ["Alt + click", "Ctrl + click", "Ctrl + Alt + click"] as const;

const effects: readonly [number, string][] = [
  [0x2, "Poisoned"], [0x200, "Invisible"], [0x40, "Asleep"], [0x400, "Paralysed"], [0x100000, "Locked in place"],
  [0x1000, "Beguiled"],
];

const spellNames: Record<number, string> = {
  1: "Stone", 2: "Gust", 3: "Ignite", 4: "Douse",
  5: "Create Reagents", 6: "Telekinesis", 7: "Light", 8: "Light Heal",
  9: "Crystal Barrier", 10: "Lightning Bolt", 11: "Infernal Armor", 12: "Cure",
  13: "Meteorite", 14: "Wizard Eye", 15: "Fireball", 16: "Fog",
  17: "Charm", 18: "Ethereal Sight", 19: "Day", 20: "Freeze",
  21: "Summon Undead", 22: "Levitate", 23: "Bolt of Flame", 24: "Full Heal",
  25: "Death", 26: "Invisibility", 27: "Ring of Fire", 28: "Mana Breath",
  29: "Time Stop", 30: "Teleport", 31: "Summon Daemon", 32: "Frost Storm",
  33: "Earthquake", 34: "Lightning Storm", 35: "Inferno", 36: "Searing Rain",
  37: "Sanctify", 38: "Bind", 39: "Restoration", 40: "Return", 41: "Spirit Speak",
  42: "Reunite Body and Spirit", 43: "Summon Pyros", 44: "Barrier of Life", 45: "Armageddon",
};
const combatShown: readonly [number, string][] = [[8, "Knocked down"], [9, "Knocked back"], [10, "Getting up"]];

const spellMana = [0,
  0, 0, 0, 0, 5, 15, 5, 5, 10, 5, 10, 10, 30, 20, 15, 15, 15, 10, 20, 20,
  25, 25, 25, 25, 35, 30, 30, 30, 20, 35, 35, 35, 40, 40, 40, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0] as const;
const manaFor = (n: number, discount: boolean) => {
  const cost = spellMana[n] ?? 0;
  return discount && cost > 0 ? Math.max(1, Math.floor(cost * 0.25)) : cost;
};
const spellRows: readonly [string, readonly number[]][] = [
  ["Linear", [1, 2, 3, 4]],
  ...[1, 2, 3, 4, 5, 6, 7, 8].map((c): [string, number[]] => [circles[c]!, [1, 2, 3, 4].map(i => 4 * c + i)]),
  ["Rituals", [37, 38, 39, 40]], ["", [41, 42, 43, 44]], ["", [45]],
];
type SpellState = "active" | "ready" | "short" | "unknown";
const spellStateWords: Record<SpellState, string> = { active: "running", ready: "castable", short: "not enough mana", unknown: "not learned" };

function SpellGrid({ known, running, mana, discount }: { known: readonly number[]; running: readonly number[]; mana: number; discount: boolean }) {
  const stateOf = (n: number): SpellState => running.includes(n) ? "active"
    : !known.includes(n) ? "unknown" : mana >= manaFor(n, discount) ? "ready" : "short";
  return (
    <div className="u9gl-spell-grid" role="list" aria-label="Spellbook">
      {spellRows.map(([label, row], r) => (
        <div className="u9gl-spell-row" role="listitem" key={r} aria-label={label || "Rituals, continued"}>
          <span className="u9gl-spell-label" aria-hidden={label ? undefined : true}>{label}</span>
          {row.map(n => {
            const state = stateOf(n), cost = manaFor(n, discount), name = spellNames[n] ?? `Spell ${n}`;
            return (
              <span key={n} className="u9gl-spell" data-state={state} aria-label={`${name}, ${cost} mana, ${spellStateWords[state]}`}>
                <small aria-hidden="true">M-{String(cost).padStart(2, "0")}</small>
                <strong aria-hidden="true">{name}</strong>
              </span>
            );
          })}
        </div>
      ))}
    </div>
  );
}


const p = (label: string, field: string, extra: Partial<Metric> = {}): Metric =>
  ({ label, group: "player", field, when: recordGate, ...extra });
const purse = (label: string, field: string) =>
  p(label, field, { text: (_v, raw) => (Number(raw) < 0 ? "not read" : String(raw)) });
const word = (label: string, field: keyof typeof levelWords) =>
  p(label, field, { text: (_v, raw) => `${levelWord(levelWords[field], raw)} (${raw})` });

const healthState = (percent: number): LcarsSystemState => percent < 25 ? "critical" : percent < 50 ? "warning" : "nominal";
const breathState = (percent: number): LcarsSystemState => percent < 30 ? "critical" : percent < 100 ? "warning" : "nominal";

function VitalBar({ label, kind, value, max, base, state }: {
  label: string; kind: "health" | "mana" | "breath"; value: string; max: string; base: string; state: (percent: number) => LcarsSystemState;
}) {
  const cur = Number(value), top = Number(max);
  const percent = top > 0 ? Math.min(100, Math.max(0, cur * 100 / top)) : 0;
  return (
    <div className="u9gl-vital" data-vital={kind} data-condition={top > 0 ? state(percent) : "offline"}>
      <div className="u9gl-vital-label"><strong>{label}</strong><span>{cur} / {top}{base !== max ? ` · base ${base}` : ""}</span></div>
      <div className="u9gl-vital-track" role="meter" aria-label={label} aria-valuemin={0} aria-valuemax={top} aria-valuenow={cur}
        aria-valuetext={`${cur} of ${top}`}>
        <div className="u9gl-vital-fill" style={{ width: `${percent}%` }} />
      </div>
    </div>
  );
}

export function U9glPlayerPage({ ctx }: { ctx: MetricContext }) {
  const g = ctx.sample?.groups.player;
  const v = g?.values;
  const record = v?.recordValid === "1", datums = v?.datumsValid === "1";
  const stale = g?.quality === "stale";
  const none = (why: string) => <p className="lcars-supporting">{why}</p>;
  const absent = !g ? "No Avatar yet: a world loads first." : null;

  const weapon = record ? Number(v!.weapon) : -1;
  const shrineBits = datums ? Number(v!.shrines) : 0;
  const spells: number[] = [];
  if (record) {
    const book = BigInt(v!.spellsLo ?? "0") | BigInt(v!.spellsHi ?? "0") << 64n;
    for (let n = 1; n <= 45; n++) if ((book >> BigInt(n)) & 1n) spells.push(n);
  }
  const flags = record ? Number(v!.stateFlags) : 0;
  const combat = record ? Number(v!.combatStates ?? "0") : 0, stunMs = record ? Number(v!.stunMs ?? "0") : 0;
  const stunLeft = stunMs > 1 ? stunMs - (g?.ageMs ?? 0) : 0;
  const active: (readonly [number, string])[] = [...effects.filter(([bit]) => flags & bit),
    ...(stunLeft > 0 ? [[-1, `Stunned ${(stunLeft / 1000).toFixed(1)} s`] as const] : stunMs === 1 ? [[-1, "Stunned"] as const] : []),
    ...combatShown.filter(([st]) => combat & (1 << st)).map(([st, name]) => [-100 - st, name] as const)];
  const running: number[] = [];
  if (record && v!.activeSpells !== undefined) {
    const on = BigInt(v!.activeSpells);
    for (let n = 1; n <= 45; n++) if ((on >> BigInt(n)) & 1n) running.push(n);
  }
  const spellName = (n: number) => spellNames[n] ?? `Spell ${n}`;

  const gold = record ? Number(v!.gold) : -1;
  const hp = record ? Number(v!.health) : 0, hpMax = record ? Number(v!.healthMax) : 0;
  const classId = datums && v!.classChosen === "1" ? Number(v!.classId) : -1;
  const carried = record ? slotFields.filter(f => Number(v![f] ?? "0")).length : 0;
  const codes: BandCode[] = [
    gold >= 0 ? { text: `${gold.toLocaleString("en-US")} GP` } : null,
    record && hpMax > 0 ? { text: `HP ${hp}/${hpMax}`, state: healthState(hp * 100 / hpMax) } : null,
    classId >= 0 ? { text: classes[classId]?.[0] ?? `Class ${classId}` } : null,
    !record ? null
      : active.length ? { text: active.length + running.length > 1 ? `${active[0]![1]} +${active.length + running.length - 1}` : active[0]![1], state: "warning" }
      : running.length ? { text: running.length > 1 ? `${spellName(running[0]!)} +${running.length - 1}` : spellName(running[0]!) }
      : { text: "None" },
    datums ? { text: `${bitCount(shrineBits)}/8` } : null,
    record ? { text: `${carried} carried` } : null,
    record && weapon >= 0 && weapon < skills.length ? { text: skills[weapon]!.name } : null,
    record ? { text: `${spells.length} spells` } : null,
  ];
  const b = (i: number) => band("player", i, codes[i]);

  return (
    <div className="u9gl-overlay-grid u9gl-readouts u9gl-player" data-quality={stale ? "stale" : "ready"}>
      <LcarsPanel band={b(0)} id="player-attributes" title="Attributes" headingLevel="h3" className="u9gl-wide">
        <div className="u9gl-player-columns">
          <div>
            <MetricReadout metric={word("Strength", "strength")} ctx={ctx} />
            <MetricReadout metric={word("Dexterity", "dexterity")} ctx={ctx} />
            <MetricReadout metric={word("Intelligence", "intelligence")} ctx={ctx} />
          </div>
          <div>
            <MetricReadout metric={p("Armour", "armour")} ctx={ctx} />
            <MetricReadout metric={p("From shield", "armourBonus")} ctx={ctx} />
            <MetricReadout metric={p("Magic resistance bonus", "magicResistBonus")} ctx={ctx} />
          </div>
          <div>
            <MetricReadout metric={purse("Gold", "gold")} ctx={ctx} />
            <MetricReadout metric={purse("Arrows", "arrows")} ctx={ctx} />
          </div>
        </div>
      </LcarsPanel>

      <LcarsPanel band={b(1)} id="player-vitals" title="Vitals" headingLevel="h3" className="u9gl-wide">
        {record ? <div className="u9gl-vital-bars">
          <VitalBar label="Hit points" kind="health" value={v!.health!} max={v!.healthMax!} base={v!.healthBase!} state={healthState} />
          <VitalBar label="Mana" kind="mana" value={v!.mana!} max={v!.manaMax!} base={v!.manaBase!} state={() => "nominal"} />
          <VitalBar label="Breath" kind="breath" value={v!.breath!} max={v!.breathMax!} base={v!.breathBase!} state={breathState} />
        </div> : none(absent ?? recordGate[1])}
        <p className="lcars-supporting u9gl-section-note">Each bar is the current value against the maximum with bonuses; the base maximum is beside it.</p>
      </LcarsPanel>

      <LcarsPanel band={b(2)} id="player-class" title="Class and magic" headingLevel="h3">
        <MetricReadout metric={{ label: "Class", group: "player", field: "classId", when: datumGate,
          text: (vals, raw) => vals.classChosen === "1" ? classes[Number(raw)]?.[0] ?? `unknown (${raw})` : "Not chosen yet" }} ctx={ctx} />
        <MetricReadout metric={{ label: "Virtue", group: "player", field: "classId", when: datumGate,
          text: (vals, raw) => vals.classChosen === "1" ? classes[Number(raw)]?.[1] ?? "" : "The gypsy's reading is not done" }} ctx={ctx} />
        <MetricReadout metric={{ label: "Magic", group: "player", field: "shrines", when: datumGate,
          text: vals => circles[bitCount(Number(vals.shrines))]! }} ctx={ctx} />
        <MetricReadout metric={{ label: "Spells known", group: "player", field: "spellsLo", when: recordGate,
          text: () => String(spells.length) }} ctx={ctx} />
      </LcarsPanel>

      <LcarsPanel band={b(3)} id="player-effects" title="Status effects" headingLevel="h3">
        {!record ? none(absent ?? recordGate[1]) : active.length + running.length
          ? <ul className="u9gl-player-tags">
              {active.map(([bit, name]) => <li key={bit} data-state="warning">{name}</li>)}
              {running.map(n => <li key={`spell-${n}`}><strong>{spellName(n)}</strong> <span>spell</span></li>)}
            </ul>
          : <p className="lcars-copy">None.</p>}
      </LcarsPanel>

      <LcarsPanel band={b(4)} id="player-shrines" title="Shrines" headingLevel="h3" className="u9gl-wide">
        {!datums ? none(absent ?? datumGate[1]) : <>
          <ul className="u9gl-player-shrines" aria-label="Shrines cleansed">
            {virtues.map((name, i) => {
              const done = (shrineBits >> i) & 1;
              return <li key={name} data-state={done ? "done" : "open"}><strong>{name}</strong><span>{done ? "Cleansed" : "Not cleansed"}</span></li>;
            })}
          </ul>
          <Codex shrineBits={shrineBits} />
          <p className="lcars-supporting">{bitCount(shrineBits)} of 8 cleansed. Each adds 10 hit points, 10 mana and +1 to an attribute of your choice (Strength +30 hit points, Intelligence +10 mana).</p>
        </>}
      </LcarsPanel>

      <LcarsPanel band={b(5)} id="player-equipment" title="Equipment" headingLevel="h3" className="u9gl-wide">
        {!record ? none(absent ?? recordGate[1]) : <Equipment values={v!} />}
      </LcarsPanel>

      <LcarsPanel band={b(6)} id="player-combat" title="Weapon skills and special moves" headingLevel="h3" className="u9gl-wide">
        {!record ? none(absent ?? recordGate[1]) : <>
          <p className="lcars-copy">In hand: <strong>{weapon >= 0 && weapon < skills.length ? skills[weapon]!.name : weapon === -1 ? "nothing" : `unknown (${weapon})`}</strong></p>
          <div className="u9gl-table-scroll">
            <table className="u9gl-player-table u9gl-moves-table" aria-label="Weapon moves">
              <thead><tr><th scope="col">Weapon</th><th scope="col">Level</th>
                {moveKeys.map((k, i) => <th scope="col" key={k}>{levelWords.skill[i + 1]}<span>{k}</span></th>)}
                <th scope="col">Next lesson</th></tr></thead>
              <tbody>
                {skills.map((s, i) => {
                  const level = Math.max(0, Math.min(3, Number(v![s.field] ?? "0")));
                  const dex = Number(v!.dexterity ?? "0");
                  return <tr key={s.field} data-in-hand={i === weapon ? "1" : "0"}>
                    <th scope="row">{s.name}{i === weapon ? " (in hand)" : ""}</th>
                    <td>{levelWords.skill[level]}</td>
                    {s.moves.map(([move, trainer], m) => <td key={move} data-known={m < level ? "1" : "0"}>
                      <strong>{move}</strong><span>{trainer}</span></td>)}
                    <td>{level >= 3 ? "Mastered" : <>
                      {s.moves[level]![1].split(",")[0]}<span data-met={dex >= level + 1 ? "1" : "0"}>
                        needs {levelWords.dexterity[level + 1]} dexterity{dex >= level + 1 ? " (met)" : ""}</span></>}</td>
                  </tr>;
                })}
              </tbody>
            </table>
          </div>
        </>}
      </LcarsPanel>

      <LcarsPanel band={b(7)} id="player-spells" title="Spellbook" headingLevel="h3" className="u9gl-wide">
        {!record ? none(absent ?? recordGate[1]) :
          <SpellGrid known={spells} running={running} mana={Number(v!.mana ?? "0")} discount={v!.spellDiscount === "1"} />}
      </LcarsPanel>

    </div>
  );
}
