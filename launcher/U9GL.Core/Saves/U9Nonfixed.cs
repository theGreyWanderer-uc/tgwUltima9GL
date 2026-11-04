namespace U9GL.Core.Saves;

public sealed class U9NonfixedException(string message) : Exception(message);

public sealed record U9Entity(long Offset, uint NextEntity, ushort OffsetX, ushort OffsetY, ushort Z, ushort TypeIndex,
                              IReadOnlyList<short> Rotation, uint Flags, ushort MeshIndex, ushort TriggerId,
                              uint ExtraDataOffset, uint BaseX, uint BaseY)
{
    public long WorldX => BaseX + OffsetX;
    public long WorldY => BaseY + OffsetY;
}

public sealed record U9ExtraData(long Offset, byte ArgCount);

public sealed record U9NonfixedPage(long Offset, uint NextPage, uint FreeEntityHead, uint FreeExtraDataHead,
                                    uint BaseX, uint BaseY, uint EntityCount, uint ExtraDataCount,
                                    IReadOnlyList<uint> BucketHeads);

public sealed record U9NonfixedChunk(int Index, IReadOnlyList<U9NonfixedPage> Pages, IReadOnlyList<U9Entity> Entities,
                                     IReadOnlyList<U9Entity> AllocatedEntities, IReadOnlyList<U9ExtraData> ExtraData,
                                     bool EntitySlotsComplete, bool ExtraDataSlotsComplete)
{
    public bool AllocationIsComplete => EntitySlotsComplete && ExtraDataSlotsComplete;
}

public sealed class U9Nonfixed
{
    public const int PageSize = 0x1000;
    public const int PageHeaderSize = 0x60;
    public const int EntitySize = 0x20;
    public const int ExtraSize = 0x10;
    public const int MaxEntitiesPerPage = 125;
    private const int TableOffset = 0x20;
    private const int TrailerSize = 4;
    private const int BucketOffset = 0x1C;
    private const int BucketCount = 17;
    private const int MaxGridDim = 256;

    private readonly byte[] _data;
    private readonly uint[] _table;
    private IReadOnlyList<U9NonfixedChunk>? _chunks;

    public U9Nonfixed(byte[] data)
    {
        if (data.Length < TableOffset + TrailerSize)
            throw new U9NonfixedException($"data too small to contain a nonfixed header: {data.Length} bytes");
        Width = U9Bytes.U32(data, 0x14);
        Height = U9Bytes.U32(data, 0x18);
        if (Width is < 1 or > MaxGridDim || Height is < 1 or > MaxGridDim)
            throw new U9NonfixedException($"implausible region grid {Width}x{Height} -- not a nonfixed file?");
        HeaderSize = TableOffset + (int)(Width * Height * 4) + TrailerSize;
        if (data.Length < HeaderSize)
            throw new U9NonfixedException(
                $"truncated: {Width}x{Height} grid needs a {HeaderSize}-byte header, file is {data.Length} bytes");
        _data = data;
        _table = new uint[Width * Height];
        for (int i = 0; i < _table.Length; i++) _table[i] = U9Bytes.U32(data, TableOffset + i * 4);
        DeclaredPayloadSize = U9Bytes.U32(data, 0x0C);
    }

    public uint Width { get; }
    public uint Height { get; }
    public int HeaderSize { get; }
    public uint DeclaredPayloadSize { get; }
    public long PayloadSize => _data.Length - HeaderSize;

    public IReadOnlyList<U9NonfixedChunk> Chunks => _chunks ??= BuildChunks();

    private bool InPayload(long rel, int size) => rel >= 0 && rel + size <= PayloadSize;

    private IReadOnlyList<U9NonfixedChunk> BuildChunks()
    {
        var chunks = new List<U9NonfixedChunk>();
        for (int i = 0; i < _table.Length; i++)
            if (_table[i] != 0 && ChunkAt(i) is { } chunk) chunks.Add(chunk);
        return chunks;
    }

