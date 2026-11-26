using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace U9GL.Core.Saves;

public sealed record SnapshotCheck(bool Ok, string Text);

public sealed class SnapshotRunException(string message) : Exception(message);

public static class NewGameSnapshot
{
    public const string RequestName = "newgame-snapshot.request";
    public const string ResultName = "newgame-snapshot.result";
    private const string ManifestName = "manifest.json";

    public static readonly IReadOnlyList<(ushort Type, string Name)> StartItems = [(5300, "toolbelt"), (5259, "backpack"), (946, "journal")];

    public static string SnapshotPath(string gameRoot) => Path.Combine(gameRoot, "static", "u9game0.sav");
    public static string SaveDirectory(string gameRoot) => Path.Combine(gameRoot, "savegame");

    public static bool Exists(string gameRoot) => File.Exists(SnapshotPath(gameRoot));

    public static SnapshotCheck Check(string gameRoot)
    {
        string path = SnapshotPath(gameRoot);
        if (!File.Exists(path)) return new(false, "There is no new-game snapshot.");
        U9SaveArchive archive;
        try { archive = U9SaveArchive.FromFile(path); }
        catch (Exception e) when (e is U9SaveException or IOException or UnauthorizedAccessException)
        {
            return new(false, $"The snapshot is not a readable save archive: {e.Message}");
        }
        var maps = archive.Nonfixed.Select(m => m.MapNumber).ToHashSet();
        if (!maps.Contains(0) || !maps.Contains(14))
            return new(false, $"The snapshot lacks map {(maps.Contains(0) ? 14 : 0)}.");
        U9Nonfixed map14;
        try { map14 = new U9Nonfixed(archive.Member("nonfixed.14")!.Data.ToArray()); }
        catch (U9NonfixedException e) { return new(false, $"The snapshot's map 14 cannot be read: {e.Message}"); }
        var types = map14.Chunks.SelectMany(c => c.Entities).Select(e => e.TypeIndex).ToHashSet();
        var missing = StartItems.Where(i => !types.Contains(i.Type)).Select(i => i.Name).ToList();
        if (missing.Count > 0)
            return new(false, $"The snapshot's map 14 has no {string.Join(" or ", missing)}: it was taken after they were picked up.");
        return new(true, $"A whole save archive with {archive.Nonfixed.Count} maps; the toolbelt, backpack and journal are on map 14.");
    }

    public static string SetAside(string gameRoot, string holdingRoot, DateTimeOffset? now = null)
    {
        string saves = SaveDirectory(gameRoot);
        string stamp = (now ?? DateTimeOffset.Now).ToString("yyyyMMdd-HHmmss");
        Directory.CreateDirectory(holdingRoot);
        string folder = Path.Combine(holdingRoot, $"snapshot-run-{stamp}");
        for (int i = 1; Directory.Exists(folder); i++) folder = Path.Combine(holdingRoot, $"snapshot-run-{stamp}-{i}");
        string working = Path.Combine(folder, "working");
        Directory.CreateDirectory(working);

        var files = new JsonArray();
        var manifest = new JsonObject
        {
            ["game_folder"] = gameRoot,
            ["save_folder"] = saves,
            ["restored"] = false,
            ["files"] = files,
        };
        var loose = LooseFiles(saves);
        foreach (string source in loose)
        {
            string target = Path.Combine(working, Path.GetFileName(source));
            File.Copy(source, target);
            string digest = Hash(target);
            if (digest != Hash(source)) throw new SnapshotRunException($"The copy of {Path.GetFileName(source)} did not match it. Nothing was moved.");
            files.Add(new JsonObject { ["name"] = Path.GetFileName(source), ["sha256"] = digest });
        }
        WriteManifest(folder, manifest);
        foreach (string source in loose) File.Delete(source);
        return folder;
    }

    public static void Restore(string folder)
    {
        var manifest = ReadManifest(folder) ?? throw new SnapshotRunException($"{folder} has no readable manifest.");
        if (manifest["restored"]?.GetValue<bool>() == true) return;
        string saves = manifest["save_folder"]!.GetValue<string>();
        Directory.CreateDirectory(saves);

        var left = LooseFiles(saves);
        if (left.Count > 0)
        {
            string run = Path.Combine(folder, "from-the-run");
            Directory.CreateDirectory(run);
            foreach (string file in left)
            {
                string target = Path.Combine(run, Path.GetFileName(file));
                if (File.Exists(target)) File.Delete(target);
                File.Move(file, target);
            }
        }
        foreach (var node in manifest["files"]!.AsArray())
        {
            string name = node!["name"]!.GetValue<string>();
            string target = Path.Combine(saves, name);
            File.Copy(Path.Combine(folder, "working", name), target, overwrite: true);
            if (Hash(target) != node["sha256"]!.GetValue<string>())
                throw new SnapshotRunException($"{name} did not come back as it was. The original is in {Path.Combine(folder, "working")}.");
        }
        manifest["restored"] = true;
        WriteManifest(folder, manifest);
    }

    public static IReadOnlyList<string> Unrestored(string holdingRoot, string gameRoot) =>
        !Directory.Exists(holdingRoot) ? []
        : Directory.EnumerateDirectories(holdingRoot, "snapshot-run-*")
            .Where(d => ReadManifest(d) is { } m && m["restored"]?.GetValue<bool>() == false
                        && string.Equals(m["game_folder"]?.GetValue<string>(), gameRoot, StringComparison.OrdinalIgnoreCase))
            .OrderBy(d => d, StringComparer.OrdinalIgnoreCase).ToList();

    public static SnapshotCheck? ReadResult(string bundle)
    {
        string path = Path.Combine(bundle, ResultName);
        if (!File.Exists(path)) return null;
        var lines = File.ReadAllLines(path);
        return new(lines.Length > 0 && lines[0].Trim() == "ok", lines.Length > 1 ? lines[1].Trim() : "");
    }

    private static List<string> LooseFiles(string saves) =>
        !Directory.Exists(saves) ? []
        : new DirectoryInfo(saves).EnumerateFiles()
            .Where(f => !f.Extension.Equals(".sav", StringComparison.OrdinalIgnoreCase))
            .Select(f => f.FullName).OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase).ToList();

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static JsonObject? ReadManifest(string folder)
    {
        try { return JsonNode.Parse(File.ReadAllText(Path.Combine(folder, ManifestName))) as JsonObject; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException) { return null; }
    }

    private static void WriteManifest(string folder, JsonObject manifest)
    {
        string path = Path.Combine(folder, ManifestName);
        File.WriteAllText(path + ".tmp", manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        File.Move(path + ".tmp", path, overwrite: true);
    }
}
