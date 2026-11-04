namespace U9GL.Core.Telemetry;

public enum TelemetryFieldKind { Unsigned, Signed, Flag, Enum }

public sealed record TelemetryField(string Name, string Unit, TelemetryFieldKind Kind = TelemetryFieldKind.Unsigned,
    int Scale = 1, IReadOnlyList<string>? Names = null);

public sealed record TelemetryPackedPart(string Name, int Bits);

public sealed record TelemetryArraySchema(string Name, string CountField, int Capacity, IReadOnlyList<TelemetryPackedPart> Parts);

public sealed record TelemetryGroupSchema(ushort Id, string Name, ushort Version, IReadOnlyList<TelemetryField> Fields, int StaleMilliseconds)
{
    public TelemetryArraySchema? Array { get; init; }
    public uint RequestBit => 1u << Id;
    public int MinimumBytes => 8 + 8 * Fields.Count;
    public int IndexOf(string field)
    {
        for (int i = 0; i < Fields.Count; i++) if (Fields[i].Name == field) return i;
        return -1;
    }
}

public static class TelemetrySchema
{
    private const string Count = "count", Bytes = "bytes";
    private const int TickStaleMilliseconds = 3_000;
    private const int MemoryStaleMilliseconds = 10_000;
    private const int SampledStaleMilliseconds = 3_000;
    private const int AreaStaleMilliseconds = 12_000;

    private static TelemetryField F(string name, string unit = Count) => new(name, unit);

    public static readonly TelemetryGroupSchema Frames = new(1, "frames", 1,
        [F("frameNo"), F("presents"), F("presentFailed"), F("drawCallsLast"), F("verticesLast")], TelemetryContract.StaleMilliseconds);
    public static readonly TelemetryGroupSchema Resources = new(2, "resources", 1,
        [F("texturesAlive"), F("textureBytes", Bytes), F("texturesCreated"), F("texturesDestroyed")], TelemetryContract.StaleMilliseconds);
    public static readonly TelemetryGroupSchema Memory = new(3, "memory", 1,
        [F("privateBytes", Bytes), F("workingSet", Bytes), F("peakWorkingSet", Bytes), F("pagefile", Bytes)], MemoryStaleMilliseconds);
    public static readonly TelemetryGroupSchema Pacing = new(4, "pacing", 1,
        [new("fpsMilli", "fps", Scale: 1000)], TickStaleMilliseconds);
    public static readonly TelemetryGroupSchema Renderer = new(5, "renderer", 1,
        [
            new("mipMode", "", TelemetryFieldKind.Enum, Names: ["off", "nearest", "trilinear"]),
            new("anisotropyMilli", "x", Scale: 1000),
            new("depthMode", "", TelemetryFieldKind.Enum, Names: ["1/z", "linear-w", "w-buffer"]),
            F("buffersAlive"), F("bufferBytes", Bytes), F("fbosAlive"),
        ], TelemetryContract.StaleMilliseconds);
    public static readonly TelemetryGroupSchema World = new(6, "world", 1,
        [
            new("mapValid", "", TelemetryFieldKind.Flag), F("map", ""),
            new("avatarValid", "", TelemetryFieldKind.Flag),
            new("avatarX", "units", TelemetryFieldKind.Signed), new("avatarY", "units", TelemetryFieldKind.Signed),
            new("avatarZ", "units", TelemetryFieldKind.Signed),
        ], TelemetryContract.StaleMilliseconds);
    public static readonly TelemetryGroupSchema TextureCache = new(7, "textureCache", 1,
        [
            F("cached"), F("cacheBytes", Bytes), F("budgetBytes", Bytes), F("pending"),
            F("uploads"), F("duplicates"), F("evictions"),
            F("uploadsPerSec", "per second"), F("evictionsPerSec", "per second"), new("uploadMaxUs", "ms", Scale: 1000),
        ], TickStaleMilliseconds);
    public static readonly TelemetryGroupSchema Overrides = new(8, "overrides", 1,
        [new("enabled", "", TelemetryFieldKind.Flag), F("files"), F("used"), F("rejected"), new("loadMaxUs", "ms", Scale: 1000)],
        TickStaleMilliseconds);
    public static readonly TelemetryGroupSchema MemoryTrend = new(9, "memoryTrend", 1,
        [
            new("floorSlopeMilli", "bytes/frame", TelemetryFieldKind.Signed, 1000),
            new("rawSlopeMilli", "bytes/frame", TelemetryFieldKind.Signed, 1000),
            F("spanFrames", "frames"), F("windowSamples"), F("windowCapacity"),
            new("floorRising", "", TelemetryFieldKind.Flag),
        ], MemoryStaleMilliseconds);