    private List<U9NonfixedPage> Pages(int index)
    {
        var pages = new List<U9NonfixedPage>();
        var seen = new HashSet<long>();
        uint value = _table[index];
        while (value != 0)
        {
            long rel = (long)value - 1;
            if (!seen.Add(rel) || !InPayload(rel, PageHeaderSize)) break;
            long b = HeaderSize + rel;
            var heads = new uint[BucketCount];
            for (int i = 0; i < BucketCount; i++) heads[i] = U9Bytes.U32(_data, b + BucketOffset + i * 4);
            var page = new U9NonfixedPage(rel, U9Bytes.U32(_data, b), U9Bytes.U32(_data, b + 4), U9Bytes.U32(_data, b + 8),
                U9Bytes.U32(_data, b + 12), U9Bytes.U32(_data, b + 16), U9Bytes.U32(_data, b + 20), U9Bytes.U32(_data, b + 24), heads);
            pages.Add(page);
            value = page.NextPage;
        }
        return pages;
    }

    private U9Entity ReadEntity(long rel, uint baseX, uint baseY)
    {
        long b = HeaderSize + rel;
        return new U9Entity(rel, U9Bytes.U32(_data, b), U9Bytes.U16(_data, b + 4), U9Bytes.U16(_data, b + 6),
            U9Bytes.U16(_data, b + 8), U9Bytes.U16(_data, b + 10),
            [U9Bytes.I16(_data, b + 12), U9Bytes.I16(_data, b + 14), U9Bytes.I16(_data, b + 16), U9Bytes.I16(_data, b + 18)],
            U9Bytes.U32(_data, b + 20), U9Bytes.U16(_data, b + 24), U9Bytes.U16(_data, b + 26), U9Bytes.U32(_data, b + 28),
            baseX, baseY);
    }

    private U9ExtraData ReadExtraData(long rel) => new(rel, _data[HeaderSize + rel]);

    private bool LooksLikeExtraData(long rel)
    {
        if (!InPayload(rel, ExtraSize)) return false;
        long b = HeaderSize + rel;
        byte count = _data[b];
        if (count is < 1 or > 3) return false;
        for (int i = count; i < 3; i++)
            if (_data[b + 1 + i] != 0) return false;
        return true;
    }

    private (List<long> Offsets, bool Valid) WalkFreeList(U9NonfixedPage page, uint head, int size, int linkOffset)
    {
        var offsets = new List<long>();
        var seen = new HashSet<long>();
        long rel = head;
        long pageStart = page.Offset + PageHeaderSize, pageEnd = page.Offset + PageSize;
        while (rel != 0)
        {
            if (seen.Contains(rel) || rel < pageStart || rel + size > pageEnd || (rel - pageStart) % size != 0 || !InPayload(rel, size))
                return (offsets, false);
            seen.Add(rel);
            offsets.Add(rel);
            rel = U9Bytes.U32(_data, HeaderSize + rel + linkOffset);
        }
        return (offsets, true);
    }

    private (List<U9Entity> Entities, List<U9ExtraData> Extras, List<long> FreeEntities, List<long> FreeExtras, bool Valid)
        PageAllocations(U9NonfixedPage page, uint baseX, uint baseY)
    {
        var (freeEntities, entityValid) = WalkFreeList(page, page.FreeEntityHead, EntitySize, 0);
        var (freeExtras, extraValid) = WalkFreeList(page, page.FreeExtraDataHead, ExtraSize, 4);
        if (!InPayload(page.Offset, PageSize))
            return ([], [], freeEntities, freeExtras, false);

        var freeEntitySet = freeEntities.ToHashSet();
        var freeExtraSet = freeExtras.ToHashSet();
        var entities = new List<U9Entity>();
        var extras = new List<U9ExtraData>();
        for (long rel = page.Offset + PageHeaderSize; rel < page.Offset + PageSize; rel += EntitySize)
        {
            if (freeEntitySet.Contains(rel)) continue;
            long second = rel + ExtraSize;
            bool firstExtra = freeExtraSet.Contains(rel) || LooksLikeExtraData(rel);
            bool secondExtra = freeExtraSet.Contains(second) || LooksLikeExtraData(second);
            if (firstExtra && secondExtra)
            {
                if (!freeExtraSet.Contains(rel)) extras.Add(ReadExtraData(rel));
                if (!freeExtraSet.Contains(second)) extras.Add(ReadExtraData(second));
            }
            else
            {
                entities.Add(ReadEntity(rel, baseX, baseY));
            }
        }
        bool valid = entityValid && extraValid
            && entities.Count == page.EntityCount
            && extras.Count == page.ExtraDataCount
            && 2 * entities.Count + extras.Count + 2 * freeEntities.Count + freeExtras.Count == 250;
        return (entities, extras, freeEntities, freeExtras, valid);
    }

