using System.Collections.Concurrent;

namespace U9GL.Core.Saves;

public sealed record ShrineObject(string Shrine, int MapNumber, ushort TypeIndex, long WorldX, long WorldY,
                                  ushort StockZ, ushort PatchedZ);

public sealed record FanPatchState(IReadOnlySet<int> Maps, bool? Companions,
                                   IReadOnlyDictionary<ShrineObject, ushort> ShrineHeights)
{
    public IReadOnlyList<ShrineObject> StockShrines() =>
        ShrineHeights.Where(kv => kv.Value == kv.Key.StockZ).Select(kv => kv.Key).ToList();

    public IReadOnlyList<ShrineObject> LoweredShrines() =>
        ShrineHeights.Where(kv => kv.Value == kv.Key.PatchedZ).Select(kv => kv.Key).ToList();
}

public sealed record FanPatchSave(int Slot, bool MissesCompanions, IReadOnlyList<string> FloatingShrines)
{
    public bool IsPartial => MissesCompanions || FloatingShrines.Count > 0;
}

public sealed record FanPatchSaves(bool PatchInstalled, IReadOnlyList<FanPatchSave> Saves, int Unreadable)
{
    public IReadOnlyList<FanPatchSave> Partial => Saves.Where(s => s.IsPartial).ToList();
}

public static class FanPatch
{
    public const int CompanionMap = 9;

    public const ushort CompanionMarkerType = 1134;
    public static readonly IReadOnlyList<ushort> CompanionMarkerLinks = [12460, 12462];

    public static readonly IReadOnlyList<ShrineObject> ShrineObjects =
    [
        new("Compassion", 9, 2940, 77368, 61027, 4365, 3405),
        new("Compassion", 9, 4956, 77341, 60986, 4361, 3380),
        new("Honesty", 9, 2941, 92677, 46920, 4129, 3669),
        new("Honesty", 9, 4938, 92690, 46970, 4108, 3658),
        new("Honor", 9, 2942, 54761, 18066, 2804, 2344),
        new("Honor", 9, 5472, 54706, 18075, 2787, 2325),
        new("Humility", 9, 2943, 82303, 37075, 2985, 2125),
        new("Humility", 9, 5496, 82343, 37046, 2990, 2120),
        new("Justice", 9, 2944, 46344, 86032, 3321, 2621),
        new("Justice", 9, 5508, 46349, 86090, 3300, 2600),
        new("Sacrifice", 9, 2945, 85428, 68598, 2759, 2300),
        new("Sacrifice", 9, 5468, 85433, 68651, 2738, 2310),
        new("Valor", 9, 2947, 33299, 26394, 2937, 2417),
        new("Valor", 9, 5288, 33342, 26390, 2923, 2405),
        new("Spirituality", 22, 2946, 2661, 3790, 4252, 3812),
        new("Spirituality", 22, 5291, 2617, 3824, 4231, 3807),
    ];

    private static readonly int[] ShrineMaps = ShrineObjects.Select(o => o.MapNumber).Distinct().Order().ToArray();

    public static IReadOnlySet<ushort> CompanionMarkers(U9Nonfixed region) =>
        region.Chunks.SelectMany(c => c.Entities)
              .Where(e => e.TypeIndex == CompanionMarkerType && CompanionMarkerLinks.Contains(e.TriggerId))
              .Select(e => e.TriggerId).ToHashSet();

    public static FanPatchState SaveState(IReadOnlyDictionary<int, U9Nonfixed> regions, IReadOnlyList<ShrineObject>? objects = null)
    {
        objects ??= ShrineObjects;
        var heights = new Dictionary<ShrineObject, ushort>();
        foreach (int map in ShrineMaps)
        {
            if (!regions.TryGetValue(map, out var region)) continue;
            var wanted = objects.Where(o => o.MapNumber == map).ToDictionary(o => (o.TypeIndex, o.WorldX, o.WorldY));
            foreach (var e in region.Chunks.SelectMany(c => c.Entities))
                if (wanted.TryGetValue((e.TypeIndex, e.WorldX, e.WorldY), out var item))
                    heights.TryAdd(item, e.Z);
        }
        bool? companions = regions.TryGetValue(CompanionMap, out var nine)
            ? CompanionMarkers(nine).SetEquals(CompanionMarkerLinks)
            : null;
        return new FanPatchState(regions.Keys.ToHashSet(), companions, heights);
    }

    private static readonly ConcurrentDictionary<string, FanPatchState> InstalledCache = new(StringComparer.OrdinalIgnoreCase);

    public static FanPatchState InstalledState(string runtimeDirectory)
    {
        var files = new SortedDictionary<int, FileInfo>();
        foreach (int map in ShrineMaps.Append(CompanionMap).Distinct())
            if (Find(runtimeDirectory, map) is { } file) files[map] = file;
        string key = runtimeDirectory + "|" + string.Join(";", files.Select(kv => $"{kv.Key}:{kv.Value.Length}:{kv.Value.LastWriteTimeUtc.Ticks}"));
        return InstalledCache.GetOrAdd(key, cacheKey =>
        {
            var regions = new Dictionary<int, U9Nonfixed>();
            foreach (var (map, file) in files)
            {
                try
                {
                    var region = new U9Nonfixed(File.ReadAllBytes(file.FullName));
                    _ = region.Chunks;
                    regions[map] = region;
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException or U9NonfixedException
                                              or ArgumentException or IndexOutOfRangeException)
                {
                }
            }
            return SaveState(regions);
        });
    }

    public static FanPatchSaves CheckSaves(string saveDirectory, string runtimeDirectory, CancellationToken cancel = default)
    {
        var installed = InstalledState(runtimeDirectory);
        var lowered = installed.LoweredShrines().ToHashSet();
        bool installedAny = installed.Companions == true || lowered.Count > 0;
        var rows = new List<FanPatchSave>();
        int unreadable = 0;
        if (Directory.Exists(saveDirectory))
        {
            foreach (string path in Directory.EnumerateFiles(saveDirectory, "u9game*.sav"))
            {
                cancel.ThrowIfCancellationRequested();
                string stem = Path.GetFileNameWithoutExtension(path);
                if (!int.TryParse(stem.AsSpan(6), out int slot)) continue;
                try
                {
                    var archive = U9SaveArchive.FromBytes(File.ReadAllBytes(path));
                    var regions = new Dictionary<int, U9Nonfixed>();
                    foreach (var member in archive.Nonfixed)
                        if (member.MapNumber is int map && (map == CompanionMap || ShrineMaps.Contains(map)))
                            regions[map] = new U9Nonfixed(member.Data.ToArray());
                    var saved = SaveState(regions);
                    bool companions = installed.Companions == true && saved.Companions == false;
                    var floating = saved.StockShrines().Where(lowered.Contains).Select(o => o.Shrine)
                                        .Distinct().Order(StringComparer.Ordinal).ToList();
                    rows.Add(new FanPatchSave(slot, companions, floating));
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException or U9SaveException
                                              or U9NonfixedException or ArgumentException or IndexOutOfRangeException)
                {
                    unreadable++;
                }
            }
        }
        return new FanPatchSaves(installedAny, rows.OrderBy(r => r.Slot).ToList(), unreadable);
    }

    private static FileInfo? Find(string directory, int map)
    {
        try
        {
            if (!Directory.Exists(directory)) return null;
            string name = $"nonfixed.{map}";
            return Directory.EnumerateFiles(directory)
                            .Where(p => string.Equals(Path.GetFileName(p), name, StringComparison.OrdinalIgnoreCase))
                            .Select(p => new FileInfo(p)).FirstOrDefault();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return null; }
    }
}
