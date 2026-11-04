using System.IO.MemoryMappedFiles;

namespace U9GL.Core.Telemetry;

public sealed class TelemetryRequestWriter : IDisposable
{
    private const int MaskOffset = 12, HeartbeatOffset = 16;
    private readonly MemoryMappedFile _mapping;
    private readonly MemoryMappedViewAccessor _view;
    private bool _disposed;

    public TelemetryRequestWriter(uint processId, ulong processCreatedFiletime)
    {
        MappingName = TelemetryContract.RequestMappingName(processId, processCreatedFiletime);
        var mapping = MemoryMappedFile.OpenExisting(MappingName, MemoryMappedFileRights.ReadWrite);
        try
        {
            _view = mapping.CreateViewAccessor(0, TelemetryContract.RequestBytes, MemoryMappedFileAccess.ReadWrite);
            if (_view.ReadUInt32(0) != TelemetryContract.RequestMagic || _view.ReadUInt16(4) != TelemetryContract.Major ||
                _view.ReadUInt32(8) != TelemetryContract.RequestBytes)
            {
                _view.Dispose();
                throw new InvalidDataException("Unknown telemetry request block layout.");
            }
            _mapping = mapping;
        }
        catch { mapping.Dispose(); throw; }
    }

    public string MappingName { get; }

    public unsafe void Renew(uint mask)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        byte* pointer = null;
        _view.SafeMemoryMappedViewHandle.AcquirePointer(ref pointer);
        try
        {
            pointer += _view.PointerOffset;
            Volatile.Write(ref *(uint*)(pointer + MaskOffset), mask);
            Interlocked.Increment(ref *(int*)(pointer + HeartbeatOffset));
        }
        finally { _view.SafeMemoryMappedViewHandle.ReleasePointer(); }
    }

    public void Dispose()
    {
        if (_disposed) return;
        try { Renew(0); } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {  }
        _disposed = true;
        _view.Dispose();
        _mapping.Dispose();
    }
}
