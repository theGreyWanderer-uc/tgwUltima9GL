using System.Diagnostics;
using System.Security.Cryptography;

namespace U9GL.Core.Data;

public enum GameDataState
{
    Ok,
    Patched,
    Changed,
    Missing,
    Extra,
    Unreadable,
}

public sealed record GameDataFile(string Folder, string Name, GameDataState State, long ExpectedBytes, long ActualBytes,
                                  string? Build = null, string? Error = null, bool Backup = false,
                                  IReadOnlyList<string>? Builds = null)
{
    public IReadOnlyList<string> Builds { get; init; } = Builds ?? (Build == null ? [] : [Build]);

    public bool IsProblem => State is GameDataState.Changed or GameDataState.Missing or GameDataState.Unreadable;
}

public sealed record GameDataFolderResult(
    string Folder, string Path, bool Exists, IReadOnlyList<GameDataFile> Files, long BytesRead,
    TimeSpan Took, DateTime CheckedUtc, string Fingerprint, IReadOnlyList<string> Subfolders, string ManifestId)
{
    public int Total => Files.Count(f => f.State != GameDataState.Extra);

    public int Ok => Files.Count(f => f.State == GameDataState.Ok);
    public int Patched => Files.Count(f => f.State == GameDataState.Patched);
    public int Changed => Files.Count(f => f.State == GameDataState.Changed);
    public int Missing => Files.Count(f => f.State == GameDataState.Missing);
    public int Extra => Files.Count(f => f.State == GameDataState.Extra);
    public int Unreadable => Files.Count(f => f.State == GameDataState.Unreadable);
    public IReadOnlyList<GameDataFile> Problems() => Files.Where(f => f.IsProblem).ToList();

    public IReadOnlyList<IReadOnlyList<string>> PatchSets() =>
        GameDataPatch.Distinct(Files.Where(f => f.State == GameDataState.Patched && !f.Backup).Select(f => f.Builds));

    public bool AllKnownGood => Exists && Changed == 0 && Missing == 0 && Unreadable == 0;
}

public enum GameDataPatchKind
{
    None,
    One,
    Mixed,
}

public sealed record GameDataPatchVerdict(GameDataPatchKind Kind, string? Build, IReadOnlyList<string> Names,
                                          IReadOnlyList<string>? Fits = null)
{
    public IReadOnlyList<string> Fits { get; init; } = Fits ?? (Build == null ? [] : [Build]);
}

public sealed record FanPatchFamily(string Id, string Title, string? Latest, string About, string? Saves = null);

public static class GameDataPatch
{
    public static readonly IReadOnlyList<FanPatchFamily> Families =
    [
        new("FW", "Forgotten World", "FW 1.19H",
            "The Forgotten World team's fixes to music, the companions and the floating Runes."),
        new("DP", "Grandor's Dialogue Patch", "DP 1.6",
            "Grandor Dragon's rewritten dialogue, books, journal and item names (fansforultima.com).",
            "Works with existing saves, but journal entries already written keep the original wording; its readme advises a new game."),
        new("Economy", "Novum's Economy Patch", null,
            "Novum's shop prices and stock, shipped with the Dialogue Patch.",
            "Works with existing saves."),
        new("Monster", "Novum's Monster Patch", null,
            "Novum's tougher monsters, shipped with the Dialogue Patch."),
    ];

    public static string FamilyOf(string build)
    {
        int space = build.IndexOf(' ');
        return space < 0 ? build : build[..space];
    }

    public static string VersionOf(string build)
    {
        int space = build.IndexOf(' ');
        return space < 0 ? "" : build[(space + 1)..];
    }

    public static GameDataPatchVerdict ResolveFamily(IEnumerable<IReadOnlyList<string>> sets, string family) =>
        Resolve(sets.Select(s => (IReadOnlyList<string>)s.Where(b => FamilyOf(b) == family).ToList()));

