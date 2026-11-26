using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace U9GL.Core.Data;

public enum EntryKind
{
    DialogueLine,
    SourceMarker,
    ShopList,
    Book,
    ItemName,
    NpcHealth,
    NpcOther,
}

public sealed record ContentDiff(string Folder, string Name, IReadOnlyList<string> Exact, IReadOnlyList<string> Closest,
                                 int FromClosest, IReadOnlyDictionary<EntryKind, int> FromStock, string? AvatarName = null)
{
    public bool IsStock => Exact.Contains(GameDataManifest.BaseBuild);
    public int ChangedFromStock => FromStock.Values.Sum();
}

public sealed class ContentCheck
{
    private const string ResourceName = "U9GL.Core.Data.u9-entry-fingerprints.tsv";
    private const int NpcRecord = 0x13C;
    private const int NpcHealthAt = 0x34, NpcHealthLength = 6;

    private static readonly Regex ShopLine = new(@"^#\d+\s+""[^""]*""\s+\$\d+", RegexOptions.Multiline | RegexOptions.CultureInvariant);

    private static readonly Regex CapitalisedWord = new(@"\b[A-Z][a-z']{2,}\b", RegexOptions.CultureInvariant);

    private readonly Dictionary<(string Folder, string Name), List<(string Build, string Unit, uint[] Hashes)>> _known;

    private ContentCheck(Dictionary<(string, string), List<(string, string, uint[])>> known) => _known = known;

    private static ContentCheck? _default;

    public static ContentCheck Load() => _default ??= Parse(ReadResource());

    public IEnumerable<(string Folder, string Name)> Files => _known.Keys;

    public static ContentCheck Parse(string text)
    {
        var known = new Dictionary<(string, string), List<(string, string, uint[])>>();
        foreach (string raw in text.Split('\n'))
        {
            string line = raw.TrimEnd('\r');
            if (line.Length == 0 || line[0] == '#') continue;
            string[] p = line.Split('\t');
            if (p.Length != 6 || !int.TryParse(p[4], out int count))
                throw new InvalidDataException($"malformed fingerprint row: {line[..Math.Min(80, line.Length)]}");
            byte[] bytes = Convert.FromBase64String(p[5]);
            if (bytes.Length != count * 4) throw new InvalidDataException($"{p[1]} {p[2]} {p[3]}: {bytes.Length} bytes for {count} hashes");
            var hashes = new uint[count];
            for (int i = 0; i < count; i++) hashes[i] = BitConverter.ToUInt32(bytes, i * 4);
            var key = (p[0], p[1].ToLowerInvariant());
            if (!known.TryGetValue(key, out var list)) known[key] = list = [];
            list.Add((p[2], p[3], hashes));
        }
        return new ContentCheck(known);
    }

    public ContentDiff? Check(string gameRoot, string folder, string name)
    {
        if (!_known.ContainsKey((folder, name.ToLowerInvariant()))) return null;
        string? path = FindFile(Path.Combine(gameRoot, folder), name);
        if (path == null) return null;
        try { return Compare(folder, name, File.ReadAllBytes(path)); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return null; }
    }

    public IReadOnlyList<ContentDiff> CheckAll(string gameRoot) =>
        Files.Select(f => Check(gameRoot, f.Folder, f.Name)).OfType<ContentDiff>().Where(d => !d.IsStock).ToList();

    public ContentDiff? Compare(string folder, string name, byte[] data)
    {
        var builds = _known[(folder, name.ToLowerInvariant())];
        var entries = FlxEntries(data);
        if (entries == null) return null;
        bool npc = builds.Any(b => b.Unit == "record");
        string unit = npc ? "record" : "entry";
        var mine = npc ? NpcRecords(entries) : entries;
        uint[] hashes = mine.Select(Hash).ToArray();

        var distance = builds.Where(b => b.Unit == unit)
                             .ToDictionary(b => b.Build, b => Differing(hashes, b.Hashes).Count);
        var exact = distance.Where(d => d.Value == 0).Select(d => d.Key).Order(GameDataPatch.Oldest).ToList();
        int best = distance.Values.Min();
        var closest = exact.Count > 0 ? [] : distance.Where(d => d.Value == best).Select(d => d.Key).Order(GameDataPatch.Oldest).ToList();

        var stock = builds.First(b => b.Build == GameDataManifest.BaseBuild && b.Unit == unit).Hashes;
        var kinds = new Dictionary<EntryKind, int>();
        uint[]? stockHealth = npc ? builds.First(b => b.Build == GameDataManifest.BaseBuild && b.Unit == "health").Hashes : null;
        foreach (int i in Differing(hashes, stock))
        {
            var kind = npc ? NpcKind(mine, i, stockHealth!) : KindOf(name, i < mine.Count ? mine[i] : []);
            kinds[kind] = kinds.GetValueOrDefault(kind) + 1;
        }

        string? avatar = null;
        if (exact.Count == 0 && name.Equals("text.flx", StringComparison.OrdinalIgnoreCase)
            && closest.Count > 0 && GameDataPatch.FamilyOf(closest[0]) == "DP")
        {
            var reference = builds.First(b => b.Build == closest[^1] && b.Unit == unit).Hashes;
            avatar = CommonWord(Differing(hashes, reference).Select(i => Utf16(mine[i])));
        }
        return new ContentDiff(folder, name, exact, closest, exact.Count > 0 ? 0 : best, kinds, avatar);
    }

