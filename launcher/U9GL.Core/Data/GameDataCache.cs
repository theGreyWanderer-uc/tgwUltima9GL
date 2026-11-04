using System.Text.Json;

namespace U9GL.Core.Data;

public sealed class GameDataCacheEntry
{
    public string Root { get; set; } = "";
    public string Folder { get; set; } = "";

    public string Fingerprint { get; set; } = "";

    public string ManifestId { get; set; } = "";

    public DateTime CheckedUtc { get; set; }
    public int Total { get; set; }
    public int Ok { get; set; }
    public int Patched { get; set; }
    public int Changed { get; set; }
    public int Missing { get; set; }
    public int Extra { get; set; }
    public int Unreadable { get; set; }

    public List<GameDataFile> Problems { get; set; } = [];
}

public sealed class GameDataCache
{
    private const int MaxProblemsKept = 50;
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    private readonly string _path;
    private List<GameDataCacheEntry> _entries = [];

    public GameDataCache(string? path = null) => _path = path ?? DefaultPath;

    public static string DefaultPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "U9GL", "datacheck.json");

    public static GameDataCache Load(string? path = null)
    {
        var cache = new GameDataCache(path);
        try
        {
            if (File.Exists(cache._path))
                cache._entries = JsonSerializer.Deserialize<List<GameDataCacheEntry>>(File.ReadAllText(cache._path), Json) ?? [];
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        {
            cache._entries = [];
        }
        return cache;
    }

    public GameDataCacheEntry? Get(string root, string folder) =>
        _entries.FirstOrDefault(e => string.Equals(e.Root, root, StringComparison.OrdinalIgnoreCase) && e.Folder == folder);

    public static bool IsCurrent(GameDataCacheEntry entry, string root, string manifestId) =>
        entry.ManifestId == manifestId &&
        entry.Fingerprint == GameDataChecker.Fingerprint(GameDataManifest.GroupFor(entry.Folder).PathIn(root));

    public void Put(string root, GameDataFolderResult result)
    {
        _entries.RemoveAll(e => string.Equals(e.Root, root, StringComparison.OrdinalIgnoreCase) && e.Folder == result.Folder);
        _entries.Add(new GameDataCacheEntry
        {
            Root = root,
            Folder = result.Folder,
            Fingerprint = result.Fingerprint,
            ManifestId = result.ManifestId,
            CheckedUtc = result.CheckedUtc,
            Total = result.Total,
            Ok = result.Ok,
            Patched = result.Patched,
            Changed = result.Changed,
            Missing = result.Missing,
            Extra = result.Extra,
            Unreadable = result.Unreadable,
            Problems = result.Problems().Take(MaxProblemsKept).ToList(),
        });
        Save();
    }

    private void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
            File.WriteAllText(_path, JsonSerializer.Serialize(_entries, Json));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}
