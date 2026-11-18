namespace U9GL.Core.Settings;

public enum SettingFile { OptionsIni, U9glIni, KeyMap }

public enum LedgerStatus { Works, Partial, Untested, Broken }

public enum SettingPage { Display, Graphics, Game, Fixes, Diagnostics, Managed }

public sealed record SettingKey(SettingFile File, string? Section, string Key);

public sealed record SettingReset(string Key, string Value, Version Before, string Why);

public sealed record SettingChoice(string Label, IReadOnlyDictionary<string, string> Values, LedgerStatus Status, string Note = "");

public sealed record SettingDefinition(
    string Id,
    SettingPage Page,
    string Title,
    string Help,
    IReadOnlyList<SettingKey> Keys,
    IReadOnlyList<SettingChoice> Choices,
    string DefaultLabel,
    LedgerStatus? CustomStatus = null,
    bool InLedger = true,
    bool OnlyValue = false)
{
    public SettingChoice Default => Choices.First(c => c.Label == DefaultLabel);

    public IEnumerable<SettingChoice> PageChoices =>
        Choices.Where(c => c.Status is LedgerStatus.Works or LedgerStatus.Partial);

    public IEnumerable<SettingChoice> ExperimentalChoices => Choices.Where(c => c.Status == LedgerStatus.Untested);

    public bool HasExperimental => CustomStatus == LedgerStatus.Untested || ExperimentalChoices.Any();
}

public static class SettingsCatalog
{
    public static IReadOnlyList<SettingDefinition> All { get; } = Build();

    public static SettingDefinition ById(string id) => All.First(d => d.Id == id);

    public static IEnumerable<KeyValuePair<string, string>> ManagedU9glValues() =>
        ValuesOf(All.Where(d => d.Page == SettingPage.Managed));

    public static IReadOnlyList<SettingReset> UpdateResets { get; } =
    [
        new("metrics_overlay", "0", new Version(0, 5, 7),
            "the metrics overlay is off by default since 0.5.7; Ctrl+F12 shows it"),
    ];

    public static IEnumerable<KeyValuePair<string, string>> InitialU9glValues() =>
        ValuesOf(All.Where(d => d.Page != SettingPage.Managed));

    public static IEnumerable<(string Section, string Key, string Value)> InitialOptionsValues(
        Func<string, string, string?> current) =>
        All.Where(d => d.Page is SettingPage.Graphics or SettingPage.Game
                       && d.Keys.Count == 1 && d.Keys[0].File == SettingFile.OptionsIni)
           .Select(d => (Key: d.Keys[0], d, Now: current(d.Keys[0].Section!, d.Keys[0].Key)))
           .Where(x => x.Now != null && x.Now != x.d.Default.Values[x.Key.Key])
           .Select(x => (x.Key.Section!, x.Key.Key, x.d.Default.Values[x.Key.Key]));

    public const string TelemetryFeedKey = "diag_telemetry";

    public static bool IsManaged(SettingFile file, string key) =>
        All.Any(d => d.Page == SettingPage.Managed &&
                     d.Keys.Any(k => k.File == file && string.Equals(k.Key, key, StringComparison.OrdinalIgnoreCase)));

    private static IEnumerable<KeyValuePair<string, string>> ValuesOf(IEnumerable<SettingDefinition> definitions) =>
        definitions.SelectMany(d => d.Default.Values.Where(v =>
            d.Keys.Any(k => k.File == SettingFile.U9glIni && k.Key == v.Key)));

    private static SettingKey Game(string key) => new(SettingFile.OptionsIni, "Game", key);
    private static SettingKey Performance(string key) => new(SettingFile.OptionsIni, "Performance", key);
    private static SettingKey U9gl(string key) => new(SettingFile.U9glIni, null, key);

    public const string WasdMovementKey = "wasd_movement";

    private static SettingChoice One(string label, string key, string value, LedgerStatus status, string note = "") =>
        new(label, new Dictionary<string, string> { [key] = value }, status, note);

    private static SettingChoice Pair(string label, string keyA, string valueA, string keyB, string valueB, LedgerStatus status) =>
        new(label, new Dictionary<string, string> { [keyA] = valueA, [keyB] = valueB }, status);

    private static SettingChoice Backpack(string label, string capacity, string free, LedgerStatus status) =>
        new(label, new Dictionary<string, string>
            { ["pack_capacity"] = capacity, ["free_placement"] = free, ["icon_pixel_hit"] = free }, status);

    private static SettingChoice Resolution(int width, int height) =>
        Pair($"{width} × {height}", "GameScreenWidth", width.ToString(), "GameScreenHeight", height.ToString(), LedgerStatus.Works);

    public const string StockCrashReturns = "the stock crash returns";

    private static SettingDefinition Fix(string id, string key, string title, string help, LedgerStatus onStatus, string onNote = "") =>
        new(id, SettingPage.Fixes, title, help, [U9gl(key)],
            [One("On", key, "1", onStatus, onNote), One("Off", key, "0", LedgerStatus.Broken, StockCrashReturns)],
            "On");

