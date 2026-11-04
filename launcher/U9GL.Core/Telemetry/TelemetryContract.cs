using System.Runtime.InteropServices;
using System.Text;

namespace U9GL.Core.Telemetry;

public static class TelemetryContract
{
    public const uint Magic = 0x4D543955, RequestMagic = 0x52543955;
    public const ushort Major = 2;
    public const int HeaderBytes = 160, EntryBytes = 16, MaxGroups = 32, MappingBytes = 65536;
    public const int SequenceOffset = 16, SizeOffset = 12, RequestBytes = 64, RequestExpiryMilliseconds = 5000;
    public const int ReadAttempts = 3, ReadBudgetMilliseconds = 20, PublishHz = 10, StaleMilliseconds = 2000;
    private const uint AvailableFlag = 1;

    public static string MappingName(uint processId, ulong processCreatedFiletime) =>
        Name("Telemetry.v2", processId, processCreatedFiletime);

    public static string RequestMappingName(uint processId, ulong processCreatedFiletime) =>
        Name("TelemetryRequest.v2", processId, processCreatedFiletime);

    public static string LegacyMappingName(uint processId, ulong processCreatedFiletime) =>
        Name("Telemetry.v1", processId, processCreatedFiletime);

    private static string Name(string kind, uint processId, ulong processCreatedFiletime)
    {
        ArgumentOutOfRangeException.ThrowIfZero(processId);
        ArgumentOutOfRangeException.ThrowIfZero(processCreatedFiletime);
        return FormattableString.Invariant($@"Local\U9GL.{kind}.{processId}.{processCreatedFiletime:x16}");
    }

