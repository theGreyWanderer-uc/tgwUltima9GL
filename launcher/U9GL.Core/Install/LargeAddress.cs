namespace U9GL.Core.Install;

public static class LargeAddress
{
    public const string CopyName = "u9-laa.exe";

    private const ushort Flag = 0x0020;

    public static string CopyPath(GameFolder folder) => Path.Combine(folder.Root, CopyName);

    private static int CharacteristicsOffset(byte[] image)
    {
        if (image.Length < 0x40 || image[0] != (byte)'M' || image[1] != (byte)'Z') return -1;
        int pe = BitConverter.ToInt32(image, 0x3C);
        if (pe < 0 || pe + 24 > image.Length) return -1;
        if (image[pe] != (byte)'P' || image[pe + 1] != (byte)'E' || image[pe + 2] != 0 || image[pe + 3] != 0) return -1;
        return pe + 4 + 18;
    }

    public static bool IsSet(string exe)
    {
        byte[] image = File.ReadAllBytes(exe);
        int at = CharacteristicsOffset(image);
        return at >= 0 && (BitConverter.ToUInt16(image, at) & Flag) != 0;
    }

    public static byte[] Flagged(byte[] image)
    {
        int at = CharacteristicsOffset(image);
        if (at < 0) throw new InvalidDataException("u9.exe is not a PE image.");
        var copy = (byte[])image.Clone();
        ushort characteristics = (ushort)(BitConverter.ToUInt16(copy, at) | Flag);
        copy[at] = (byte)characteristics;
        copy[at + 1] = (byte)(characteristics >> 8);
        return copy;
    }

    public static bool EnsureCopy(GameFolder folder)
    {
        byte[] wanted = Flagged(File.ReadAllBytes(folder.Exe));
        string path = CopyPath(folder);
        if (File.Exists(path) && File.ReadAllBytes(path).AsSpan().SequenceEqual(wanted)) return false;
        SafeFile.WriteAllBytes(path, wanted);
        return true;
    }

    public sealed record CopyState(bool Exists, bool MatchesExe, string? Sha256);

    public static CopyState Check(GameFolder folder)
    {
        string path = CopyPath(folder);
        if (!File.Exists(path)) return new CopyState(false, false, null);
        byte[] copy = File.ReadAllBytes(path);
        string sha = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(copy));
        bool matches;
        try
        {
            matches = File.Exists(folder.Exe) && copy.AsSpan().SequenceEqual(Flagged(File.ReadAllBytes(folder.Exe)));
        }
        catch (InvalidDataException)
        {
            matches = false;
        }
        return new CopyState(true, matches, sha);
    }

    public static bool RemoveCopy(GameFolder folder)
    {
        string path = CopyPath(folder);
        if (!File.Exists(path)) return false;
        File.Delete(path);
        return true;
    }
}
