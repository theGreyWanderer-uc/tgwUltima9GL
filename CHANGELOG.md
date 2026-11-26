# U9GL changes

What changed in each release, for players. Settings named here are in the U9GL launcher.

## 0.6.2 (2026-11-26)

- **New Game from the journal now works.** The original game, as GOG and Steam sell it, ships
  without the save the journal's New Game needs, so a new game started from the journal skipped
  the opening and left the toolbelt, backpack and journal behind. In the launcher, open
  **Save data** and press **Make the new-game snapshot**: the game plays its opening, U9GL saves
  the snapshot and closes the game, in about two minutes. You may skip the intro video with
  Escape. Make it before installing fan patches. Until it exists, Save data carries a yellow badge.
- **Mouse sensitivity.** Set how fast the camera turns, from 50% to 300%, on the Game page. The
  original turns less the wider your screen; "Mouse sensitivity at high resolutions" makes up for
  that on wide screens. "Invert mouse" is there too. The in-game keys still work.
- **Speech and subtitles.** A Conversation section on the Game page: voices with subtitles (the
  original), voices without subtitles, or subtitles only, and whether the Avatar repeats the reply
  you choose. With a patch that rewrites the dialogue, choose subtitles only: the recordings still
  say the original lines.
- **Fan patches recognised.** A Fan patches card on the dashboard names what is installed: the
  Forgotten World patches (1.19.01, 1.19G, 1.19H), Grandor's Dialogue Patch (1.4, 1.5, 1.6) and
  Novum's Economy and Monster patches. It says when a newer version exists and what each patch
  means for your saves. A changed file no known patch explains is described instead of only being
  called modified.
- **A changed u9.exe is described.** If your u9.exe is not the original, the dashboard's Game data
  card now says which parts of it differ, and names what stat mods change there: weapon damage and
  reach, damage types, blocking and hit effects, shields, armour, gold piles, and creature
  resistances. It tells an edit to these game values apart from a change to the game's code.
  For now it only reports: U9GL still needs the original u9.exe to install and play.
- **Saves started before a fan patch.** Save data and the dashboard point out saves started before
  Forgotten World 1.19G or 1.19H (they miss the companions and may keep the floating Runes), or
  before the Monster Patch (monsters keep their old hit points). Such saves still load normally.
- **Launcher.** Save data is now second in the list, under the Dashboard. The dashboard has four
  cards, and checks the game's data files as it opens, reading only what changed. No check runs
  while the game is running. The Game page is split into sections.

## 0.6.1 (2026-11-24)

Bug fixes only. Where a problem came in with a recent release, its line says which.

- **Fixed: wrong textures on very wide screens.** At ultrawide resolutions such as 3440x1440,
  water, rock and other textures could cover the ground, sky, trees and characters for a moment,
  most often with the sea in view or in fog. U9GL ran out of room for the frame's pieces and drew
  the rest with the last texture. It now has four times the room, and if a frame ever runs out it
  leaves the rest out instead of drawing it wrong. The limit is as old as U9GL.
- **Fixed: a book leaves its shadow on the wall.** Near a wall lit by lamps, opening and closing a
  book left a large dark book shape on the wall. Came in with 0.5.8.
- **Fixed: the poison's green breaks up over water.** Poisoned with the sea in view, part of the
  screen lost the green tint and showed a grey patch with a stepped edge. Reflections on the water
  now take the tint like everything else.
- **Fixed: a dark square follows the Avatar over water.** Standing at the water's edge, a square
  without reflection showed on the water around the Avatar. Came in with 0.5.8.

## 0.6.0 (2026-11-18)

- **Hills and ground keep their detail further away.** New "Land detail distance" on the
  Graphics page. The game draws distant land coarse and blurred and sharpens it as you approach;
  set this further and the land stays sharp out to the whole view. New installs start at 9216
  (the whole view); if you are updating, pick it on the Graphics page.
- **Models can keep full detail at any distance.** "Model detail distance" has a new
  "Always full detail" choice.
