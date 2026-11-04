using System.Text;

namespace U9GL.Core.Saves;

public sealed class U9SaveException(string message) : Exception(message);

public sealed record U9StartDat(int Slot)
{
    public const int Size = 10;
    private static readonly byte[] Magic = "U9.008"u8.ToArray();

    public static U9StartDat FromBytes(ReadOnlySpan<byte> data)
    {
        if (data.Length != Size)
            throw new U9SaveException($"start.dat must be exactly 10 bytes, got {data.Length}");
        if (!data[..6].SequenceEqual(Magic))
            throw new U9SaveException($"invalid start.dat magic {U9Repr.Bytes(data[..6])}");
        int slot = U9Bytes.I32(data, 6);
        if (slot < 0)
            throw new U9SaveException($"invalid negative save slot {slot}");
        return new U9StartDat(slot);
    }

    public static U9StartDat FromFile(string path) => FromBytes(File.ReadAllBytes(path));
}

public sealed record U9SaveHeader(
    string Label,
    string Description,
    IReadOnlyList<float> Position,
    IReadOnlyList<float> Orientation,
    float Yaw,
    float Pitch,
    float Roll,
    int HeaderMap,
    int Time,
    int Day);

public sealed record U9SaveMember(string Name, ReadOnlyMemory<byte> Data, int Offset, int? MapNumber = null);

public sealed class U9SaveArchive
{
    public const int MaxMapNumber = 255;
    public const int MaxShippedMapNumber = 239;
    private const int MetadataSize = 52;
    private static readonly byte[] Magic = "U9:008"u8.ToArray();

    private U9SaveArchive(U9SaveHeader header, U9SaveMember screenshot, U9SaveMember processes,
                          U9SaveMember diary, IReadOnlyList<U9SaveMember> nonfixed)
    {
        Header = header;
        Screenshot = screenshot;
        Processes = processes;
        Diary = diary;
        Nonfixed = nonfixed;
    }

    public U9SaveHeader Header { get; }
    public U9SaveMember Screenshot { get; }
    public U9SaveMember Processes { get; }
    public U9SaveMember Diary { get; }
    public IReadOnlyList<U9SaveMember> Nonfixed { get; }

    public static U9SaveArchive FromFile(string path) => FromBytes(File.ReadAllBytes(path));

    public static U9SaveArchive FromBytes(byte[] data)
    {
        var reader = new Reader(data);
        var header = reader.Header();
        var screenshot = reader.ReadMember("screenshot");
        var processes = reader.ReadMember("processes.dat");
        var diary = reader.ReadMember("diary.txt");

        var nonfixed = new List<U9SaveMember>();
        var seen = new HashSet<int>();
        while (true)
        {
            int map = reader.ReadI32("nonfixed map number");
            if (map == -1) break;
            if (map is < 0 or > MaxMapNumber)
                throw new U9SaveException($"invalid nonfixed map number {map}");
            if (!seen.Add(map))
                throw new U9SaveException($"duplicate nonfixed map number {map}");
            string name = $"nonfixed.{map}";
            var (offset, length) = reader.ReadBytes(reader.ReadI32($"{name} length"), name);
            nonfixed.Add(new U9SaveMember(name, data.AsMemory(offset, length), offset, map));
        }
        if (reader.Cursor != data.Length)
            throw new U9SaveException($"{data.Length - reader.Cursor} trailing bytes after map terminator");
        return new U9SaveArchive(header, screenshot, processes, diary, nonfixed);
    }

    public static U9SaveHeader ReadHeader(byte[] prefix) => new Reader(prefix).Header();

    public U9SaveMember? Member(string name)
    {
        string lowered = name.ToLowerInvariant();
        if (lowered == "processes.dat") return Processes;
        if (lowered == "diary.txt") return Diary;
        return Nonfixed.FirstOrDefault(m => m.Name == lowered);
    }

    private sealed class Reader(byte[] data)
    {
        public int Cursor { get; private set; }

        public U9SaveHeader Header()
        {
            if (data.Length < 6 || !data.AsSpan(0, 6).SequenceEqual(Magic))
                throw new U9SaveException($"invalid save archive magic {U9Repr.Bytes(data.AsSpan(0, Math.Min(6, data.Length)))}");
            Cursor = 6;
            string label = ReadString("label");
            string description = ReadString("description");
            var (m, _) = ReadBytes(MetadataSize, "save metadata");
            float F(int i) => U9Bytes.F32(data, m + i * 4);
            int I(int i) => U9Bytes.I32(data, m + i * 4);
            return new U9SaveHeader(label, description,
                [F(0), F(1), F(2)], [F(3), F(4), F(5), F(6)], F(7), F(8), F(9), I(10), I(11), I(12));
        }

        public U9SaveMember ReadMember(string name)
        {
            var (offset, length) = ReadBytes(ReadI32($"{name} length"), name);
            return new U9SaveMember(name, data.AsMemory(offset, length), offset);
        }

        public int ReadI32(string label)
        {
            if (Cursor + 4 > data.Length)
                throw new U9SaveException($"truncated before {label}");
            int value = U9Bytes.I32(data, Cursor);
            Cursor += 4;
            return value;
        }

        public (int Offset, int Length) ReadBytes(int length, string label)
        {
            if (length < 0)
                throw new U9SaveException($"negative {label} length {length}");
            long end = (long)Cursor + length;
            if (end > data.Length)
                throw new U9SaveException($"{label} extends beyond EOF ({Cursor}+{length}>{data.Length})");
            int offset = Cursor;
            Cursor = (int)end;
            return (offset, length);
        }

        private string ReadString(string label)
        {
            var (offset, length) = ReadBytes(ReadI32($"{label} length"), label);
            if (length == 0) return "";
            if (data[offset + length - 1] != 0)
                throw new U9SaveException($"{label} is not NUL-terminated");
            try
            {
                return U9Bytes.Windows1252.GetString(data, offset, length - 1);
            }
            catch (DecoderFallbackException)
            {
                throw new U9SaveException($"{label} is not valid text");
            }
        }
    }
}

internal static class U9Repr
{
    public static string Bytes(ReadOnlySpan<byte> data)
    {
        var text = new StringBuilder("b'");
        foreach (byte b in data)
        {
            if (b == (byte)'\\' || b == (byte)'\'') text.Append('\\').Append((char)b);
            else if (b is >= 0x20 and < 0x7F) text.Append((char)b);
            else text.Append($"\\x{b:x2}");
        }
        return text.Append('\'').ToString();
    }
}