    private static SettingDefinition FixGroup(string id, string[] keys, string title, string help,
                                              LedgerStatus onStatus, string offNote)
    {
        var on = keys.ToDictionary(k => k, _ => "1");
        var off = keys.ToDictionary(k => k, _ => "0");
        return new(id, SettingPage.Fixes, title, help, keys.Select(U9gl).ToList(),
            [new SettingChoice("On", on, onStatus), new SettingChoice("Off", off, LedgerStatus.Broken, offNote)],
            "On");
    }

    private static SettingDefinition UntestedFix(string id, string key, string title, string help,
                                                 string offNote) =>
        new(id, SettingPage.Fixes, title, help, [U9gl(key)],
            [One("On", key, "1", LedgerStatus.Untested), One("Off", key, "0", LedgerStatus.Partial, offNote)],
            "Off");

    private static SettingDefinition Managed(string key, string value, bool inLedger = true) =>
        new(key, SettingPage.Managed, key, "Managed by the launcher.", [U9gl(key)],
            [One(value, key, value, LedgerStatus.Works)], value, InLedger: inLedger);

    private static SettingDefinition Diag(string key, string title, string help, bool installOn, string on = "1") =>
        new(key, SettingPage.Diagnostics, title, help, [U9gl(key)],
            [One("On", key, on, LedgerStatus.Works), One("Off", key, "0", LedgerStatus.Works)],
            installOn ? "On" : "Off", InLedger: false);