- **More choices for the distance settings.** The draw, building, dungeon and small-object
  distances have new steps between the existing ones, so you can fine-tune them.
- **WASD movement.** A new switch on the Game page: walk with W, A, S and D as well as the arrow
  keys, and diagonally with two keys held (W and D, say) without the Avatar or the camera
  turning. The spellbook moves from S to E. The game's key map is backed up first, and Off puts
  it back.
- **Softer sun shading on characters.** The side of a character turned from the sun now darkens
  gradually across the body instead of in patches.

## 0.5.9 (2026-11-17)

- **Faster, especially near lamps.** The lamp shadows added in 0.5.8 cost much less, and several
  parts of U9GL that ran every frame are cheaper. On our test PC: about 6 fps more in Lord
  British's throne room and about 8 more in Yew.
- **The web dashboard's game feed turns off with the dashboard.** If "Enable the local web
  dashboard" was ever ticked and later unticked, the game kept feeding it, at a cost to frame rate.
  Play now switches the feed off whenever the dashboard is off.

## 0.5.8 (2026-11-16)

- **Fill lights are off on every map.** These are invisible lights the game scatters to lift the
  dark; as light pools they shone through walls, for example lighting the ground outside the
  Gypsy's wagon from inside it. Britannia and the Avatar's house kept them until now.
  **After unzipping, press Update in the launcher** so the new setting takes effect.
- **Lamp shadows hold steady.** By the Gypsy's wagon, lamp light and shadow flashed and shadows
  vanished as the camera turned. Lamps now remember walls that have left the screen, walls seen
  from behind cast shadows, and fires cast shadows like lamps. This costs frame rate in busy spots.
- **Tree shadows no longer come and go as you turn.** A tree whose top was just above the picture
  cast no shadow on the grass in view; it does now.
- **No shadow trail behind a flying Avatar.**
- **Waterfalls look right.** Teal shards on the waterfall near the Avatar's house, and spray
  drawn under the water on Britannia's falls, are fixed; the trees and cliff no longer show
  reflected on the falling water.

## 0.5.7 (2026-11-15)

