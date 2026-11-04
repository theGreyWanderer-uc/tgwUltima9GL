namespace U9GL.Core.Telemetry;

public enum TelemetryProducerKind : uint { Game = 1, Synthetic = 2 }

public sealed class TelemetryGroup(TelemetryGroupSchema schema, ulong collectedQpc, IReadOnlyList<ulong> values,
    IReadOnlyList<ulong>? array = null)
{
    public IReadOnlyList<ulong> Array { get; } = array ?? [];
    public TelemetryGroupSchema Schema { get; } = schema;
    public ulong CollectedQpc { get; } = collectedQpc;
    public IReadOnlyList<ulong> Values { get; } = values;
    public ulong this[string field] => Values[Schema.IndexOf(field) is >= 0 and var i ? i
        : throw new KeyNotFoundException($"{Schema.Name} has no field {field}.")];
}

public sealed record TelemetrySnapshot(uint ProducerPid, ulong ProcessCreatedFiletime, string SessionId,
    ulong QpcFrequency, ulong SessionStartQpc, ulong SampleQpc, ulong? SampleUtcFiletime, ulong SampleIndex,
    TelemetryProducerKind ProducerKind, string ProducerVersion, ushort Minor, uint RequestedGroups,
    IReadOnlyList<TelemetryGroup> Groups, int SkippedGroups)
{
    public TelemetryGroup? Group(TelemetryGroupSchema schema)
    {
        foreach (var group in Groups) if (group.Schema == schema) return group;
        return null;
    }

    public TelemetryGroup? Group(string name)
    {
        foreach (var group in Groups) if (group.Schema.Name == name) return group;
        return null;
    }
}

public enum TelemetryReadStatus { Accepted, Uninitialized, Busy, Changed, Incompatible, Invalid, ProcessExited }

public sealed record TelemetryDecodeResult(TelemetryReadStatus Status, TelemetrySnapshot? Snapshot, string? Reason = null);
