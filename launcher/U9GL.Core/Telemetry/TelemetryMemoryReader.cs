using System.Diagnostics;
using System.IO.MemoryMappedFiles;

namespace U9GL.Core.Telemetry;

public sealed class TelemetryMemoryReader : IDisposable
{
    private readonly Process _process;
    private readonly ulong _created;
    private readonly MemoryMappedFile _mapping;
    private readonly MemoryMappedViewAccessor _view;
    private readonly byte[] _copy = new byte[TelemetryContract.MappingBytes];
    private TelemetrySnapshot? _identity;
    private bool _disposed;

    public TelemetryMemoryReader(int processId)
    {
        _process = Process.GetProcessById(processId);
        MemoryMappedFile? mapping = null;
        try
        {
            _ = _process.SafeHandle;
            _created = checked((ulong)_process.StartTime.ToFileTimeUtc());
            MappingName = TelemetryContract.MappingName(checked((uint)processId), _created);
            mapping = MemoryMappedFile.OpenExisting(MappingName, MemoryMappedFileRights.Read);
            _view = mapping.CreateViewAccessor(0, TelemetryContract.MappingBytes, MemoryMappedFileAccess.Read);
            _mapping = mapping;
        }
        catch
        {
            mapping?.Dispose();
            _process.Dispose();
            throw;
        }
    }

    public string MappingName { get; }
    public ulong ProcessCreatedFiletime => _created;
    public long Attempts { get; private set; }
    public long BusyAttempts { get; private set; }
    public long ChangedAttempts { get; private set; }

    public unsafe TelemetryDecodeResult Read()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_process.HasExited) return new(TelemetryReadStatus.ProcessExited, null, "Producer process exited.");
        long start = Stopwatch.GetTimestamp();
        byte* pointer = null;
        _view.SafeMemoryMappedViewHandle.AcquirePointer(ref pointer);
        try
        {
            pointer += _view.PointerOffset;
            TelemetryDecodeResult result = new(TelemetryReadStatus.Busy, null, "Publisher was writing.");
            for (int attempt = 0; attempt < TelemetryContract.ReadAttempts; attempt++)
            {
                Attempts++;
                uint before = Volatile.Read(ref *(uint*)(pointer + TelemetryContract.SequenceOffset));
                if ((before & 1) != 0) { BusyAttempts++; continue; }
                Thread.MemoryBarrier();
                uint size = Volatile.Read(ref *(uint*)(pointer + TelemetryContract.SizeOffset));
                int length = size is >= TelemetryContract.HeaderBytes and <= TelemetryContract.MappingBytes
                    ? (int)size : TelemetryContract.HeaderBytes;
                new ReadOnlySpan<byte>(pointer, length).CopyTo(_copy);
                Thread.MemoryBarrier();
                uint after = Volatile.Read(ref *(uint*)(pointer + TelemetryContract.SequenceOffset));
                if (Stopwatch.GetElapsedTime(start).TotalMilliseconds > TelemetryContract.ReadBudgetMilliseconds)
                    return new(TelemetryReadStatus.Changed, null, "Read budget expired; copy discarded.");
                result = TelemetryContract.Decode(_copy.AsSpan(0, length), before, after);
                if (result.Status == TelemetryReadStatus.Changed) { ChangedAttempts++; continue; }
                if (result.Snapshot is { } sample)
                {
                    if (sample.ProducerPid != _process.Id || sample.ProcessCreatedFiletime != _created ||
                        sample.QpcFrequency != (ulong)Stopwatch.Frequency ||
                        sample.SampleQpc > (ulong)Stopwatch.GetTimestamp() + 1)
                        return new(TelemetryReadStatus.Invalid, null, "Process identity or QPC clock mismatch.");
                    if (_identity is { } first &&
                        (sample.SessionId != first.SessionId || sample.SessionStartQpc != first.SessionStartQpc ||
                         sample.ProducerKind != first.ProducerKind || sample.ProducerVersion != first.ProducerVersion))
                        return new(TelemetryReadStatus.Invalid, null, "Publisher identity changed in the same mapping.");
                    _identity ??= sample;
                    if (_process.HasExited) return new(TelemetryReadStatus.ProcessExited, null, "Producer exited during read.");
                }
                return result;
            }
            return result;
        }
        finally { _view.SafeMemoryMappedViewHandle.ReleasePointer(); }
    }

    public static bool IsStale(TelemetrySnapshot sample, long nowQpc) =>
        Stopwatch.GetElapsedTime((long)sample.SampleQpc, nowQpc).TotalMilliseconds >= TelemetryContract.StaleMilliseconds;

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _view.Dispose();
        _mapping.Dispose();
        _process.Dispose();
    }
}
