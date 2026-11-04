using System.Diagnostics;
using U9GL.Core.Install;
using U9GL.Core.Telemetry;
using U9GL.WebDashboard;
using U9GL.WebDashboard.Telemetry;

string? Option(string name) { int at = Array.IndexOf(args, name); return at >= 0 && at + 1 < args.Length ? args[at + 1] : null; }
int port = int.Parse(Option("--port") ?? "10099");
int seconds = int.Parse(Option("--seconds") ?? "300");
string? publisher = Option("--publisher");
string? game = Option("--game");
string samples = Option("--samples") ?? "300";
using var stop = new CancellationTokenSource(TimeSpan.FromSeconds(seconds));
Console.CancelKeyPress += (_, e) => { e.Cancel = true; stop.Cancel(); };
await using var host = new WebDashboardHost(port, publisher != null ? () => new NativeSyntheticSource(publisher, samples) :
    game != null ? () => new GameTelemetrySource(new GameFolder(game)) : null);
await host.StartAsync(stop.Token);
Console.WriteLine("READY " + host.Address);
try { await Task.Delay(Timeout.Infinite, stop.Token); } catch (OperationCanceledException) { }

sealed class NativeSyntheticSource : IDashboardTelemetrySource
{
    private readonly Process _child;
    private TelemetryMemoryReader? _reader;
    internal NativeSyntheticSource(string executable, string samples)
    {
        var start = new ProcessStartInfo(executable) { UseShellExecute = false, CreateNoWindow = true };
        foreach (string argument in new[] { "--delay-ms", "1000", "--seconds", (int.Parse(samples) / 10 + 60).ToString(), "--samples", samples }) start.ArgumentList.Add(argument);
        _child = Process.Start(start) ?? throw new IOException("Dashboard harness: could not start synthetic publisher.");
    }
    public TelemetrySourceUpdate Poll()
    {
        if (_child.HasExited) return new("processExited", "Synthetic publisher exited; history retained.");
        if (_reader == null)
        {
            try { _reader = new(_child.Id); }
            catch (FileNotFoundException) { return new("waitingForTelemetry", "Waiting for the synthetic mapping"); }
        }
        var result = _reader.Read();
        return result.Snapshot is { } s ? new("live", "Synthetic native transport verification", s) :
            result.Status == TelemetryReadStatus.ProcessExited ? new("processExited", "Synthetic publisher exited") :
            result.Status is TelemetryReadStatus.Invalid or TelemetryReadStatus.Incompatible ? new("error", result.Reason ?? "Invalid sample") :
            new("live", "Synthetic publisher updating");
    }
    public void Dispose()
    {
        _reader?.Dispose();
        if (!_child.HasExited) _child.Kill();
        _child.WaitForExit();
        _child.Dispose();
    }
}