    public static IReadOnlyList<(string Family, GameDataPatchVerdict Verdict)> ResolveAll(IEnumerable<IReadOnlyList<string>> sets)
    {
        var all = sets.ToList();
        var present = all.SelectMany(s => s).Select(FamilyOf).Distinct().ToList();
        var order = Families.Select(f => f.Id).ToList();
        int Rank(string f) { int i = order.IndexOf(f); return i < 0 ? int.MaxValue : i; }
        return present.OrderBy(Rank).ThenBy(f => f, StringComparer.Ordinal)
                      .Select(f => (f, ResolveFamily(all, f))).ToList();
    }

    public static readonly IComparer<string> Oldest = StringComparer.Ordinal;

    public static IReadOnlyList<IReadOnlyList<string>> Distinct(IEnumerable<IReadOnlyList<string>> sets)
    {
        var seen = new HashSet<string>();
        var result = new List<IReadOnlyList<string>>();
        foreach (var set in sets)
        {
            var sorted = set.Distinct().Order(Oldest).ToList();
            if (sorted.Count > 0 && seen.Add(string.Join("|", sorted))) result.Add(sorted);
        }
        return result;
    }

    public static GameDataPatchVerdict Resolve(IEnumerable<IReadOnlyList<string>> sets)
    {
        var all = Distinct(sets);
        if (all.Count == 0) return new GameDataPatchVerdict(GameDataPatchKind.None, null, []);
        IEnumerable<string> common = all[0];
        foreach (var set in all.Skip(1)) common = common.Intersect(set);
        var fits = common.Order(Oldest).ToList();
        if (fits.Count > 0) return new GameDataPatchVerdict(GameDataPatchKind.One, fits[0], [fits[0]], fits);
        var names = all.Where(s => s.Count == 1).Select(s => s[0]).Distinct().Order(Oldest).ToList();
        if (names.Count == 0) names = all.SelectMany(s => s).Distinct().Order(Oldest).ToList();
        return new GameDataPatchVerdict(GameDataPatchKind.Mixed, null, names);
    }
}

public sealed record GameDataProgress(string Folder, int FilesDone, int FilesTotal, long BytesDone, long BytesTotal, string? Current);

public sealed class GameDataChecker(GameDataManifest? manifest = null)
{
    private const int BufferSize = 1 << 20;

    private static readonly HashSet<(string Folder, string Name)> GameWritten = [("static", "u9game0.sav")];

    public GameDataManifest Manifest { get; } = manifest ?? GameDataManifest.Load();

    public static string Fingerprint(string path)
    {
        if (!Directory.Exists(path)) return "absent";
        long bytes = 0, newest = 0;
        int count = 0;
        foreach (string file in Directory.EnumerateFiles(path))
        {
            var info = new FileInfo(file);
            count++;
            bytes += info.Length;
            newest = Math.Max(newest, info.LastWriteTimeUtc.Ticks);
        }
        return $"{count}:{bytes}:{newest}";
    }

