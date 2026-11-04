using System.Diagnostics;

namespace U9GL.Core.Install;

public sealed record ComponentVersion(bool Exists, bool IsOurs, Version? Version, string? ProductVersion)
{
    public static ComponentVersion Missing { get; } = new(false, false, null, null);

    public string Display => Version?.ToString(3) ?? (Exists ? "no U9GL version" : "missing");
}

public interface IFileVersionReader
{
    ComponentVersion Read(string path);
}

public sealed class FileVersionReader : IFileVersionReader
{
    public const string ProductName = "U9GL";

    public ComponentVersion Read(string path)
    {
        if (!File.Exists(path)) return ComponentVersion.Missing;
        var info = FileVersionInfo.GetVersionInfo(path);
        bool ours = string.Equals(info.ProductName, ProductName, StringComparison.Ordinal);
        Version? version = ours ? new Version(info.FileMajorPart, info.FileMinorPart, info.FileBuildPart) : null;
        return new ComponentVersion(true, ours, version, info.ProductVersion);
    }
}

internal static class Versions
{
    public static int Compare(Version a, Version b) =>
        (a.Major, a.Minor, Math.Max(a.Build, 0)).CompareTo((b.Major, b.Minor, Math.Max(b.Build, 0)));

    public static string Text(Version v) => new Version(v.Major, v.Minor, Math.Max(v.Build, 0)).ToString(3);
}
