using System.Net;
using System.Text.Json;
using U9GL.WebDashboard.Telemetry;

namespace U9GL.WebDashboard.Tests;

[TestClass]
public sealed class DashboardStreamTests
{
    [TestMethod]
    public async Task ReconnectGetsAuthoritativeHistoryWithoutAddingSamplesAndShutdownClosesStreams()
    {
        await using var host = new WebDashboardHost(0);
        host.Telemetry.Apply(new("live", "replay", DashboardTelemetryTests.Sample()));
        await host.StartAsync();
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(8) };
        var streams = new List<HttpResponseMessage>();
        try
        {
            for (int i = 0; i < 4; i++)
            {
                var response = await client.GetAsync(new Uri(host.Address!, "/api/v1/events"), HttpCompletionOption.ResponseHeadersRead);
                streams.Add(response);
                Assert.AreEqual("text/event-stream", response.Content.Headers.ContentType?.MediaType);
            }
            using var overflow = await client.GetAsync(new Uri(host.Address!, "/api/v1/events"), HttpCompletionOption.ResponseHeadersRead);
            Assert.AreEqual(HttpStatusCode.ServiceUnavailable, overflow.StatusCode);
            using var stream = new StreamReader(await streams[0].Content.ReadAsStreamAsync());
            using var first = await ReadEvent(stream);
            Assert.AreEqual("1", first.RootElement.GetProperty("current").GetProperty("sampleIndex").GetString());
            host.Telemetry.Apply(new("processExited", "Exited"));
            using var current = JsonDocument.Parse(await client.GetStringAsync(new Uri(host.Address!, "/api/v1/telemetry")));
            Assert.AreEqual("processExited", current.RootElement.GetProperty("state").GetString());
            Assert.HasCount(1, host.Telemetry.GetView().Sessions[0].History);
            await host.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
            Assert.IsNull(host.Address);
        }
        finally { foreach (var stream in streams) stream.Dispose(); }

        await host.StartAsync();
        using var reconnect = await client.GetAsync(new Uri(host.Address!, "/api/v1/events"), HttpCompletionOption.ResponseHeadersRead);
        using var reader = new StreamReader(await reconnect.Content.ReadAsStreamAsync());
        using var restored = await ReadEvent(reader);
        Assert.AreEqual("processExited", restored.RootElement.GetProperty("state").GetString());
        Assert.AreEqual(1, restored.RootElement.GetProperty("sessions")[0].GetProperty("history").GetArrayLength());
    }

    private static async Task<JsonDocument> ReadEvent(StreamReader reader)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        string? line;
        while ((line = await reader.ReadLineAsync(timeout.Token)) != null)
            if (line.StartsWith("data: ", StringComparison.Ordinal)) return JsonDocument.Parse(line[6..]);
        throw new IOException("SSE stream ended before its initial event.");
    }
}
