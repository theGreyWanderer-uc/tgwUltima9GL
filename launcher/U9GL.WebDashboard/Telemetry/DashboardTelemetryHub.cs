using System.Text.Json;
using System.Threading.Channels;

namespace U9GL.WebDashboard.Telemetry;

public sealed class DashboardTelemetryHub(DashboardTelemetryStore store)
{
    public const int ClientLimit = 4;
    private readonly object _gate = new();
    private readonly HashSet<Channel<string>> _clients = [];

    public DashboardSubscription? Subscribe()
    {
        lock (_gate)
        {
            if (_clients.Count >= ClientLimit) return null;
            var channel = Channel.CreateBounded<string>(new BoundedChannelOptions(1)
            { FullMode = BoundedChannelFullMode.DropOldest, SingleReader = true, SingleWriter = false });
            _clients.Add(channel);
            channel.Writer.TryWrite(JsonSerializer.Serialize(store.GetView(), DashboardTelemetryStore.Json));
            return new(channel.Reader, () => { lock (_gate) { _clients.Remove(channel); channel.Writer.TryComplete(); } });
        }
    }

    public void Publish()
    {
        lock (_gate)
        {
            if (_clients.Count == 0) return;
            string json = JsonSerializer.Serialize(store.GetView(), DashboardTelemetryStore.Json);
            foreach (var client in _clients) client.Writer.TryWrite(json);
        }
    }
}

public sealed class DashboardSubscription(ChannelReader<string> reader, Action release) : IDisposable
{
    public ChannelReader<string> Reader { get; } = reader;
    public void Dispose() => release();
}