    public IReadOnlyList<(int Record, ushort BaseMaximum)> ChangedHealth(byte[] npcFlx)
    {
        var stock = _known.GetValueOrDefault(("runtime", "npc.flx"))
                          ?.FirstOrDefault(b => b.Build == GameDataManifest.BaseBuild && b.Unit == "health").Hashes;
        var entries = FlxEntries(npcFlx);
        if (stock == null || entries == null) return [];
        var records = NpcRecords(entries);
        var result = new List<(int, ushort)>();
        for (int i = 0; i < Math.Min(records.Count, stock.Length); i++)
            if (Hash(records[i][NpcHealthAt..(NpcHealthAt + NpcHealthLength)]) != stock[i])
                result.Add((i, BitConverter.ToUInt16(records[i], NpcBaseMaximumAt)));
        return result;
    }

    public const int NpcBaseMaximumAt = 0x38;

    public static string FamilyOf(EntryKind kind) => kind switch
    {
        EntryKind.ShopList => "Economy",
        EntryKind.NpcHealth or EntryKind.NpcOther => "Monster",
        _ => "DP",
    };

    public static string Describe(IEnumerable<KeyValuePair<EntryKind, int>> kinds)
    {
        var parts = kinds.Where(k => k.Value > 0).OrderBy(k => k.Key).Select(k => Phrase(k.Key, k.Value)).ToList();
        return parts.Count switch
        {
            0 => "",
            1 => parts[0],
            _ => string.Join(", ", parts[..^1]) + " and " + parts[^1],
        };
    }

    private static string Phrase(EntryKind kind, int n)
    {
        string count = n.ToString("N0", System.Globalization.CultureInfo.InvariantCulture);
        string one = kind switch
        {
            EntryKind.DialogueLine => "dialogue line",
            EntryKind.SourceMarker => "source marker",
            EntryKind.ShopList => "shop list",
            EntryKind.Book => "book or sign",
            EntryKind.ItemName => "item name",
            EntryKind.NpcHealth => "NPC's hit points",
            _ => "other NPC record",
        };
        if (n == 1) return $"{count} {one}";
        string many = kind switch
        {
            EntryKind.Book => "books and signs",
            EntryKind.NpcHealth => "NPCs' hit points",
            _ => one + "s",
        };
        return $"{count} {many}";
    }

    private static List<int> Differing(uint[] mine, uint[] known)
    {
        var result = new List<int>();
        for (int i = 0; i < Math.Max(mine.Length, known.Length); i++)
            if ((i < mine.Length ? mine[i] : 0) != (i < known.Length ? known[i] : 0)) result.Add(i);
        return result;
    }

    private static uint Hash(byte[] unit) =>
        unit.Length == 0 ? 0 : BitConverter.ToUInt32(SHA256.HashData(unit), 0);

    private static EntryKind KindOf(string name, byte[] entry) => name.ToLowerInvariant() switch
    {
        "text.flx" => Utf16(entry).EndsWith(" : BEGIN FILE", StringComparison.Ordinal) ? EntryKind.SourceMarker : EntryKind.DialogueLine,
        "books-en.flx" => ShopLine.IsMatch(Encoding.Latin1.GetString(entry)) ? EntryKind.ShopList : EntryKind.Book,
        _ => EntryKind.ItemName,
    };

    private static EntryKind NpcKind(List<byte[]> records, int i, uint[] stockHealth)
    {
        if (i >= records.Count || i >= stockHealth.Length) return EntryKind.NpcOther;
        uint health = Hash(records[i][NpcHealthAt..(NpcHealthAt + NpcHealthLength)]);
        return health != stockHealth[i] ? EntryKind.NpcHealth : EntryKind.NpcOther;
    }

    private static string Utf16(byte[] entry) => Encoding.Unicode.GetString(entry).TrimEnd('\0', ' ');

    private static string? CommonWord(IEnumerable<string> lines)
    {
        HashSet<string>? common = null;
        foreach (string line in lines)
        {
            var words = CapitalisedWord.Matches(line).Select(m => m.Value).ToHashSet(StringComparer.Ordinal);
            if (common == null) common = words; else common.IntersectWith(words);
            if (common.Count == 0) return null;
        }
        return common is { Count: 1 } ? common.First() : null;
    }

    private static List<byte[]>? FlxEntries(byte[] data)
    {
        const int DirectoryAt = 0x80;
        if (data.Length < DirectoryAt) return null;
        int count = BitConverter.ToInt32(data, 0x50);
        if (count <= 0 || count > 65536 || DirectoryAt + (long)count * 8 > data.Length) return null;
        var entries = new List<byte[]>(count);
        for (int i = 0; i < count; i++)
        {
            int offset = BitConverter.ToInt32(data, DirectoryAt + i * 8);
            int length = BitConverter.ToInt32(data, DirectoryAt + i * 8 + 4);
            bool ok = length > 0 && offset >= DirectoryAt && (long)offset + length <= data.Length;
            entries.Add(ok ? data[offset..(offset + length)] : []);
        }
        return entries;
    }

    private static List<byte[]> NpcRecords(List<byte[]> entries)
    {
        byte[] table = entries.Count > 0 ? entries[0] : [];
        var records = new List<byte[]>();
        for (int at = 0; at + NpcRecord <= table.Length; at += NpcRecord) records.Add(table[at..(at + NpcRecord)]);
        return records;
    }

    private static string? FindFile(string directory, string name)
    {
        try
        {
            return Directory.Exists(directory)
                ? Directory.EnumerateFiles(directory).FirstOrDefault(p => string.Equals(Path.GetFileName(p), name, StringComparison.OrdinalIgnoreCase))
                : null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return null; }
    }

    private static string ReadResource()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"{ResourceName} is not embedded in U9GL.Core");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