    private static List<SettingDefinition> Build()
    {
        const LedgerStatus Works = LedgerStatus.Works;
        const LedgerStatus Partial = LedgerStatus.Partial;

        return
        [
            new("resolution", SettingPage.Display, "Resolution",
                "The size the game draws at. The window fits it to the monitor; a 16:9 size fills a 16:9 monitor.",
                [Game("GameScreenWidth"), Game("GameScreenHeight")],
                [Resolution(640, 480), Resolution(800, 600), Resolution(856, 480), Resolution(1024, 768),
                 Resolution(1280, 720), Resolution(1600, 1200), Resolution(1920, 1080)],
                "640 × 480", CustomStatus: Works),

            new("window-size", SettingPage.Display, "Window",
                "How big the game's window is.",
                [U9gl("gl_window_size")],
                [One("The game's resolution", "gl_window_size", "1", Works, "If the resolution is bigger than the monitor, the window is shrunk to fit, keeping its shape."),
                 One("Whole monitor, borderless", "gl_window_size", "2", Works),
                 One("Fixed 1280 × 960", "gl_window_size", "0", Works)],
                "The game's resolution"),

            new("window-frame", SettingPage.Display, "Window frame",
                "A title bar to drag the window by: switch to another program, then drag it. Needs "
                + "\"Keep playing\" behind other windows. Only when the window is smaller than the "
                + "monitor; the whole-monitor window always stays borderless.",
                [U9gl("gl_window_frame")],
                [One("Borderless", "gl_window_frame", "0", Works),
                 One("Title bar", "gl_window_frame", "1", Works)],
                "Borderless"),

            new("background-play", SettingPage.Display, "Behind other windows",
                "Keep the game running, drawing and playing sound while another program is in front, "
                + "instead of minimising it.",
                [U9gl("gl_background_play"), U9gl("gl_background_sound")],
                [Pair("Keep playing, with sound", "gl_background_play", "1", "gl_background_sound", "1", Works),
                 Pair("Keep playing, silent", "gl_background_play", "1", "gl_background_sound", "0", Works),
                 Pair("Minimise (the original)", "gl_background_play", "0", "gl_background_sound", "1", Works)],
                "Minimise (the original)"),

            new("window-anchor", SettingPage.Display, "Position on the monitor",
                "Where the window sits when it is smaller than the monitor.",
                [U9gl("gl_window_anchor")],
                [One("Top left", "gl_window_anchor", "7", Works), One("Top centre", "gl_window_anchor", "8", Works),
                 One("Top right", "gl_window_anchor", "9", Works), One("Centre left", "gl_window_anchor", "4", Works),
                 One("Centre", "gl_window_anchor", "5", Works), One("Centre right", "gl_window_anchor", "6", Works),
                 One("Bottom left", "gl_window_anchor", "1", Works), One("Bottom centre", "gl_window_anchor", "2", Works),
                 One("Bottom right", "gl_window_anchor", "3", Works)],
                "Centre"),

            new("stretch", SettingPage.Display, "Stretch to fill",
                "With the whole-monitor window, stretch the picture to fill it, interface included.",
                [U9gl("gl_stretch")],
                [One("Stretch", "gl_stretch", "1", Works),
                 One("Keep shape, with bars", "gl_stretch", "0", Works)],
                "Stretch"),

            new("wide-view", SettingPage.Display, "Wide screens",
                "Wider than 4:3, the original loses height and books run off the screen. Keep the 4:3 "
                + "height for books only, or for the whole game, which shows more at the sides.",
                [U9gl("wide_view")],
                [One("Original", "wide_view", "0", Works),
                 One("Books only", "wide_view", "1", Works),
                 One("Whole game", "wide_view", "2", Works)],
                "Whole game"),

            new("post-pass", SettingPage.Graphics, "Enhanced graphics",
                "Draws each frame offscreen and finishes it in a second pass. Lamp light, shadows, the "
                + "night look and game brightness all need it; Off is the original picture.",
                [U9gl("gl_post")],
                [One("On", "gl_post", "1", Works),
                 One("Off (the original picture)", "gl_post", "0", Works)],
                "On"),

            new("lamp-light", SettingPage.Graphics, "Lamp light",
                "Candles, torches and lamps light the world per pixel and stay on their own side of walls; "
                + "the original lets them shine through walls. The Avatar, furniture and fences cast shadows on "
                + "the floor and walls, from up to 8 lamps. Only what is on screen casts a shadow.",
                [U9gl("gl_light_pools"), U9gl("gl_light_shadows")],
                [Pair("On", "gl_light_pools", "1", "gl_light_shadows", "1", Partial),
                 Pair("Off (the original)", "gl_light_pools", "0", "gl_light_shadows", "0", Works)],
                "On"),

            new("sun-shadows", SettingPage.Graphics, "Sun shadows",
                "The Avatar, buildings and trees cast shadows away from the sun, which follows the time of day; "
                + "roofs shade the rooms under them. They lengthen toward evening and fade at dusk and in storms.",
                [U9gl("gl_sun_shadows"), U9gl("gl_sun_shadow_memory")],
                [Pair("On", "gl_sun_shadows", "1", "gl_sun_shadow_memory", "1", Partial),
                 Pair("Off (the original)", "gl_sun_shadows", "0", "gl_sun_shadow_memory", "0", Works)],
                "On"),

            new("moon-shadows", SettingPage.Graphics, "Moon shadows",
                "At night the brighter moon casts faint shadows, as the sun does by day: strongest at full "
                + "moon, none at new moon, fading at dusk, dawn and in storms.",
                [U9gl("gl_moon_shadows")],
                [One("On", "gl_moon_shadows", "1", Works),
                 One("Off (the original)", "gl_moon_shadows", "0", Works)],
                "On"),

            new("night-grade", SettingPage.Graphics, "Night look",
                "At night the world is greyed, darkened and cooled, so lamps stand out. It follows the game's own "
                + "light, so it is partly applied at dusk, at dawn and in storms, and not at all in dungeons.",
                [U9gl("gl_night_grade")],
                [One("On", "gl_night_grade", "100", Works),
                 One("Off (the original)", "gl_night_grade", "0", Works)],
                "On"),

            new("underwater-haze", SettingPage.Graphics, "Underwater haze",
                "Under the water, the view fades into a deep blue with distance, red first, and grows darker "
                + "the deeper you are. The original shows the water as clear as air.",
                [U9gl("gl_under_fog")],
                [One("On", "gl_under_fog", "1", Works),
                 One("Off (the original)", "gl_under_fog", "0", Works)],
                "On"),

            new("underwater-caustics", SettingPage.Graphics, "Underwater caustics",
                "Under the water, a moving web of light from the ripples above plays over the seabed and "
                + "whatever stands on it, softer the deeper it lies. Not in caves and dungeons.",
                [U9gl("gl_under_caustics")],
                [One("On", "gl_under_caustics", "1", Works),
                 One("Off (the original)", "gl_under_caustics", "0", Works)],
                "On"),

            new("underwater-shafts", SettingPage.Graphics, "Underwater light shafts",
                "Under the water, sunlight slants down through the surface in beams that follow the sun and "
                + "fade at dusk, in storms and at night.",
                [U9gl("gl_under_shafts")],
                [One("On", "gl_under_shafts", "1", Works),
                 One("Off (the original)", "gl_under_shafts", "0", Works)],
                "On"),

            new("anti-aliasing", SettingPage.Graphics, "Anti-aliasing",
                "Draws the world at twice the width and height and averages it down, so edges, leaves and "
                + "distant ground stop shimmering, at a large cost in speed.",
                [U9gl("gl_ss_scale")],
                [One("Off", "gl_ss_scale", "1", Works),
                 One("On (2× supersampling)", "gl_ss_scale", "2", Works)],
                "Off"),

            new("draw-distance", SettingPage.Graphics, "Draw distance",
                "How far the world is drawn above ground. Further costs drawing time.",
                [Performance("FarClippingPlaneDistance")],
                [One("5000 (the original)", "FarClippingPlaneDistance", "5000", Works),
                 One("6000", "FarClippingPlaneDistance", "6000", Works),
                 One("7000", "FarClippingPlaneDistance", "7000", Works),
                 One("8000", "FarClippingPlaneDistance", "8000", Works)],
                "8000"),

            new("object-draw-distance", SettingPage.Graphics, "Building draw distance",
                "How far buildings and other objects are drawn above ground; the land goes on to the draw "
                + "distance. Underground the dungeon draw distance covers both. Further costs drawing time.",
                [Performance("MiddleClippingPlaneDistance")],
                [One("3500 (the original)", "MiddleClippingPlaneDistance", "3500", Works),
                 One("4000", "MiddleClippingPlaneDistance", "4000", Works),
                 One("4500", "MiddleClippingPlaneDistance", "4500", Works),
                 One("5000", "MiddleClippingPlaneDistance", "5000", Works)],
                "4000"),

            new("dungeon-draw-distance", SettingPage.Graphics, "Dungeon draw distance",
                "How far the world is drawn underground - dungeons, and some open places such as the "
                + "elemental planes. Further costs drawing time.",
                [Performance("DungeonClippingPlaneDistance")],
                [One("2000 (the original)", "DungeonClippingPlaneDistance", "2000", Works),
                 One("2500", "DungeonClippingPlaneDistance", "2500", Works),
                 One("3000", "DungeonClippingPlaneDistance", "3000", Works),
                 One("3500", "DungeonClippingPlaneDistance", "3500", Works),
                 One("4000", "DungeonClippingPlaneDistance", "4000", Works)],
                "4000"),

            new("small-object-distance", SettingPage.Graphics, "Small object distance",
                "How far small things - lantern glows, small props - are drawn before the game drops "
                + "them. Further costs drawing time.",
                [Performance("CullStartDist")],
                [One("1000 (the original)", "CullStartDist", "1000", Works),
                 One("1500", "CullStartDist", "1500", Works),
                 One("2000", "CullStartDist", "2000", Works),
                 One("2500", "CullStartDist", "2500", Works),
                 One("3000", "CullStartDist", "3000", Works),
                 One("3500", "CullStartDist", "3500", Works),
                 One("4000", "CullStartDist", "4000", Works)],
                "4000"),

            new("model-detail-distance", SettingPage.Graphics, "Model detail distance",
                "How far buildings and other models keep their full detail before the game swaps in a "
                + "simpler version. Always full detail never swaps. Further costs drawing time. "
                + "The land has its own setting, Land detail distance.",
                [Game("LODScale")],
                [One("1 (the original)", "LODScale", "1.0", Works),
                 One("2 (twice the original)", "LODScale", "2.0", Works),
                 One("Always full detail", "LODScale", "100", Works)],
                "2 (twice the original)"),

            new("land-detail-distance", SettingPage.Graphics, "Land detail distance",
                "How far hills and ground keep their full detail. Past it the game draws them coarse and "
                + "blurred, and they sharpen as you approach; cracks showing sky can open where the two meet.",
                [U9gl("gl_terrain_detail_distance")],
                [One("3072 (the original)", "gl_terrain_detail_distance", "0", Works),
                 One("4096", "gl_terrain_detail_distance", "4096", Works),
                 One("5120", "gl_terrain_detail_distance", "5120", Works),
                 One("6144", "gl_terrain_detail_distance", "6144", Works),
                 One("7168", "gl_terrain_detail_distance", "7168", Works),
                 One("8192", "gl_terrain_detail_distance", "8192", Works),
                 One("9216 (the whole view)", "gl_terrain_detail_distance", "9216", Works)],
                "3072 (the original)"),

            new("post-gamma", SettingPage.Graphics, "Game brightness",
                "Applies the game's own brightness setting to the game's window only. Ignored leaves the picture at full brightness.",
                [U9gl("gl_post_gamma")],
                [One("Applied", "gl_post_gamma", "1", Works),
                 One("Ignored", "gl_post_gamma", "0", Works)],
                "Applied"),

            new("texture-override", SettingPage.Graphics, "Replacement textures",
                "Draws images from the \"textures\" folder beside U9GL.exe in place of the game's own, "
                + "at any size. Each file is named for the texture it replaces.",
                [U9gl("texture_override")],
                [One("Off", "texture_override", "0", Works),
                 One("On", "texture_override", "1", Works)],
                "On"),

            new("texture-detail", SettingPage.Graphics, "Texture detail",
                "The game's own textures load at full size. Replacements always draw at their own size.",
                [Game("GameMipMapLevel")],
                [One("Full (the original)", "GameMipMapLevel", "1", Works)],
                "Full (the original)", OnlyValue: true),

            new("windows-key", SettingPage.Game, "Windows key",
                "Whether the Windows key works while the game has focus.",
                [Game("DisableWinKey")],
                [One("Works as normal", "DisableWinKey", "0", Works),
                 One("Disabled while playing", "DisableWinKey", "1", Works)],
                "Disabled while playing"),

            new("screenshot", SettingPage.Game, "Screenshot (Ctrl+P)",
                "What Ctrl+P saves into the game folder. Print Screen is left to Windows; Ctrl+F9 still "
                + "saves U9GL's own frame dump.",
                [U9gl("gl_screenshot")],
                [One("Off", "gl_screenshot", "0", Works),
                 One("The game's picture, before U9GL (PNG)", "gl_screenshot", "1", Works),
                 One("The finished picture (PNG)", "gl_screenshot", "2", Works),
                 One("The finished picture, as the game's TGA", "gl_screenshot", "3", Works),
                 One("Both PNGs, before and after, to compare", "gl_screenshot", "4", Works)],
                "The finished picture (PNG)"),

            new("gold-cap", SettingPage.Game, "Gold limit",
                "The most gold the Avatar can carry. Past 9,999 the backpack counts in thousands and millions, "
                + "in the purse's own lettering: 12,345 reads 12K.",
                [U9gl("gold_cap")],
                [One("9,999 (the original)", "gold_cap", "9999", Works),
                 One("999,999", "gold_cap", "999999", Works)],
                "999,999"),

            new("backpack", SettingPage.Game, "Backpack and bag",
                "Free placement: items stay where you drop them, may overlap, and the backpack and bags hold "
                + "far more. If you switch back to the grid, items past 20 (9 in a bag) drop beside the Avatar.",
                [U9gl("pack_capacity"), U9gl("free_placement"), U9gl("icon_pixel_hit")],
                [Backpack("Original grid (20 items, bag 9)", "0", "0", Works),
                 Backpack("Free placement, up to 1,024 items", "1024", "1", Works),
                 Backpack("Free placement, up to 4,096 items", "4096", "1", Works)],
                "Original grid (20 items, bag 9)"),

            new("halberd", SettingPage.Game, "Usable halberds",
                "The game has a finished halberd, a staff weapon with its own icon, but leaves every "
                + "halberd in the world as scenery. On lets you pick them up, carry them and fight with "
                + "them, with the battleaxe's sounds. Off is the original game.",
                [U9gl("restore_halberd")],
                [One("On", "restore_halberd", "1", Works),
                 One("Off (the original)", "restore_halberd", "0", Works)],
                "On"),

            new("wasd", SettingPage.Game, "WASD movement",
                "Walk with W, A, S and D as well as the arrow keys, and diagonally with two keys held "
                + "(W and D, say). The spellbook moves from S to E. Off puts it back. The key map is backed up first.",
                [new(SettingFile.KeyMap, null, WasdMovementKey), U9gl("diagonal_move")],
                [Pair("Off (the original)", WasdMovementKey, "0", "diagonal_move", "0", Works),
                 Pair("On", WasdMovementKey, "1", "diagonal_move", "1", Works)],
                "Off (the original)"),

            new("combat-run", SettingPage.Game, "Enemies in combat",
                "In the original, most enemies walk up to you in a fight. With Run they run, weapon drawn. "
                + "Enemies backing away are unchanged.",
                [U9gl("fix_combat_run")],
                [One("Walk (the original)", "fix_combat_run", "0", Works),
                 One("Run", "fix_combat_run", "1", Works)],
                "Run"),

            new("large-address", SettingPage.Game, "Large address space",
                "Up to 4 GB of memory instead of 2 GB, for many high-resolution textures. U9GL starts a "
                + "copy, u9-laa.exe; your u9.exe is never changed.",
                [U9gl("large_address")],
                [One("Off (the original, 2 GB)", "large_address", "0", Works),
                 One("On (4 GB)", "large_address", "1", Works)],
                "On (4 GB)"),

            new("portal-range", SettingPage.Game, "Seeing into buildings",
                "How far from an open building its contents still show. At 700 they can vanish from "
                + "just outside; 1500 is advised.",
                [Performance("MaximumPortalRange")],
                [One("700 (the original)", "MaximumPortalRange", "700", Works),
                 One("1500", "MaximumPortalRange", "1500", Works),
                 One("3000", "MaximumPortalRange", "3000", Works)],
                "1500"),

            new("metrics-overlay", SettingPage.Game, "Metrics overlay",
                "An on-screen panel of frame rate and what the game is drawing: general, objects and "
                + "textures. Ctrl+F12 cycles them while playing.",
                [U9gl("metrics_overlay")],
                [One("Off at start", "metrics_overlay", "0", Works),
                 One("On", "metrics_overlay", "1", Works)],
                "Off at start"),

            Fix("fix-actor-init", "fix_actor_init", "Start new actors with clean memory",
                $"Prevents the {Faults.F3.InSentence}, and {Faults.F4.InSentence}.", Works),
            Fix("fix-f3", "fix_f3", "Saves load past damaged creature data",
                $"Prevents the {Faults.F3.InSentence} when a creature's data is already damaged: the game "
                + "skips the damaged part instead of crashing.", Works),
            Fix("fix-reload-imm", "fix_reload_imm", "Hostile NPCs keep attacking",
                $"Keeps {Faults.F4.InSentence} from coming back when the game sets up a creature again.", Works),
            Fix("fix-rebuild-flag", "fix_rebuild_flag", "Hostile spellcasters cast",
                "Some hostile spellcasters never cast a spell, depending on what was left in memory when "
                + "they were made. This sets them up properly, so they cast every time.", Works),
            Fix("fix-f5", "fix_f5", "Teleporting spellcasters no longer crash",
                $"Prevents the {Faults.F5.InSentence}.", LedgerStatus.Partial,
                "when it steps in, that teleport is cancelled and the spellcaster stays put"),
            Fix("fix-composed-slot", "fix_composed_slot", "Cutscenes start and skip safely",
                $"Prevents the {Faults.F8.InSentence}.", Works),
            Fix("fix-f7", "watch_f7", "Flying no longer crashes",
                $"Prevents the {Faults.F7.InSentence}. When the world streams in and out fast, a missing "
                + "object is noted in the log and skipped.", Works),
            FixGroup("fix-spell-orphans", ["fix_spell_orphans", "fix_spell_caster"],
                "End spells whose caster is gone",
                $"Prevents the {Faults.F11.InSentence}, and the {Faults.F12.InSentence}. A spell whose "
                + "caster has been unloaded - you flew or ran far away - fizzles instead of landing.",
                Works, StockCrashReturns),
            Fix("fix-absent-target", "fix_absent_target", "Creatures stop at a fallen Avatar",
                $"Prevents the {Faults.F15.InSentence}, and the {Faults.F16.InSentence}. Once a "
                + "monster has killed you, creatures no longer attack, turn toward or measure the "
                + "distance to a character the game has already removed.", Works),
            Fix("fix-spell-kind", "fix_spell_kind", "Rings of Fire are not mistaken for spells",
                $"Prevents the {Faults.F19.InSentence}. When two Rings of Fire overlap, the game no longer "
                + "takes a burning ring for a spell and deletes it as one.", Works),
            Fix("fix-bad-models", "fix_bad_models", "Skip models the game cannot read",
                $"Prevents the {Faults.F23.InSentence}. A handful of models in the game's data are "
                + "stored in a layout the game cannot read, and asking for one crashes it. With this "
                + "fix the object is drawn as the game's plain placeholder instead.", Works),
            FixGroup("fix-magenta-key", ["fix_magenta_key"], "See-through flames and effects",
                "Some of the game's flames, sparks, lava bursts and other effects were painted on a "
                + "purple background meant to be see-through, which the game never cuts out. U9GL "
                + "cuts it out, so they show without purple boxes around them.",
                Works, "the purple boxes around some flames and effects come back"),
            Fix("fix-sound-slot", "fix_sound_slot", "Sounds survive a map change",
                $"Prevents the {Faults.F24.InSentence}. When a map change unloads the object a sound "
                + "belongs to, the game measures the sound from where the object was last placed "
                + "instead of crashing.", Works),
            Fix("fix-line-hit", "fix_line_hit", "Collision checks at a map's edge",
                $"Prevents the {Faults.F25.InSentence}. A collision check that finds no real object "
                + "past the edge of a map is treated as finding nothing, instead of crashing the game.", Works),
            Fix("fix-shrine-reveal", "fix_shrine_reveal", "Shrine cleansings always finish",
                $"Prevents the {Faults.F18.InSentence}. If the shrine's own rune or item has been picked up, "
                + "moved or left on another map, the cleansing brings it back to the altar, or makes a new one.", Works),
            FixGroup("fix-lett-book", ["fix_lett_book"], "Lett's book teaches from the backpack",
                "Sir William Lett's book in his tomb teaches the last two-handed move, but the game "
                + "only lets it when the book is read lying on the ground, and never says whether it "
                + "worked. With this on it teaches the same way when read from the backpack, and when "
                + "the book is closed a line says whether you learned it. The lesson still needs Adept "
                + "dexterity and Journeyman two-handed skill.",
                Works, "the original: the book teaches only when read lying on the ground"),
            FixGroup("fix-rain-splash", ["fix_rain_splash"], "Keep rain splashes out of buildings",
                "In rain, splashes could show on the floor inside a building, as in Lord British's "
                + "throne room. With this on, rain that falls into a room makes no splash.",
                Works, "the original: in rain, splashes show on the floor of Lord British's throne room"),
            FixGroup("fix-indoor-fog", ["fix_indoor_fog"], "Clear the storm's haze indoors",
                "In rain the whole scene is fogged, rooms included, so a long room fills with haze. "
                + "With this on, the fog inside a building is as in clear weather.",
                Works, "the original: in rain, rooms are as hazy as outdoors"),
            FixGroup("fix-lightning-hall", ["fix_lightning_hall"], "No lightning flash in closed rooms",
                "Lightning lights every wall in view. In closed rooms and high halls such as Lord "
                + "British's throne room that looks wrong, so with this on a strike there stays dark. "
                + "Houses with windows keep their flashes, and thunder is unchanged.",
                Works, "the original: lightning lights the walls of every room, closed rooms included"),
            FixGroup("fix-clock-rebase", ["fix_clock_rebase"],
                "Fix slow motion after long uptime",
                $"Prevents the {Faults.F13.InSentence}. The game counts time from when Windows "
                + "started; with Fast Startup a Shut down does not reset it, and past about 25 days "
                + "animation crawls. This makes the game count from when it starts instead.",
                Works, "slow-motion animation returns once Windows has been on for about 25 days"),
            FixGroup("fix-blink",
                ["fix_blink_tolerance", "fix_blink_level", "fix_blink_search", "fix_blink_recast", "fix_caster_range"],
                "Let spellcasters' teleports land",
                "A spellcaster's teleport plays its effects but mostly leaves her where she was, and between spells "
                + "she walks back up to you. This tests the spot before she casts, moves it to nearby ground that "
                + "fits, and sets her down on the ground - not in the air, and not up on a roof. She then holds her "
                + "ground at range and is ready to cast almost as soon as she lands.", Works,
                "spellcasters' teleports mostly fail again and they walk back to melee range, as in the original game"),
            FixGroup("fix-chase-paths", ["fix_chase_goal", "fix_chase_obstacles"],
                "Chasing enemies find their way to you",
                "When an enemy chasing you is blocked, the original game sends it on a detour to where you "
                + "were when the chase began, planned as if trees were not there, so it can walk past you or "
                + "stay stuck behind a tree. This aims the detour at where you are now and makes it go round "
                + "what blocked it.",
                Works,
                "blocked enemies detour to where you were and can stay stuck behind trees, as in the original game"),

            FixGroup("fix-collider-ref", ["fix_collider_ref"],
                "Meshes are unloaded when no longer used",
                "The original game keeps an extra reference on some objects' meshes each time it builds their "
                + "collision, so those meshes are never unloaded and near Moonglow the count climbs by thousands a "
                + "second. This drops the extra reference.",
                Works,
                "some meshes are never unloaded, as in the original game"),
            FixGroup("fix-flyer-unstick", ["fix_flyer_unstick"],
                "Free birds stuck in trees",
                "Birds and vultures that spawn inside a tree cannot fly out, and cry again and again. "
                + "This notices a flying creature that keeps trying to move and cannot, for more than "
                + "two and a half seconds, and sets it down at a free spot nearby. Perched and feeding "
                + "birds are left alone.",
                Works,
                "birds can stay stuck in trees, crying, as in the original game"),
            FixGroup("fix-doors", ["fix_doors"],
                "Doors open and close properly",
                "Some doors swing past closed, wobble, or end up half open but treated as shut, so the "
                + "next click opens them further. Triggers beside those doors start a second swing "
                + "while the first is still moving. This lets a door make one swing at a time, keeps "
                + "a door that stopped short marked open so the next click closes it, and closes "
                + "doors already left half open in a save.",
                Works,
                "some doors swing past closed or stay half open, as in the original game"),
            FixGroup("fix-door-portals", ["fix_door_portals"],
                "Doors close off rooms properly",
                "The game marks the doorway a shut door stands in as closed, and does not look through "
                + "it when working out what can be seen from where you are. Loading a save upsets "
                + "those marks: the first load clears them, so shut doors count as open until they "
                + "are used, and a later load can leave a door's doorway marked closed while the door "
                + "stands open. This puts the marks right from the doors themselves.",
                Works,
                "after loading a save, the game can treat shut doors as open and open doors as shut, as in the original game"),
            new("fix-shopkeepers", SettingPage.Fixes, "Shopkeepers stay off their counters",
                "Shopkeepers reading the book on their counter climb up onto it to reach it, and stay standing "
                + "there. With this on they read it from the floor, and anyone already up on a table or counter "
                + "walks back down.",
                [U9gl("fix_furniture_walkoff"), U9gl("fix_readbook_standoff")],
                [new SettingChoice("On", new Dictionary<string, string>
                    { ["fix_furniture_walkoff"] = "1", ["fix_readbook_standoff"] = "80" }, Works),
                 new SettingChoice("Off", new Dictionary<string, string>
                    { ["fix_furniture_walkoff"] = "0", ["fix_readbook_standoff"] = "0" }, LedgerStatus.Broken,
                    "shopkeepers climb onto their counters and stay there, as in the original game")],
                "On"),

            Diag("probe_handles", "Object handle table",
                "Lists what kinds of object the game's handle table holds, at frame 600 and every 1,200 after.", false),
            Diag("area_occupancy", "Area occupancy",
                "How full the busiest area around the camera is (2,045 objects at most), in the overlay and the log.", false),
            Diag("watch_movie", "Cutscene endings",
                "Which way each cutscene ends, and whether the picture comes back after it.", false),
            Diag("watch_composed", "Composed texture slots",
                "Checks the slots the journal, compass and distant terrain are drawn into, once a frame.", false),
            Diag("tex_source_check", "Texture source check",
                "Compares the first 32 textures against the game's own copy, byte for byte.", false),
            Diag("watch_slots", "Actor memory watches",
                $"The watches behind the fixes for the {Faults.F3.InSentence} and {Faults.F4.InSentence}: actor "
                + "ticks, scheduling, allocations and frees.", false),
            Diag("watch_compass", "Compass frames",
                "The composed textures used each frame, logged when the picture changes.", false),
            Diag("draw_watch_frames", "Composed draw order",
                "For the first 900 frames, the composed textures in the order they are drawn.", false, on: "900"),
            Diag("dump_frame", "Frame snapshot",
                "Saves frame 600 as a picture in %TEMP%. Ctrl+F9 saves one at any time.", false, on: "600"),
            Diag("ui_watch", "Journal and interface",
                "Every change to the journal and interface pictures. High volume.", false),
            Diag("perf_log", "Texture upload timing",
                "The time each burst of texture uploads takes. High volume.", false),
            Diag("dump_new_blocks", "New actor memory",
                "Dumps the first 80 new actors' memory as it is handed out, before they are built.", false, on: "80"),
            Diag("diag_actor_watch", "Actors",
                "Every creature and NPC near you, of any kind: what it is, where it is, whom it fights, its state and health, and what changes inside it.", false),
            Diag("dump_textures", "Texture dump",
                "Saves every texture the game draws to %TEMP%\\u9gl_textures, colour and alpha, once each.", false),
            Diag("diag_teleport", "Teleport crash hunt",
                $"Everything for the {Faults.F5.InSentence}: the teleport watch for a long session, plus area sampling.", false),
            Diag("diag_tele_watch", "Teleports",
                "Every spellcaster's cast, teleport, landing and refusal.", false),
            Diag("diag_path_watch", "Chase paths",
                "How a chasing enemy's path changes, and how far its goal is from you.", false),
            Diag("diag_bird_watch", "Birds",
                "Songbirds: perching, flying, when they next sing, and their height.", false),
            Diag("diag_sound_watch", "Sounds",
                "Every sound played for an object, with who made it. High volume.", false),
            Diag("diag_light_watch", "Lights",
                "The game's lights as they appear, move and go, and the ones nearest the camera every 30 seconds.", false),
            Diag("diag_portal_watch", "Doors and doorways",
                "Which doorways count as closed, every change, each door's doorway as it swings, and a check that doors and doorways agree.", false),
            Diag("diag_door_watch", "Door swings",
                "Every step of a door opening and closing - its angle and state - and a note when one stops away from closed.", false),
            Diag("diag_effect_census", "Glows and effects",
                "Each glow and effect texture drawn, with its colour and where it was; a table when the game quits. "
                + "The console's post effects lists the current frame's.", false),
            Diag("diag_shadow_cost", "Shadow cost",
                "Once a second, what the shadows cost: time spent on the guard band, lamp shadows and sun shadows, "
                + "the whole frame, the GPU's passes, which shadow switches were on, how many lights are near you and how far "
                + "each shadow-casting lamp is. The console's pool shadow cost "
                + "turns it on and off in play.", false),
            Diag("diag_stuck_watch", "Stuck flyers",
                "Birds and other flyers that keep trying to move and cannot.", false),
            Diag("diag_bag_watch", "Bags and containers",
                "Every use of a bag, and every container window opening and closing.", false),
            Diag("diag_npc_watch", "NPC table",
                "Changes to NPCs' levels, health and mana, and every creature spawned. Dumps the table at start and exit.", false),
            Diag("diag_travel_watch", "Map changes",
                "Every map load - pad, door or moongate - with where the Avatar was before and after.", false),
            Diag("diag_focus_watch", "Focus changes",
                "Every activation, minimise, close and destroy of the game window, with what the game's quit and focus code would see.", false),
            Diag("diag_linehit_scan", "Collision lists",
                "Every 30 frames, checks the game's collision lists for shapes left behind or pointing at objects that are gone.", false),
            Diag("diag_trigger_watch", "Triggers",
                "Every trigger the game runs - which object, which trigger and what started it - and every quest flag it sets or tests.", false),
            Diag("diag_player_watch", "The Avatar",
                "Every change to the Avatar: attributes, hit points, mana and breath, armour, effects, weapon and "
                + "equipment, weapon skills and spells, class and shrines - with who raised a skill or taught a "
                + "spell, and each narration the game plays (the Lett book's lesson among them).", false),
            Diag("diag_shrine_watch", "Shrine cleansings",
                "Each shrine cleansing: the rune, glyph and items it holds, every stage it passes, and a warning when "
                + "its rune or item is missing. Also the triggers run at a shrine.", false),
            Diag("diag_mesh_stat", "Mesh census",
                "Meshes loaded and in use every 5 seconds, and any whose count keeps climbing.", false),
            Diag("diag_furniture_watch", "NPCs on furniture",
                "NPCs standing on tables, counters and the like, as they get up and down.", false),
            Diag("diag_fastarea", "Area peaks",
                "The fullest each area gets, printed as it happens and when the game quits.", false),
            Diag(TelemetryFeedKey, "Web dashboard feed",
                "Hands the frame, texture and memory counters to the launcher's web dashboard, at most 10 times a "
                + "second. Turned on with the dashboard.", false),

            Managed("rasterizer5", "1"),
            Managed("own_renderer_life", "1"),
            Managed("glide_present", "0"),
            Managed("gl_detach", "1"),
            Managed("loader", "1"),
            Managed("loader_dll", "u9log.dll"),
            Managed("depth_mode", "2"),
            new("gl-window-fixed-size", SettingPage.Managed, "gl_window_w × gl_window_h", "Managed by the launcher.",
                [U9gl("gl_window_w"), U9gl("gl_window_h")],
                [Pair("1280 × 960", "gl_window_w", "1280", "gl_window_h", "960", Works)], "1280 × 960"),
            new("gl-window-position", SettingPage.Managed, "gl_window_x, gl_window_y", "Managed by the launcher.",
                [U9gl("gl_window_x"), U9gl("gl_window_y")],
                [Pair("0, 0", "gl_window_x", "0", "gl_window_y", "0", Works)], "0, 0"),
            new("game-rasterizer", SettingPage.Managed, "Game Rasterizer", "Set to 3 by Install; rasterizer5 = 1 selects 5 from it.",
                [Game("Game Rasterizer")], [One("3", "Game Rasterizer", "3", Works)], "3"),
            new("screen-depth", SettingPage.Managed, "GameScreenDepth", "The renderer forces 16.",
                [Game("GameScreenDepth")], [One("16", "GameScreenDepth", "16", Works)], "16"),
        ];
    }
}
