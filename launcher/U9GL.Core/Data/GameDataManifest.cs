using System.Reflection;

namespace U9GL.Core.Data;

public sealed record GameDataEntry(string Folder, string Name, long Bytes, string Sha256, string Build);

public sealed record GameDataGroup(string Folder, string Title, string Description, bool ReportExtras)
{
    public string PathIn(string gameRoot) => Folder == "." ? gameRoot : Path.Combine(gameRoot, Folder);
}

public sealed class GameDataManifest
{
    private const string ResourceName = "U9GL.Core.Data.u9-game-data.tsv";

    public static readonly IReadOnlyList<GameDataGroup> Groups =
    [
        new(".", "Program files", "u9.exe and the four libraries it loads.", false),
        new("static", "Static data", "The world: fixed maps, models, textures, animations, text and scripts. The game only reads these.", true),
        new("sound", "Sound data", "Music, speech and sound effects.", true),
        new("runtime", "Runtime maps", "Each map's starting objects. A save keeps its own copy of the maps it changed, in savegame; a map your save has never touched loads from here.", true),
        new("Movies", "Movies", "The cutscenes.", true),
    ];

    public static GameDataGroup GroupFor(string folder) =>
        Groups.FirstOrDefault(g => g.Folder == folder) ?? new GameDataGroup(folder, folder, "", true);

    private readonly Dictionary<(string Folder, string Name), List<GameDataEntry>> _byFile;

    private GameDataManifest(IReadOnlyList<GameDataEntry> entries, IReadOnlyList<string> notes, string id)
    {
        Entries = entries;
        Notes = notes;
        Id = id;
        _byFile = entries.GroupBy(e => (e.Folder, Name: e.Name.ToLowerInvariant()))
                         .ToDictionary(g => g.Key, g => g.ToList());
    }

    public string Id { get; }

    public IReadOnlyList<GameDataEntry> Entries { get; }

    public IReadOnlyList<string> Notes { get; }

    public IReadOnlyList<string> Builds() => Entries.Select(e => e.Build).Distinct().Order(StringComparer.Ordinal).ToList();

    public static GameDataManifest Load() => _default ??= Parse(ReadResource());

    private static GameDataManifest? _default;

    public static GameDataManifest Parse(string text)
    {
        var entries = new List<GameDataEntry>();
        var notes = new List<string>();
        foreach (string raw in text.Split('\n'))
        {
            string line = raw.TrimEnd('\r');
            if (line.Length == 0) continue;
            if (line[0] == '#')
            {
                if (!line.StartsWith("#folder", StringComparison.Ordinal)) notes.Add(line.TrimStart('#', ' '));
                continue;
            }
            string[] parts = line.Split('\t');
            if (parts.Length != 5 || !long.TryParse(parts[2], out long bytes))
                throw new InvalidDataException($"malformed manifest row: {line}");
            entries.Add(new GameDataEntry(parts[0], parts[1], bytes, parts[3], parts[4]));
        }
        if (entries.Count == 0) throw new InvalidDataException("the manifest has no entries");
        string id = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(
            System.Text.Encoding.UTF8.GetBytes(text)))[..12];
        return new GameDataManifest(entries, notes, id);
    }

    public IReadOnlyList<GameDataEntry> For(string folder, string name) =>
        _byFile.TryGetValue((folder, name.ToLowerInvariant()), out var rows) ? rows : [];

    public IEnumerable<GameDataEntry> InFolder(string folder) =>
        Entries.Where(e => e.Folder == folder);

    public int FileCount(string folder) =>
        Entries.Where(e => e.Folder == folder).Select(e => e.Name.ToLowerInvariant()).Distinct().Count();

    private static string ReadResource()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"{ResourceName} is not embedded in U9GL.Core");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
