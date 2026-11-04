namespace U9GL.Core.Saves;

public sealed class U9FixedException(string message) : Exception(message);

public sealed record U9FixedObject(long Offset, uint Reference, short X, short Y, short Z, ushort TypeIndex,
                                   IReadOnlyList<short> Rotation, uint Flags, uint BaseX, uint BaseY)
{
    public long WorldX => BaseX + X;
    public long WorldY => BaseY + Y;
}

public sealed record U9FixedPage(long Offset, uint NextPage, uint FreeListHead, uint BaseX, uint BaseY,
                                 uint LiveCount, IReadOnlyList<int> LiveSlots, bool FreeListWalked);

public sealed record U9FixedChunk(uint BaseX, uint BaseY, int TableIndex, IReadOnlyList<U9FixedPage> Pages,
                                  IReadOnlyList<U9FixedObject> Objects);

public sealed class U9Fixed
{
    public const int PageSize = 0x1000;
    public const int PageHeaderSize = 0x60;
    public const int ObjectSize = 0x18;
    public const int MaxObjectsPerPage = 166;
    private const int TableOffset = 0x1C;
    private const int MaxGridDim = 256;

    private readonly byte[] _data;
    private readonly uint[] _table;
    private IReadOnlyList<U9FixedChunk>? _chunks;

    public U9Fixed(byte[] data)
    {
        if (data.Length < TableOffset)
            throw new U9FixedException($"data too small to contain a fixed header: {data.Length} bytes");
        Width = U9Bytes.U32(data, 0x10);
        Height = U9Bytes.U32(data, 0x14);
        if (Width is < 1 or > MaxGridDim || Height is < 1 or > MaxGridDim)
            throw new U9FixedException($"implausible region grid {Width}x{Height} -- not a fixed file?");
        HeaderSize = TableOffset + (int)(Width * Height * 4) + 4;
        if (data.Length < HeaderSize)
            throw new U9FixedException(
                $"truncated: a {Width}x{Height} grid needs a {HeaderSize}-byte header, file is {data.Length} bytes");
        _data = data;
        _table = new uint[Width * Height];
        for (int i = 0; i < _table.Length; i++) _table[i] = U9Bytes.U32(data, TableOffset + i * 4);
        HeapSize = U9Bytes.U32(data, 0x08);
    }

    public uint Width { get; }
    public uint Height { get; }
    public int HeaderSize { get; }

    public uint HeapSize { get; }

    public long PayloadSize => _data.Length - HeaderSize;

    public IReadOnlyList<U9FixedChunk> Chunks => _chunks ??= BuildChunks();

    private bool InPayload(long rel, int size) => rel >= 0 && rel + size <= Math.Min(HeapSize, PayloadSize);

    private IReadOnlyList<U9FixedChunk> BuildChunks()
    {
        var chunks = new List<U9FixedChunk>();
        for (int i = 0; i < _table.Length; i++)
            if (_table[i] != 0 && ChunkAt(i) is { } chunk) chunks.Add(chunk);
        return chunks;
    }

    private U9FixedChunk? ChunkAt(int tableIndex)
    {
        var pages = Pages(tableIndex);
        if (pages.Count == 0) return null;
        uint baseX = pages[0].BaseX, baseY = pages[0].BaseY;
        var objects = new List<U9FixedObject>();
        foreach (var page in pages)
        {
            long start = page.Offset + PageHeaderSize;
            foreach (int slot in page.LiveSlots)
            {
                long rel = start + slot * ObjectSize;
                if (!InPayload(rel, ObjectSize)) break;
                objects.Add(ReadObject(rel, baseX, baseY));
            }
        }
        return new U9FixedChunk(baseX, baseY, tableIndex, pages, objects);
    }

    private List<U9FixedPage> Pages(int tableIndex)
    {
        var pages = new List<U9FixedPage>();
        var seen = new HashSet<long>();
        uint value = _table[tableIndex];
        while (value != 0)
        {
            long rel = (long)value - 1;
            if (!seen.Add(rel) || !InPayload(rel, PageHeaderSize)) break;
            long b = HeaderSize + rel;
            uint next = U9Bytes.U32(_data, b), freeHead = U9Bytes.U32(_data, b + 4);
            uint baseX = U9Bytes.U32(_data, b + 0x0C), baseY = U9Bytes.U32(_data, b + 0x10);
            uint liveCount = U9Bytes.U32(_data, b + 0x14);
            var (slots, walked) = LiveSlots(rel, freeHead, liveCount);
            pages.Add(new U9FixedPage(rel, next, freeHead, baseX, baseY, liveCount, slots, walked));
            value = next;
        }
        return pages;
    }

    private (IReadOnlyList<int> Slots, bool Walked) LiveSlots(long rel, uint freeHead, uint liveCount)
    {
        var free = FreeSlots(rel);
        if (free != null && MaxObjectsPerPage - free.Count == liveCount)
            return (Enumerable.Range(0, MaxObjectsPerPage).Where(i => !free.Contains(i)).ToList(), true);
        long span = freeHead - rel - PageHeaderSize;
        long count = freeHead != 0 && span >= 0 ? span / ObjectSize : 0;
        return (Enumerable.Range(0, (int)Math.Min(count, MaxObjectsPerPage)).ToList(), false);
    }

    private HashSet<int>? FreeSlots(long rel)
    {
        uint head = U9Bytes.U32(_data, HeaderSize + rel + 4);
        var free = new HashSet<int>();
        while (head != 0)
        {
            long offset = head - rel - PageHeaderSize;
            if (offset < 0 || offset % ObjectSize != 0) return null;
            long index = offset / ObjectSize;
            if (index >= MaxObjectsPerPage || free.Contains((int)index)) return null;
            long record = rel + PageHeaderSize + index * ObjectSize;
            if (!InPayload(record, ObjectSize)) return null;
            free.Add((int)index);
            head = U9Bytes.U32(_data, HeaderSize + record);
        }
        return free;
    }

    private U9FixedObject ReadObject(long rel, uint baseX, uint baseY)
    {
        long b = HeaderSize + rel;
        return new U9FixedObject(rel, U9Bytes.U32(_data, b),
            U9Bytes.I16(_data, b + 4), U9Bytes.I16(_data, b + 6), U9Bytes.I16(_data, b + 8), U9Bytes.U16(_data, b + 10),
            [U9Bytes.I16(_data, b + 12), U9Bytes.I16(_data, b + 14), U9Bytes.I16(_data, b + 16), U9Bytes.I16(_data, b + 18)],
            U9Bytes.U32(_data, b + 20), baseX, baseY);
    }

    public U9FixedObject? ObjectRecordAt(long offset)
    {
        long pageOffset = offset & ~(long)(PageSize - 1);
        long delta = offset - pageOffset - PageHeaderSize;
        if (delta < 0 || delta % ObjectSize != 0 || delta / ObjectSize >= MaxObjectsPerPage || !InPayload(offset, ObjectSize))
            return null;
        foreach (var chunk in Chunks)
            foreach (var page in chunk.Pages)
                if (page.Offset == pageOffset)
                    return ReadObject(offset, page.BaseX, page.BaseY);
        return null;
    }
}