    public GameDataFolderResult Check(string gameRoot, string folder, IProgress<GameDataProgress>? progress = null,
                                      CancellationToken cancel = default)
    {
        var group = GameDataManifest.GroupFor(folder);
        string path = group.PathIn(gameRoot);
        var clock = Stopwatch.StartNew();
        string fingerprint = Fingerprint(path);
        var expected = Manifest.InFolder(folder).GroupBy(e => e.Name.ToLowerInvariant())
                               .ToDictionary(g => g.Key, g => g.ToList());
        if (!Directory.Exists(path))
            return new GameDataFolderResult(folder, path, false, expected.Values.Where(Shipped).Select(Absent).ToList(), 0,
                                            clock.Elapsed, DateTime.UtcNow, fingerprint, [], Manifest.Id);

        var onDisk = Directory.EnumerateFiles(path).ToDictionary(p => Path.GetFileName(p).ToLowerInvariant(), p => p,
                                                                 StringComparer.OrdinalIgnoreCase);
        var subfolders = group.ReportExtras
            ? Directory.EnumerateDirectories(path).Select(Path.GetFileName).OfType<string>().Order(StringComparer.OrdinalIgnoreCase).ToList()
            : [];
        var strays = group.ReportExtras
            ? onDisk.Keys.Except(expected.Keys).Where(k => !GameWritten.Contains((folder, k)))
                    .Order(StringComparer.OrdinalIgnoreCase).ToList()
            : [];

        var work = expected.Select(e => (Name: e.Value[0].Name, Path: onDisk.GetValueOrDefault(e.Key), Rows: e.Value))
                           .Where(w => w.Path != null || Shipped(w.Rows)).ToList();
        long bytesTotal = work.Where(w => w.Path != null).Sum(w => Length(w.Path!));

        long bytesDone = 0;
        int filesDone = 0;
        var files = new GameDataFile[work.Count];
        var options = new ParallelOptions
        {
            CancellationToken = cancel,
            MaxDegreeOfParallelism = Math.Clamp(Environment.ProcessorCount / 2, 2, 6),
        };

        void Report(string? current) => progress?.Report(new GameDataProgress(folder,
            Volatile.Read(ref filesDone), work.Count, Interlocked.Read(ref bytesDone), bytesTotal, current));

        Report(null);
        Parallel.For(0, work.Count, options, index =>
        {
            var item = work[index];
            files[index] = Classify(folder, item.Name, item.Path, item.Rows, ref bytesDone, cancel);
            Interlocked.Increment(ref filesDone);
            Report(item.Name);
        });

        var all = files.ToList();
        all.AddRange(strays.Select(key => new GameDataFile(folder, Path.GetFileName(onDisk[key]), GameDataState.Extra,
                                                          0, Length(onDisk[key]))));
        return new GameDataFolderResult(folder, path, true, all, Interlocked.Read(ref bytesDone), clock.Elapsed,
                                        DateTime.UtcNow, fingerprint, subfolders, Manifest.Id);
    }

    private static bool Shipped(List<GameDataEntry> rows) => rows.Any(r => r.Build == GameDataManifest.BaseBuild);

    private static GameDataFile Absent(List<GameDataEntry> rows) =>
        new(rows[0].Folder, rows[0].Name, GameDataState.Missing, rows[0].Bytes, 0);

    private static GameDataFile Classify(string folder, string name, string? filePath, List<GameDataEntry> rows,
                                         ref long bytesDone, CancellationToken cancel)
    {
        if (filePath == null) return Absent(rows);
        long length = Length(filePath);
        var changed = new GameDataFile(folder, name, GameDataState.Changed, rows[0].Bytes, length);
        try
        {
            var sized = rows.Where(r => r.Bytes == length).ToList();
            if (sized.Count == 0) return changed;
            string sha = Sha256(filePath, ref bytesDone, cancel);
            var matches = sized.Where(r => string.Equals(r.Sha256, sha, StringComparison.OrdinalIgnoreCase)).ToList();
            if (matches.Count == 0) return changed;
            var stock = matches.FirstOrDefault(r => r.Build == GameDataManifest.BaseBuild);
            if (stock != null) return new GameDataFile(folder, name, GameDataState.Ok, stock.Bytes, length, stock.Build);
            var builds = matches.Select(r => r.Build).Distinct().Order(GameDataPatch.Oldest).ToList();
            var first = matches.First(r => r.Build == builds[0]);
            return new GameDataFile(folder, name, GameDataState.Patched, first.Bytes, length, first.Build,
                                    Backup: first.Backup, Builds: builds);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return changed with { State = GameDataState.Unreadable, Error = e.Message };
        }
    }

    private static long Length(string path)
    {
        try { return new FileInfo(path).Length; }
        catch (IOException) { return 0; }
    }

    private static string Sha256(string path, ref long bytesDone, CancellationToken cancel)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, BufferSize,
                                          FileOptions.SequentialScan);
        byte[] buffer = new byte[BufferSize];
        int read;
        while ((read = stream.Read(buffer)) > 0)
        {
            cancel.ThrowIfCancellationRequested();
            hash.AppendData(buffer, 0, read);
            Interlocked.Add(ref bytesDone, read);
        }
        return Convert.ToHexStringLower(hash.GetHashAndReset());
    }
}