- **Keep playing behind other windows.** Switch to another program and the game keeps running,
  drawing and playing its sound, instead of minimising itself. Keys you type elsewhere do not reach
  it, the mouse moves freely over it, and one click brings it back (Display page: "Behind other
  windows", with or without sound).
- **Play in a window.** When the window is smaller than the monitor, it can have a title bar.
  To move it, switch to another program, drag the title bar, then click back into the game
  (Display page: "Window frame"; needs "Keep playing" behind other windows). The whole-monitor
  window stays borderless.
- **The Ctrl+F12 overlay is off by default.** 0.5.6 showed it to everyone. Updating from 0.5.6
  turns it off once; press Ctrl+F12 in the game to show it, or choose "On" in the Game page's
  "Metrics overlay" to start with it.

## 0.5.6 (2026-11-13)

- **No black screen without OpenGL.** Where the PC cannot run OpenGL at all (a Remote Desktop
  session, a virtual machine without a graphics driver), the game now starts with its own Glide
  renderer, as if U9GL were off, instead of a black window that had to be closed by force.
- **Halberds are held by the shaft,** not by the tip of the blade.
- **The cathedral's light shaft is smooth,** without the grainy patches in the beam and on the
  floor.
- **Anti-aliasing is off in a new install:** it costs too much speed. Turn it on in the Graphics
  page's "Anti-aliasing".
- **Fixed: About's "Copy details" closed the launcher** when another program was using the
  clipboard.
- **GOG and Steam** are both named as supported wherever the launcher and the readme said GOG
  alone.
- **A replacement-texture example:** `textures\example` holds a sample picture that turns part
  of the sky into a smiley, and a README on making your own replacement textures.
- Small renderer fixes for stability.

## 0.5.5 (2026-11-09)

- **Every creature fights back.** Some townsfolk and creatures could chase you and never strike, a
  bug in the original game. They now fight as they should (Fixes page: "Start new actors with
  clean memory").
- **Usable halberds.** The game has a finished halberd but leaves every one as scenery. Now you
  can pick them up, carry them and fight with them (Game page: "Usable halberds").
- **Under the water.** The view fades into a deep blue with distance and grows darker with depth;
  light ripples over the bed and sunlight slants down through the surface (Graphics page:
  "Underwater haze", "Underwater caustics", "Underwater light shafts").
- **The Black Gate hall glows.** The Guardian, the small columns' glyphs, the Black Gate itself,
  the mirror, the throne and the floor have their own glow and light. Black walls stay black near
  a torch.
- **The gold count says K and M** past 9,999 and 999,999, in the purse's own lettering.
- **Fixed: purple boxes round flames and effects** (a burning oil flask, flame arrows, fireballs,
  sparkles and more), a bug in the original game (Fixes page: "See-through flames and effects").
- **Fixed: crashes** at the edge of a map that does not wrap, when a sound's object was unloaded
  by a map change, and on sixteen models the game cannot read (Fixes page: "Collision checks at a
  map's edge", "Sounds survive a map change", "Skip models the game cannot read").
- **Fixed: Sir William Lett's book** now teaches from the backpack too, not only when read lying
  on the ground (Fixes page: "Lett's book teaches from the backpack").
- **The Steam release is supported.**
- **New installs start with smoother edges** (anti-aliasing on), Ctrl+P saving a PNG and the
  backpack on the original grid.
- **Web dashboard: the Player page.** Hit points, mana and breath, attributes, weapon skills and
  moves, class and virtue, the shrines and the Codex, equipment, gold and arrows. The spellbook is a
  grid showing each spell's mana cost and whether it is running, castable or out of reach, and
  status effects show what the game is really doing to you: poison, invisibility, sleep,
  paralysis, stun and knockdown, and the spells running on you. Every section has a heading bar
  with its headline value.
- **Diagnostics are off in a new install.**
- **A download without the .NET runtime** for players who have .NET 10 installed.

## 0.5.4 (2026-11-04)

- **Smoother edges.** Edges are no longer stair-stepped, and leaves and fences shimmer less as
  the camera moves; the belt, bars and compass stay sharp. For the steadiest picture, the
  Graphics page's "Anti-aliasing" draws the world at twice the size and averages it down, at a
  large cost in speed.
- **Model detail distance.** Buildings and other models keep their full detail twice as far
  before the game swaps in simpler ones (Graphics page: "Model detail distance"; 1 is the game
  as shipped).
- **Sun shadows settle.** Distant tree shadows no longer flicker while you stand still: they
  follow the sun in small steps and fade between them. Walls turned away from a high sun are
  evenly shaded instead of showing lit bands beside their own shadow.
- **Stronger sun rays,** longer and drawn from a tighter patch of sky round the sun.
- **More glow.** Lava on models, more crystals, the ice spikes, the Staff of the Dead, the
  Sword of Frost, the Mage Shield, the Kiran Shield, the Lightning Sword, the Fire Armor's
  flames and the glyph plaques glow.
- **More mist becomes fog:** a second ground-mist sheet, the ghost ship's green mist in
  Britain's harbour and the Abyss's green mist. Open books are no longer fogged over.
- **Fixed: a crash in Lord British's fight with Blackthorn,** a bug in the original game: when
  his Rings of Fire overlapped, one was mistaken for a spell and removed as one (Fixes page:
  "Rings of Fire are not mistaken for spells").
- **Fixed: settings with long values were cut short,** so some glowing surfaces never glowed.
- **The web dashboard.** The launcher's new Web Dashboard page can serve a live page of the
  game's frame rate, memory, textures, rendering cost per pass, the world around you, an
  event feed and history, to open in a browser. An option, off by default, lets a phone,
  tablet or another PC on your network open it through a private link or a QR code. Windows
  may ask once whether to let U9GL through the firewall.
- **Settings you save in the launcher stay saved.**

## 0.5.3 (2026-10-26)

- **Glow.** Lava, flames, spells, pentagrams, the moongate, crystals and the rune columns give
  off a soft glow, chosen by what is drawn, so white walls and the sky stay as they are.
- **Fires light their surroundings.** Braziers, campfires, fire rings and candles cast a
  flickering light on what is around them. The dark soot cone the game drew above braziers
  is gone.
- **The runes of virtue stand out:** each rune's symbol glows and casts a small light in its
  colour.
- **The moons.** They glow inside their halos instead of showing as dark discs, and dim with
  their phase, from full to none at new moon.
- **Moon shadows.** At night the brighter moon casts faint shadows, strongest at full moon,
  none at new moon, fading at dusk, dawn and in storms (Graphics page: "Moon shadows").
- **Real fog in Moonglow.** The flat sheet of mist is now a layer of fog: thick across a
  distance, thin underfoot, with drifting wisps, and darker at night with the world.
- **Small things are drawn farther away.** Moonglow's green wall lanterns glow from across the
  courtyard instead of only up close (Graphics page: "Small object distance").
- **More lamps at once:** up to 128 light the world, so busy places such as Moonglow at night
  no longer leave the farther lamps dark.
- **The ghost ship's yellow-green mist glows.**
- **Fixed: diagonal stripes on sunlit walls** seen from a distance.
- **Fixed: surfaces that nearly overlap flickering** between each other as the camera moved,
  such as the rune patterns on the rune columns.
- **Fixed: pink leftovers in the game's own art,** which the original renderer shows too: the
  lava bursts on the lava field west of Britain, the line under the snow on snowy pines, and
  the border round an opened town map.
- **Fixed: a crash when cleansing a shrine** whose rune or item had been picked up or left on
  another map: it is brought back to the altar or made anew (Fixes page: "Shrine cleansings
  always finish").
- **Fixed: a crash after teleporting quickly among flying creatures.**
- **Fixed: a forge's light leaking under its wall.**
- **The plane of Earth is dark** like the other elemental planes.
- **Fixes page:** "Guard the world while flying" and shopkeepers staying off their counters
  are on by default.
- **The two small coloured bars in the top-left corner are gone.**
- **Console:** `moon` shows both moons, `fog` the fog, and `dump <seconds>` takes a frame dump
  after a delay.

## 0.5.2 (2026-10-25)

- **Fixed: a crash every time the game was closed.** It happened in the graphics driver as
  U9GL shut down, so nothing was lost, but Windows recorded a crash each time.
- **Fixed: a carried lantern or torch lighting the room below instead of yours.** On some upper
  floors, Stonegate's top floor among them, the room around you stayed dark until you stood on
  a carpet or jumped.
- **Console:** `gate on` and `gate off` open and close the moongate on Earth without the
  gypsy's reading; `gate` shows its state.
- **Launcher Diagnostics page:** a Triggers switch, for reporting quest and event problems.

## 0.5.1 (2026-10-24)

- **Dungeons are dark; bring a torch.** Everywhere but Britannia and the plane of Earth keeps
  the night's darkness whatever the hour, and a torch you carry burns brighter and reaches
  further there.
- **Lava glows.** Lava lights the caves and passages around it with a warm, slowly pulsing
  light, already before you first see it, and it is no longer darkened at night or in dungeons.
- **Draw distances on the Graphics page:** how far the game draws above ground (8000) and
  underground (4000, twice the original; the Fire plane counts as underground).
- **No more light spots with nothing there.** The game's invisible fill lights showed as
  bright spots and rings on pedestals and pillars in dungeons; outside Britannia they are now
  left out.
- **Sun shadows reach the foot of a tree.** Under a low sun a trunk's shadow began a strip of
  lit ground away from it, and broke into strips on gentle slopes.
- **Shadows on water.** Near the shore, shadows fall on the water instead of the bed seen
  through it, and a shoreline tree's shadow stays when the tree leaves the screen.
- **Lamp posts no longer shadow themselves.** The large hexagon a lantern threw under its own
  post at night, coming and going as the camera turned, is gone.
- **The journal and spellbook look as the original game draws them:** the lamps around the
  Avatar no longer light their pages.
- **No lightning flashes in closed rooms and high halls**, such as Lord British's throne room.
  Houses with windows still flash. On by default (Fixes page: "No lightning flash in closed
  rooms").
- **Fixed: storm haze inside buildings.** In rain the fog no longer fills rooms; it returns as
  you step outside. On by default (Fixes page: "Clear the storm's haze indoors").
- **Fixed: rain splashes on the floor inside buildings**, as in Lord British's throne room.
  On by default (Fixes page: "Keep rain splashes out of buildings").
- **Smoother frame rate in large buildings** such as Lord British's castle, where moving the
  camera could drop to 45 fps with sun shadows on.
- **Fixed: things inside open buildings vanishing** as you look in from just outside, for
  example in the Britain Storehouse. This is the original game's own behaviour; the new
  **Seeing into buildings** setting (Game page) raises the distance, and 1500 is advised.
- **Smoother distant hills in low sunlight.** Sun shadows now fade out with distance, so the
  far hills, which the game draws in coarse detail, no longer show hard dark wedges that
  shifted as you turned. Nearby shadows are unchanged.
- **Launcher About page:** a list of versions to paste into a bug report, with a Copy
  button, and an animated logo while Windows is in light mode. A switch on the page turns
  the animation off.

## 0.5.0 (2026-10-21)

- **Better lighting and shadows** - the launcher's new **Graphics** page, all on by
  default; Off gives the original picture.
  - Candles, torches and lamps light the room around them, and their light stays in the
    room: it comes out of open doors and windows and stops at walls and shut doors, where
    the original game lit straight through walls. Flames flicker smoothly.
  - The lamp lighting you most casts shadows of you, furniture and fences.
  - The sun casts shadows that follow the time of day, lengthen toward evening and fade at
    dusk and in storms. Roofs shade the rooms under them, and sunlight comes in through
    open doors and windows.
  - Night look and Game brightness settings. The game's own brightness setting now reaches
    the OpenGL renderer.
- **Doors open and close properly.** Some doors swung past closed, wobbled, or stayed half
  open while the game treated them as shut, so the next click opened them further, and
  saves kept them that way. A door now makes one swing at a time, and the right leaf of
  Lord British's castle double doors closes the pair again. On by default (Fixes page).
- **Doors close off rooms properly** after loading a save. No visible change yet; the new
  lighting relies on it. On by default (Fixes page).
- **Saved-game checks in plain words.** The launcher's Data page explains each finding,
  what it means and whether to try loading, and can back up a save before you play it.
- **Large address space:** the launcher checks the game copy it makes for this setting, and
  rebuilds it if it has changed.
- **Console:** `pos` names the map you are on.

## 0.4.1 (2026-10-04)

- **Fixed: crashes after the Avatar dies** while monsters are still around. On by default
  (Fixes page: "Creatures stop at a fallen Avatar").
- **Fixed: part of nGlide stayed active** alongside the OpenGL renderer.
- **Fixed: a very large `u9gl.ini` was ignored** and every setting went back to its default.
- **New installs start from recommended settings**: replacement textures, the raised gold
  limit and backpack, and large address space. Updating keeps your own settings, and your
  screen resolution is never changed.
- **Large address space (4 GB)** has been stable in play and moved from Experimental to the
  Game page. The performance overlay moved to the Game page too.
- **Launcher:** a Diagnostics page, an icon, and shorter setting descriptions.
- **Optional NPC fixes** (off by default, Fixes page): shopkeepers stop short of the counter
  when they go to read a book, instead of climbing onto it; NPCs left standing on tables
  and counters walk back down.
- **Console:** `tppos npc <name>` goes to an NPC on the current map, `get npc` / `set npc`
  show and change an NPC's position, target and flags, and `get npc highway <name>` lists
  the places an NPC's schedule uses.
- Smaller DLLs.

## Earlier versions

0.1.0 to 0.4.0 were not tracked.
