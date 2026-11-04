using System.ComponentModel;
using System.Diagnostics;
using U9GL.Core.Install;
using U9GL.Core.Telemetry;

namespace U9GL.WebDashboard.Telemetry;

public sealed record TelemetrySourceUpdate(string State, string Detail, TelemetrySnapshot? Sample = null, DashboardGameDto? Game = null);

public interface IDashboardTelemetrySource : IDisposable { TelemetrySourceUpdate Poll(); }

public sealed class GameTelemetrySource : IDashboardTelemetrySource
{
    private readonly GameFolder _folder;
    private Process? _process;
    private TelemetryMemoryReader? _reader;
    private TelemetryRequestWriter? _requests;
    private DashboardGameDto? _game;
    private long _nextDiscovery;
    private bool _exited;

    public GameTelemetrySource(GameFolder folder) => _folder = folder;

    public TelemetrySourceUpdate Poll()
    {
        try
        {
            if (_process == null)
            {
                if (Stopwatch.GetTimestamp() >= _nextDiscovery)
                {
                    _nextDiscovery = Stopwatch.GetTimestamp() + Stopwatch.Frequency;
                    _process = FindGame();
                }
                if (_process == null) return _exited
                    ? new("processExited", "Game process exited; history retained.") : new("waitingForGame", "Waiting for game");
                _exited = false;
            }
            if (_process.HasExited) return ProcessExited();
            if (_reader == null)
            {
                try { _reader = new(_process.Id); }
                catch (FileNotFoundException)
                {
                    return LegacyProducer()
                        ? new("incompatible", "The game DLL publishes telemetry v1; this launcher reads v2. Stage matching U9GL builds.")
                        : new("waitingForTelemetry", "Waiting for telemetry; turn on the game feed in the launcher (diag_telemetry) and start the game again.");
                }
                try { _requests = new((uint)_process.Id, _reader.ProcessCreatedFiletime); }
                catch (Exception e) when (e is FileNotFoundException or InvalidDataException or UnauthorizedAccessException) { _requests = null; }
            }
            _requests?.Renew(TelemetrySchema.AllRequestBits);
            var result = _reader.Read();
            if (result.Status == TelemetryReadStatus.ProcessExited) return ProcessExited();
            if (result.Snapshot is { } sample)
                return sample.ProducerKind == TelemetryProducerKind.Game
                    ? new("live", "Reading existing game diagnostics", sample, _game)
                    : new("incompatible", "The selected game mapping contains a non-game producer.");
            return result.Status switch
            {
                TelemetryReadStatus.Incompatible => new("incompatible", result.Reason ?? "Unsupported telemetry contract"),
                TelemetryReadStatus.Invalid => new("error", result.Reason ?? "Invalid telemetry sample"),
                TelemetryReadStatus.Uninitialized => new("waitingForTelemetry", "Waiting for the first complete publication"),
                _ => new("live", "Publisher is updating; retaining the last accepted sample."),
            };
        }
        catch (Exception e) when (e is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException or ArgumentException)
        {
            if (_process?.HasExited == true) return ProcessExited();
            return new("error", "Telemetry acquisition: " + e.Message);
        }
    }

    private Process? FindGame()
    {
        Process? selected = null;
        var candidates = Process.GetProcessesByName("u9").Concat(Process.GetProcessesByName("u9-laa")).ToArray();
        try
        {
            foreach (var process in candidates)
            {
                try
                {
                    string? file = process.MainModule?.FileName;
                    if (file != null && (string.Equals(file, _folder.Exe, StringComparison.OrdinalIgnoreCase) ||
                        string.Equals(file, LargeAddress.CopyPath(_folder), StringComparison.OrdinalIgnoreCase)))
                    {
                        if (selected != null) throw new IOException("More than one game process belongs to this installation.");
                        _ = process.SafeHandle;
                        selected = process;
                        _game = Game(file);
                    }
                }
                catch (InvalidOperationException) {  }
                catch (Win32Exception) {  }
            }
            return selected;
        }
        catch { selected?.Dispose(); throw; }
        finally { foreach (var process in candidates) if (process != selected) process.Dispose(); }
    }

    private static DashboardGameDto Game(string file)
    {
        bool large;
        try { large = LargeAddress.IsSet(file); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { large = false;  }
        ulong bytes = large && Environment.Is64BitOperatingSystem ? 4UL << 30 : 2UL << 30;
        return new(Path.GetFileName(file), large, DashboardTelemetryProjection.Exact(bytes));
    }

    private bool LegacyProducer()
    {
        try
        {
            using var legacy = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(TelemetryContract.LegacyMappingName(
                (uint)_process!.Id, checked((ulong)_process.StartTime.ToFileTimeUtc())), System.IO.MemoryMappedFiles.MemoryMappedFileRights.Read);
            return true;
        }
        catch (FileNotFoundException) { return false; }
    }

    private TelemetrySourceUpdate ProcessExited()
    {
        _requests?.Dispose(); _requests = null;
        _reader?.Dispose(); _reader = null;
        _process?.Dispose(); _process = null;
        _exited = true; _nextDiscovery = 0;
        return new("processExited", "Game process exited; history retained.");
    }

    public void Dispose() { _requests?.Dispose(); _reader?.Dispose(); _process?.Dispose(); }
}