    private U9NonfixedChunk? ChunkAt(int index)
    {
        var pages = Pages(index);
        if (pages.Count == 0) return null;
        uint baseX = pages[0].BaseX, baseY = pages[0].BaseY;

        var indexed = new List<U9Entity>();
        var seen = new HashSet<long>();
        foreach (uint head in pages[0].BucketHeads)
        {
            long rel = head;
            var local = new HashSet<long>();
            while (rel != 0 && !seen.Contains(rel) && !local.Contains(rel))
            {
                if (!InPayload(rel, EntitySize)) break;
                local.Add(rel);
                seen.Add(rel);
                var entity = ReadEntity(rel, baseX, baseY);
                indexed.Add(entity);
                rel = entity.NextEntity;
            }
        }
        indexed.Sort((a, b) => a.Offset.CompareTo(b.Offset));

        var allocated = new Dictionary<long, U9Entity>();
        var extraByOffset = new Dictionary<long, U9ExtraData>();
        var pageIndexed = pages.ToDictionary(p => p.Offset, _ => new List<U9Entity>());
        foreach (var entity in indexed)
        {
            long pageOffset = entity.Offset / PageSize * PageSize;
            if (pageIndexed.TryGetValue(pageOffset, out var list)) list.Add(entity);
        }

        var allocatorPages = new HashSet<long>();
        foreach (var page in pages)
        {
            var (entities, extras, _, _, valid) = PageAllocations(page, baseX, baseY);
            var scanned = entities.Select(e => e.Offset).ToHashSet();
            if (valid && pageIndexed[page.Offset].All(e => scanned.Contains(e.Offset)))
            {
                allocatorPages.Add(page.Offset);
                foreach (var e in entities) allocated[e.Offset] = e;
                foreach (var x in extras) extraByOffset[x.Offset] = x;
            }
            else
            {
                foreach (var e in pageIndexed[page.Offset]) allocated[e.Offset] = e;
            }
        }
        foreach (var entity in indexed) allocated.TryAdd(entity.Offset, entity);

        long EffectiveCount(Func<U9NonfixedPage, uint> field, long knownLegacy)
        {
            long exact = pages.Where(p => allocatorPages.Contains(p.Offset)).Sum(p => (long)field(p));
            var legacy = pages.Where(p => !allocatorPages.Contains(p.Offset)).Select(p => (long)field(p)).ToList();
            if (legacy.Count == 0) return exact;
            long largest = legacy.Max();
            return exact + (largest >= knownLegacy ? largest : legacy.Sum());
        }

        long knownLegacyEntities = pages.Where(p => !allocatorPages.Contains(p.Offset)).Sum(p => (long)pageIndexed[p.Offset].Count);
        bool entityComplete = allocated.Count == EffectiveCount(p => p.EntityCount, knownLegacyEntities);
        var allocatedSorted = allocated.OrderBy(kv => kv.Key).Select(kv => kv.Value).ToList();

        foreach (var entity in allocated.Values)
        {
            long rel = entity.ExtraDataOffset;
            if (rel != 0 && LooksLikeExtraData(rel)) extraByOffset.TryAdd(rel, ReadExtraData(rel));
        }
        long knownLegacyExtras = extraByOffset.Values.Count(x => !allocatorPages.Contains(x.Offset / PageSize * PageSize));
        bool extraComplete = extraByOffset.Count == EffectiveCount(p => p.ExtraDataCount, knownLegacyExtras);

        return new U9NonfixedChunk(index, pages, indexed, allocatedSorted,
            extraByOffset.OrderBy(kv => kv.Key).Select(kv => kv.Value).ToList(), entityComplete, extraComplete);
    }

    public U9Entity? EntityRecordAt(long offset)
    {
        long pageOffset = offset & ~(long)(PageSize - 1);
        long delta = offset - pageOffset - PageHeaderSize;
        if (delta < 0 || delta % EntitySize != 0 || delta / EntitySize >= MaxEntitiesPerPage || !InPayload(offset, EntitySize))
            return null;
        foreach (var chunk in Chunks)
            foreach (var page in chunk.Pages)
                if (page.Offset == pageOffset)
                    return ReadEntity(offset, page.BaseX, page.BaseY);
        return null;
    }
}
