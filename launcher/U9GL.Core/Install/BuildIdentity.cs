using System.Security.Cryptography;

namespace U9GL.Core.Install;

public sealed record KnownBuild(string Name, string Sha256, long Size, string Source = "");

public sealed record GameBuild(string? Name, string Sha256, long Size, string? Source = null)
{
    public bool IsSupported => Name != null;

    public string Display =>
        Name == null ? "unknown" : string.IsNullOrEmpty(Source) ? Name : $"{Name} ({Source})";
}

public static class BuildIdentity
{
    public static IReadOnlyList<KnownBuild> Known { get; } =
    [
        new("1.19F", "83915175abaf8a65dab96fc42f1feed24a78f55dc8b28a736b61ffa83314b545", 4_593_152, "GOG"),
        new("1.19F", "339a5ba8683546363ab8b95c53d3e980dfbd959afe8719b11f2666c4dbd0e152", 4_603_776, "Steam"),
    ];

    public static GameBuild Identify(string exePath, IReadOnlyList<KnownBuild>? known = null)
    {
        long size = new FileInfo(exePath).Length;
        string hash = Sha256Of(exePath);
        var match = (known ?? Known).FirstOrDefault(k =>
            k.Size == size && string.Equals(k.Sha256, hash, StringComparison.OrdinalIgnoreCase));
        return new GameBuild(match?.Name, hash, size, match?.Source);
    }

    public static string Sha256Of(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexStringLower(SHA256.HashData(stream));
    }
}