    public static readonly TelemetryGroupSchema Area = new(10, "area", 1,
        [F("gridW"), F("gridH"), F("cellsDrawn"), F("cellsActive"), F("cellsCollide"), F("cellsQueued"), F("cellsWithSlots"), F("slotBytes", Bytes), F("lruCells"), F("ringNear"), F("ringMid"), F("ringFar"), F("terrainMeshes"), F("terrainMeshMax"), F("terrainA"), F("terrainB"), F("queueSorting"), F("queueWaiting"), F("queueReady"), F("pendingMeshes"), F("pendingInstances"), F("objects"), F("worstCell"), F("worstCellCapacity"), F("drawn"), F("moving"), F("fixed"), F("nonfixed"), F("unresolved"), F("uniqueTypes"), F("topType1", ""), F("topCount1"), F("topType2", ""), F("topCount2"), F("topType3", ""), F("topCount3"), F("inCamera"), F("meshTypes"), F("meshInUse"), F("meshIdle"), F("meshUsers"), F("meshMaxUsers"), F("meshMaxAppearance", ""), F("meshBytesPool", Bytes), F("meshBytesOutside", Bytes), F("poolSize", Bytes), F("poolLargestFree", Bytes), F("engineMeshTypes"), F("engineMeshBytes", Bytes), F("idleKeep"), F("fixedUsed", Bytes), F("fixedMax", Bytes), F("nonfixedUsed", Bytes), F("nonfixedMax", Bytes), new("sampleUs", "ms", Scale: 1000)], AreaStaleMilliseconds);
    public static readonly TelemetryGroupSchema AddressSpace = new(11, "addressSpace", 1,
        [F("freeBytes", Bytes), F("largestFreeBytes", Bytes), new("walkUs", "ms", Scale: 1000)], SampledStaleMilliseconds);
    public static readonly TelemetryGroupSchema Vram = new(12, "vram", 2,
        [new("source", "", TelemetryFieldKind.Enum, Names: ["not reported", "DXGI"]), F("dedicatedBytes", Bytes),
         F("localUsageBytes", Bytes), F("localBudgetBytes", Bytes), F("sharedUsageBytes", Bytes), F("sharedBudgetBytes", Bytes),
         new("queryUs", "ms", Scale: 1000)], SampledStaleMilliseconds);
    public static readonly TelemetryGroupSchema OverrideResident = new(13, "overrideResident", 1,
        [F("count"), F("bytes", Bytes)], SampledStaleMilliseconds);

    public static readonly TelemetryGroupSchema Gpu = new(14, "gpu", 1,
        [F("frameNo", ""), F("samples"), F("skipped"), new("support", "", TelemetryFieldKind.Enum, Names: ["not checked", "timer queries", "none"]), new("frameUs", "ms", Scale: 1000), F("regionMask", ""), new("sceneUs", "ms", Scale: 1000), new("beamUs", "ms", Scale: 1000), new("raysUs", "ms", Scale: 1000), new("bloomUs", "ms", Scale: 1000), new("aoUs", "ms", Scale: 1000), new("poolShadowsUs", "ms", Scale: 1000), new("sunShadowsUs", "ms", Scale: 1000), new("waterUs", "ms", Scale: 1000), new("compositeUs", "ms", Scale: 1000)], SampledStaleMilliseconds);

    public static readonly TelemetryGroupSchema AreaCells = new(15, "areaCells", 1,
        [F("gridW"), F("gridH"), new("avatarValid", "", TelemetryFieldKind.Flag), F("avatarX", ""), F("avatarY", ""),
         F("slotCapacity"), F("cellsOutside"), F("count")], AreaStaleMilliseconds)
    {
        Array = new("cells", "count", 256,
            [new("x", 8), new("y", 8), new("objects", 16), new("drawn", 12), new("moving", 12), new("flags", 8)]),
    };

    public static readonly TelemetryGroupSchema TextureList = new(16, "textureList", 1,
        [F("cacheEntries"), F("count")], AreaStaleMilliseconds)
    {
        Array = new("textures", "count", 256,
            [new("id", 16), new("frame", 8), new("width", 12), new("height", 12), new("age", 12), new("overridden", 1), new("keyed", 1)]),
    };
    public static readonly TelemetryGroupSchema MeshList = new(17, "meshList", 1,
        [F("meshTypes"), F("count")], AreaStaleMilliseconds)
    {
        Array = new("meshes", "count", 256, [new("appearance", 16), new("handles", 16), new("bytes", 28), new("inPool", 1)]),
    };

    public static IReadOnlyList<TelemetryGroupSchema> Groups { get; } =
        [Frames, Resources, Memory, Pacing, Renderer, World, TextureCache, Overrides, MemoryTrend, Area, AddressSpace, Vram, OverrideResident, Gpu, AreaCells, TextureList, MeshList];

    public static uint AllRequestBits { get; } = Groups.Aggregate(0u, (mask, g) => mask | g.RequestBit);

    public static TelemetryGroupSchema? Find(ushort id, ushort version)
    {
        foreach (var group in Groups) if (group.Id == id && group.Version == version) return group;
        return null;
    }

    public static TelemetryGroupSchema? Find(string name)
    {
        foreach (var group in Groups) if (group.Name == name) return group;
        return null;
    }
}
