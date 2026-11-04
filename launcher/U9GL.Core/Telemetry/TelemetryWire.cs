using System.Runtime.InteropServices;

namespace U9GL.Core.Telemetry;

[StructLayout(LayoutKind.Explicit, Size = TelemetryContract.HeaderBytes)]
internal struct TelemetryWireHeader
{
    [FieldOffset(0)] public uint Magic;
    [FieldOffset(4)] public ushort Major;
    [FieldOffset(6)] public ushort Minor;
    [FieldOffset(8)] public uint HeaderBytes;
    [FieldOffset(12)] public uint SnapshotBytes;
    [FieldOffset(16)] public uint Sequence;
    [FieldOffset(20)] public uint ProducerPid;
    [FieldOffset(24)] public ulong ProcessCreatedFiletime;
    [FieldOffset(32)] public ulong SessionBytes0;
    [FieldOffset(40)] public ulong SessionBytes1;
    [FieldOffset(48)] public ulong QpcFrequency;
    [FieldOffset(56)] public ulong SessionStartQpc;
    [FieldOffset(64)] public ulong SampleQpc;
    [FieldOffset(72)] public ulong SampleUtcFiletime;
    [FieldOffset(80)] public ulong SampleIndex;
    [FieldOffset(88)] public uint ProducerKind;
    [FieldOffset(92)] public ushort GroupCount;
    [FieldOffset(94)] public ushort EntryBytes;
    [FieldOffset(96)] public ulong VersionBytes0;
    [FieldOffset(104)] public ulong VersionBytes1;
    [FieldOffset(112)] public ulong VersionBytes2;
    [FieldOffset(120)] public ulong VersionBytes3;
    [FieldOffset(128)] public uint MappingBytes;
    [FieldOffset(132)] public uint RequestedGroups;
    [FieldOffset(136)] public ulong Reserved0;
    [FieldOffset(144)] public ulong Reserved1;
    [FieldOffset(152)] public ulong Reserved2;
}

[StructLayout(LayoutKind.Explicit, Size = TelemetryContract.EntryBytes)]
internal struct TelemetryWireEntry
{
    [FieldOffset(0)] public ushort Id;
    [FieldOffset(2)] public ushort Version;
    [FieldOffset(4)] public uint Offset;
    [FieldOffset(8)] public uint Bytes;
    [FieldOffset(12)] public uint Flags;
}