    public static TelemetryDecodeResult Decode(ReadOnlySpan<byte> bytes, uint sequenceBefore, uint sequenceAfter)
    {
        static TelemetryDecodeResult Reject(TelemetryReadStatus status, string reason) => new(status, null, reason);
        if ((sequenceBefore & 1) != 0) return Reject(TelemetryReadStatus.Busy, "Publisher was writing.");
        if (sequenceBefore != sequenceAfter) return Reject(TelemetryReadStatus.Changed, "Sequence changed during copy.");
        if (!BitConverter.IsLittleEndian || bytes.Length < HeaderBytes || bytes.Length > MappingBytes)
            return Reject(TelemetryReadStatus.Incompatible, "Expected a little-endian record of 160 to 65536 bytes.");
        var wire = MemoryMarshal.Read<TelemetryWireHeader>(bytes);
        if (wire.Sequence != sequenceBefore) return Reject(TelemetryReadStatus.Changed, "Copied sequence differs from observations.");
        if (AllZero(bytes)) return Reject(TelemetryReadStatus.Uninitialized, "No snapshot has been published.");
        if (wire.Magic != Magic || wire.Major != Major || wire.HeaderBytes != HeaderBytes ||
            wire.EntryBytes != EntryBytes || wire.MappingBytes != MappingBytes || wire.SnapshotBytes != bytes.Length)
            return Reject(TelemetryReadStatus.Incompatible, "Unknown telemetry format, version or size.");

        var version = bytes.Slice(96, 32);
        int terminator = version.IndexOf((byte)0);
        if (wire.ProducerPid == 0 || wire.ProcessCreatedFiletime == 0 ||
            (wire.SessionBytes0 == 0 && wire.SessionBytes1 == 0) || wire.SampleIndex == 0 ||
            wire.QpcFrequency == 0 || wire.QpcFrequency > long.MaxValue ||
            wire.SessionStartQpc == 0 || wire.SampleQpc < wire.SessionStartQpc || wire.SampleQpc > long.MaxValue ||
            wire.ProducerKind is not (1 or 2) || !AllZero(bytes.Slice(136, 24)) ||
            terminator <= 0 || !PrintableAscii(version[..terminator]) || !AllZero(version[terminator..]))
            return Reject(TelemetryReadStatus.Invalid, "Invalid identity, clock, reserved bytes or producer version.");

        int directoryEnd = HeaderBytes + wire.GroupCount * EntryBytes;
        if (wire.GroupCount > MaxGroups || directoryEnd > bytes.Length)
            return Reject(TelemetryReadStatus.Invalid, "Group directory is too large for the record.");

        var groups = new List<TelemetryGroup>(wire.GroupCount);
        int skipped = 0, previousId = 0;
        long previousEnd = directoryEnd;
        for (int i = 0; i < wire.GroupCount; i++)
        {
            var entry = MemoryMarshal.Read<TelemetryWireEntry>(bytes.Slice(HeaderBytes + i * EntryBytes, EntryBytes));
            long end = (long)entry.Offset + entry.Bytes;
            if (entry.Id == 0 || entry.Id > 31 || entry.Id <= previousId || entry.Offset % 8 != 0 ||
                entry.Bytes < 8 || entry.Bytes % 8 != 0 || entry.Offset < previousEnd || end > bytes.Length ||
                (entry.Flags & ~AvailableFlag) != 0)
                return Reject(TelemetryReadStatus.Invalid, $"Directory entry {i} is out of order, misaligned, overlapping or out of bounds.");
            previousId = entry.Id;
            previousEnd = end;
            var payload = bytes.Slice((int)entry.Offset, (int)entry.Bytes);
            var schema = TelemetrySchema.Find(entry.Id, entry.Version);
            if ((entry.Flags & AvailableFlag) == 0)
            {
                if (!AllZero(payload)) return Reject(TelemetryReadStatus.Invalid, $"Unavailable group {entry.Id} contains data.");
                if (schema == null) skipped++;
                continue;
            }
            ulong collected = MemoryMarshal.Read<ulong>(payload);
            if (collected < wire.SessionStartQpc || collected > wire.SampleQpc)
                return Reject(TelemetryReadStatus.Invalid, $"Group {entry.Id} collection time is outside this session/sample.");
            if (schema == null) { skipped++; continue; }
            if (payload.Length < schema.MinimumBytes)
                return Reject(TelemetryReadStatus.Invalid, $"Group {schema.Name} is shorter than its version's fields.");
            var values = new ulong[schema.Fields.Count];
            for (int f = 0; f < values.Length; f++) values[f] = MemoryMarshal.Read<ulong>(payload.Slice(8 + f * 8, 8));
            ulong[]? array = null;
            if (schema.Array is { } tail)
            {
                ulong count = values[schema.IndexOf(tail.CountField)];
                long room = (payload.Length - schema.MinimumBytes) / 8;
                if (count > (ulong)tail.Capacity || (long)count > room)
                    return Reject(TelemetryReadStatus.Invalid, $"Group {schema.Name} claims more {tail.Name} than it holds.");
                array = new ulong[count];
                for (int w = 0; w < array.Length; w++) array[w] = MemoryMarshal.Read<ulong>(payload.Slice(schema.MinimumBytes + w * 8, 8));
            }
            groups.Add(new TelemetryGroup(schema, collected, values, array));
        }

        var snapshot = new TelemetrySnapshot(wire.ProducerPid, wire.ProcessCreatedFiletime,
            Convert.ToHexStringLower(bytes.Slice(32, 16)), wire.QpcFrequency, wire.SessionStartQpc,
            wire.SampleQpc, wire.SampleUtcFiletime == 0 ? null : wire.SampleUtcFiletime, wire.SampleIndex,
            (TelemetryProducerKind)wire.ProducerKind, Encoding.ASCII.GetString(version[..terminator]),
            wire.Minor, wire.RequestedGroups, groups, skipped);
        return new(TelemetryReadStatus.Accepted, snapshot);
    }

    private static bool AllZero(ReadOnlySpan<byte> bytes) => !bytes.ContainsAnyExcept((byte)0);

    private static bool PrintableAscii(ReadOnlySpan<byte> bytes)
    {
        foreach (byte value in bytes) if (value is < 0x20 or > 0x7e) return false;
        return true;
    }
}
