namespace U9GL.Core.Saves;

public sealed class U9ProcessDataException(string message) : Exception(message);

public sealed record U9ItemHandleEntry(int Index, int UsageCount, int MapNumber, int EncodedItemOffset)
{
    public bool IsFree => MapNumber == -1;
    public bool IsLive => MapNumber >= 0 && EncodedItemOffset != 0;
    public bool IsFixed => IsLive && EncodedItemOffset < 0;

    public int ItemOffset => Math.Abs(EncodedItemOffset);
}

public sealed class U9ItemHandleTable
{
    public const int HandleDataOffset = 0x27F74;
    private const uint HandleVersion = 1;
    private const uint CameraVersion = 2;
    private const int RecordSize = 12;
    private const uint MinCount = 2;
    private const uint MaxCount = 0x10000;

    private U9ItemHandleTable(uint version, int count, int freeHead, IReadOnlyList<U9ItemHandleEntry> entries)
    {
        Version = version;
        Count = count;
        FreeHead = freeHead;
        Entries = entries;
        LiveEntries = entries.Where(e => e.IsLive).ToList();
        FixedEntries = LiveEntries.Where(e => e.IsFixed).ToList();
        NonfixedEntries = LiveEntries.Where(e => !e.IsFixed).ToList();
    }

    public uint Version { get; }
    public int Count { get; }
    public int FreeHead { get; }
    public int Offset => HandleDataOffset;
    public int EndOffset => HandleDataOffset + 12 + Count * RecordSize;
    public IReadOnlyList<U9ItemHandleEntry> Entries { get; }
    public IReadOnlyList<U9ItemHandleEntry> LiveEntries { get; }
    public IReadOnlyList<U9ItemHandleEntry> FixedEntries { get; }
    public IReadOnlyList<U9ItemHandleEntry> NonfixedEntries { get; }

    public static U9ItemHandleTable FromBytes(ReadOnlySpan<byte> data)
    {
        if (data.Length < HandleDataOffset + 12)
            throw new U9ProcessDataException($"process data is too short for item handle header at 0x{HandleDataOffset:X}");
        uint mainVersion = U9Bytes.U32(data, 0), stateVersion = U9Bytes.U32(data, 4);
        if (mainVersion != 8 || stateVersion != 2)
            throw new U9ProcessDataException($"unsupported process versions {mainVersion}/{stateVersion}; expected 8/2");
        uint version = U9Bytes.U32(data, HandleDataOffset);
        uint count = U9Bytes.U32(data, HandleDataOffset + 4);
        uint freeHead = U9Bytes.U32(data, HandleDataOffset + 8);
        if (version != HandleVersion)
            throw new U9ProcessDataException($"unsupported item handle version {version}; expected {HandleVersion}");
        if (count is < MinCount or > MaxCount)
            throw new U9ProcessDataException($"implausible item handle count {count}");
        long tableEnd = HandleDataOffset + 12 + (long)count * RecordSize;
        if (tableEnd + 4 > data.Length)
            throw new U9ProcessDataException($"truncated item handle table: needs {tableEnd + 4} bytes, got {data.Length}");
        uint cameraVersion = U9Bytes.U32(data, tableEnd);
        if (cameraVersion != CameraVersion)
            throw new U9ProcessDataException(
                $"item handle table does not end at camera version {CameraVersion} (found {cameraVersion})");

        var entries = new U9ItemHandleEntry[count];
        int invalidMaps = 0;
        for (int i = 0; i < count; i++)
        {
            long at = HandleDataOffset + 12 + (long)i * RecordSize;
            entries[i] = new U9ItemHandleEntry(i, U9Bytes.I32(data, at), U9Bytes.I32(data, at + 4), U9Bytes.I32(data, at + 8));
            if (entries[i].MapNumber < -1) invalidMaps++;
            if (entries[i].EncodedItemOffset == int.MinValue)
                throw new U9ProcessDataException($"item handle {i} has an item offset with no absolute value");
        }
        if (invalidMaps > 0)
            throw new U9ProcessDataException($"{invalidMaps} item handle entries have map numbers below -1");
        if (freeHead >= count)
            throw new U9ProcessDataException($"item handle free head {freeHead} is outside 0..{count - 1}");
        return new U9ItemHandleTable(version, (int)count, (int)freeHead, entries);
    }

    public IReadOnlyList<int> WalkFreeChain()
    {
        var chain = new List<int>();
        var seen = new HashSet<int>();
        int current = FreeHead;
        while (current != 0)
        {
            if (current < 0 || current >= Count)
                throw new U9ProcessDataException($"free chain leaves table at handle {current}");
            if (!seen.Add(current))
                throw new U9ProcessDataException($"free chain cycles at handle {current}");
            var entry = Entries[current];
            if (!entry.IsFree)
                throw new U9ProcessDataException($"free chain enters live handle {current}");
            chain.Add(current);
            current = entry.UsageCount;
        }
        int expected = Entries.Skip(1).Count(e => e.IsFree);
        if (seen.Count != expected || Entries.Skip(1).Any(e => e.IsFree && !seen.Contains(e.Index)))
            throw new U9ProcessDataException($"free chain covers {seen.Count} of {expected} free handles");
        return chain;
    }
}
