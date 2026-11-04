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
                                  string? Build = null, string? Error = null)
{
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

    public bool AllKnownGood => Exists && Changed == 0 && Missing == 0 && Unreadable == 0;
}

public sealed record GameDataProgress(string Folder, int FilesDone, int FilesTotal, long BytesDone, long BytesTotal, string? Current);

public sealed class GameDataChecker(GameDataManifest? manifest = null)
{
    private const int BufferSize = 1 << 20;

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
            return new GameDataFolderResult(folder, path, false, expected.Values.Select(Absent).ToList(), 0,
                                            clock.Elapsed, DateTime.UtcNow, fingerprint, [], Manifest.Id);

        var onDisk = Directory.EnumerateFiles(path).ToDictionary(p => Path.GetFileName(p).ToLowerInvariant(), p => p,
                                                                 StringComparer.OrdinalIgnoreCase);
        var subfolders = group.ReportExtras
            ? Directory.EnumerateDirectories(path).Select(Path.GetFileName).OfType<string>().Order(StringComparer.OrdinalIgnoreCase).ToList()
            : [];
        var strays = group.ReportExtras
            ? onDisk.Keys.Except(expected.Keys).Order(StringComparer.OrdinalIgnoreCase).ToList()
            : [];

        var work = expected.Select(e => (Name: e.Value[0].Name, Path: onDisk.GetValueOrDefault(e.Key), Rows: e.Value)).ToList();
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
            var match = sized.FirstOrDefault(r => string.Equals(r.Sha256, sha, StringComparison.OrdinalIgnoreCase));
            if (match == null) return changed;
            string baseBuild = rows.Select(r => r.Build).Order(StringComparer.Ordinal).First();
            var state = match.Build == baseBuild ? GameDataState.Ok : GameDataState.Patched;
            return new GameDataFile(folder, name, state, match.Bytes, length, match.Build);
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
