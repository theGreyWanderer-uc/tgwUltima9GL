using System.Text.Encodings.Web;
using System.Text.Json;

namespace U9GL.Core.Saves;

public sealed class SaveBackupException(string message) : Exception(message);

public static class SaveBackup
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        TypeInfoResolver = new System.Text.Json.Serialization.Metadata.DefaultJsonTypeInfoResolver(),
    };

    public static string Create(string archive, string saveDirectory, string output, int? slot, DateTimeOffset? now = null)
    {
        ValidateOutput(output, saveDirectory, archive);
        if (!File.Exists(archive)) throw new SaveBackupException("The selected save file is not there, so it cannot be backed up.");
        var loose = LooseFiles(saveDirectory);
        var files = new List<(string Source, string Relative)> { (archive, $"archive/{Path.GetFileName(archive)}") };
        files.AddRange(loose.Select(f => (f, $"working/{Path.GetFileName(f)}")));
        if (files.Any(f => new FileInfo(f.Source).LinkTarget != null))
            throw new SaveBackupException("A save file is a symbolic link; the backup copies real files only.");

        var moment = (now ?? DateTimeOffset.UtcNow).ToUniversalTime();
        string stem = $"u9-save-slot-{(slot?.ToString() ?? "unknown")}-{moment:yyyyMMdd'T'HHmmssfff}Z";
        Directory.CreateDirectory(output);
        string folder = NewFolder(output, stem);

        var records = new List<Dictionary<string, object?>>();
        var manifest = new Dictionary<string, object?>
        {
            ["schema_version"] = 1,
            ["complete"] = false,
            ["slot"] = slot,
            ["created_utc"] = moment.ToString("yyyy-MM-dd'T'HH:mm:ss.ffffff'+00:00'"),
            ["archive_source"] = Path.GetFullPath(archive),
            ["working_source"] = Path.GetFullPath(saveDirectory),
            ["start_dat_present"] = loose.Any(f => Path.GetFileName(f).Equals("start.dat", StringComparison.OrdinalIgnoreCase)),
            ["working_files_are_current"] = true,
            ["files"] = records,
            ["note"] = "Working files are the current folder snapshot and may belong to another slot. No installed map files or other slot archives are included.",
        };
        string manifestPath = Path.Combine(folder, "manifest.json");
        WriteManifest(manifestPath, manifest);
        try
        {
            var before = files.ToDictionary(f => f.Source, f => Signature(f.Source), StringComparer.OrdinalIgnoreCase);
            foreach (var (source, relative) in files)
            {
                string target = Path.Combine(folder, relative.Replace('/', Path.DirectorySeparatorChar));
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                File.Copy(source, target, overwrite: false);
                string digest = Hash(target);
                if (digest != Hash(source) || Signature(source) != before[source])
                    throw new SaveBackupException("A save file changed during the backup. Quit the game and try again.");
                records.Add(new Dictionary<string, object?>
                {
                    ["path"] = relative,
                    ["source"] = Path.GetFullPath(source),
                    ["bytes"] = new FileInfo(target).Length,
                    ["sha256"] = digest,
                });
            }
            if (!LooseFiles(saveDirectory).SequenceEqual(loose, StringComparer.OrdinalIgnoreCase)
                || before.Any(kv => Signature(kv.Key) != kv.Value))
                throw new SaveBackupException("The save folder changed during the backup. Quit the game and try again.");
            manifest["complete"] = true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or SaveBackupException)
        {
            manifest["error"] = e.Message;
            throw e as SaveBackupException ?? new SaveBackupException($"The backup did not finish: {e.Message}");
        }
        finally
        {
            WriteManifest(manifestPath, manifest);
        }
        return folder;
    }

    public static void ValidateOutput(string output, string saveDirectory, params string[] sources)
    {
        string target = Full(output);
        string save = Full(saveDirectory);
        var protectedDirectories = new List<string> { save };
        if (Path.GetFileName(save).Equals("savegame", StringComparison.OrdinalIgnoreCase) && Path.GetDirectoryName(save) is { } parent)
            protectedDirectories.AddRange(new[] { "static", "runtime" }.Select(n => Path.Combine(parent, n)));
        protectedDirectories.AddRange(sources.Where(Directory.Exists).Select(Full));
        foreach (string directory in protectedDirectories)
            if (target.Equals(directory, StringComparison.OrdinalIgnoreCase)
                || target.StartsWith(directory + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new SaveBackupException("Backups go outside the game's data folders.");
        if (sources.Any(s => Full(s).Equals(target, StringComparison.OrdinalIgnoreCase)))
            throw new SaveBackupException("The backup folder must not be one of the save files.");
    }

    private static List<string> LooseFiles(string saveDirectory) =>
        !Directory.Exists(saveDirectory) ? []
        : new DirectoryInfo(saveDirectory).EnumerateFiles()
            .Where(f => !f.Extension.Equals(".sav", StringComparison.OrdinalIgnoreCase))
            .Select(f => f.FullName).OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase).ToList();

    private static string NewFolder(string output, string stem)
    {
        for (int index = 0; ; index++)
        {
            string folder = Path.Combine(output, index == 0 ? stem : $"{stem}_{index}");
            if (Directory.Exists(folder) || File.Exists(folder)) continue;
            Directory.CreateDirectory(folder);
            return folder;
        }
    }

    private static (long Length, long Ticks) Signature(string path)
    {
        var info = new FileInfo(path);
        return (info.Length, info.LastWriteTimeUtc.Ticks);
    }

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(stream));
    }

    private static void WriteManifest(string path, Dictionary<string, object?> manifest) =>
        File.WriteAllText(path, JsonSerializer.Serialize(manifest, JsonOptions) + "\n");

    private static string Full(string path) => Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
}
