using System.Text.Json;

namespace U9GL.Core.Install;

public sealed class InstallManifest
{
    public const string FileName = "install.json";

    private static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    public string Version { get; set; } = "";
    public DateTime InstalledUtc { get; set; }
    public string GameSha256 { get; set; } = "";
    public List<DeployedFile> Files { get; set; } = [];
    public List<IniEdit> OptionsEdits { get; set; } = [];
    public List<BackupEntry> Backups { get; set; } = [];

    public static InstallManifest? Load(string path) =>
        File.Exists(path) ? JsonSerializer.Deserialize<InstallManifest>(File.ReadAllBytes(path), Json) : null;

    public void Save(string path) => SafeFile.WriteAllBytes(path, JsonSerializer.SerializeToUtf8Bytes(this, Json));
}

public sealed record DeployedFile(string Path, string Sha256, string Version);

public sealed record IniEdit(string Section, string Key, string? OldValue, string NewValue, string Reason, DateTime WhenUtc);

public sealed record BackupEntry(string OriginalPath, string BackupPath, string Sha256, DateTime WhenUtc);
