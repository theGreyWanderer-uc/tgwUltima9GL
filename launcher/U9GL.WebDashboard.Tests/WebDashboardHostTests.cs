using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using U9GL.WebDashboard;

[assembly: Parallelize(Scope = ExecutionScope.MethodLevel)]

namespace U9GL.WebDashboard.Tests;

[TestClass]
public sealed class WebDashboardHostTests
{
    [TestMethod]
    public async Task RefusesForeignHostNamesAgainstDnsRebinding()
    {
        await using var host = new WebDashboardHost(0);
        await host.StartAsync();
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        foreach (string path in new[] { "/", "/api/v1/telemetry" })
        {
            using var foreign = new HttpRequestMessage(HttpMethod.Get, new Uri(host.Address!, path));
            foreign.Headers.Host = "attacker.example";
            using var refused = await client.SendAsync(foreign);
            Assert.AreEqual(HttpStatusCode.BadRequest, refused.StatusCode);
            Assert.AreEqual(0, (await refused.Content.ReadAsByteArrayAsync()).Length);
        }
        using var named = new HttpRequestMessage(HttpMethod.Get, new Uri(host.Address!, "/api/v1/telemetry"));
        named.Headers.Host = "LocalHost:" + host.Address!.Port;
        using var served = await client.SendAsync(named);
        Assert.AreEqual(HttpStatusCode.OK, served.StatusCode);
    }

    [TestMethod]
    public void NetworkAdmissionNeedsTheTokenFromOtherDevicesOnly()
    {
        const string token = "abcdefghijklmnopqrstuv";
        var lan = IPAddress.Parse("192.168.1.30");
        const string api = "/api/v1/telemetry";
        Assert.AreEqual(WebDashboardHost.Admission.Serve, WebDashboardHost.Admit(IPAddress.Loopback, "127.0.0.1", api, null, null, token));
        Assert.AreEqual(WebDashboardHost.Admission.Refuse, WebDashboardHost.Admit(IPAddress.Loopback, "attacker.example", api, token, token, token));
        Assert.AreEqual(WebDashboardHost.Admission.Refuse, WebDashboardHost.Admit(lan, "192.168.1.20", "/", token, null, null));
        Assert.AreEqual(WebDashboardHost.Admission.Serve, WebDashboardHost.Admit(lan, "192.168.1.20", "/assets/index.js", null, null, token));
        Assert.AreEqual(WebDashboardHost.Admission.ServeAndRemember, WebDashboardHost.Admit(lan, "192.168.1.20", api, token, null, token));
        Assert.AreEqual(WebDashboardHost.Admission.Serve, WebDashboardHost.Admit(lan, Environment.MachineName.ToLowerInvariant(), api, null, token, token));
        Assert.AreEqual(WebDashboardHost.Admission.NeedsToken, WebDashboardHost.Admit(lan, "192.168.1.20", api, null, null, token));
        Assert.AreEqual(WebDashboardHost.Admission.NeedsToken, WebDashboardHost.Admit(lan, "192.168.1.20", "/api/v1/events", token[..^1], token + "x", token));
        Assert.AreEqual(WebDashboardHost.Admission.Refuse, WebDashboardHost.Admit(lan, "attacker.example", "/", token, token, token));
        Assert.AreEqual(WebDashboardHost.Admission.Refuse, WebDashboardHost.Admit(lan, "localhost", api, token, token, token));
        foreach (var (address, _) in WebDashboardHost.LocalAddresses())
            Assert.IsFalse(address.ToString().StartsWith("169.254.", StringComparison.Ordinal), address.ToString());
        string a = WebDashboardHost.NewNetworkToken(), b = WebDashboardHost.NewNetworkToken();
        Assert.AreNotEqual(a, b);
        Assert.AreEqual(22, a.Length);
        Assert.IsTrue(a.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_'), a);
    }

    [TestMethod]
    public async Task ServesEmbeddedPageOnIpv4Loopback()
    {
        await using var host = new WebDashboardHost(0);
        await host.StartAsync();
        Assert.IsNotNull(host.Address);
        Assert.AreEqual("127.0.0.1", host.Address.Host);
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        using var response = await client.GetAsync(host.Address);
        Assert.AreEqual(HttpStatusCode.OK, response.StatusCode);
        Assert.AreEqual("text/html", response.Content.Headers.ContentType?.MediaType);
        string html = await response.Content.ReadAsStringAsync();
        StringAssert.Contains(html, "U9GL diagnostics");
        Assert.AreEqual("no-cache", response.Headers.CacheControl?.ToString());
        var assets = Regex.Matches(html, "(?:src|href)=\"(/assets/[^\"]+)\"").Select(m => m.Groups[1].Value).ToArray();
        Assert.IsTrue(assets.Any(a => a.EndsWith(".js")));
        Assert.IsTrue(assets.Any(a => a.EndsWith(".css")));
        foreach (string asset in assets)
        {
            using var result = await client.GetAsync(new Uri(host.Address, asset));
            result.EnsureSuccessStatusCode();
            StringAssert.Contains(result.Headers.CacheControl!.ToString(), "immutable");
            string content = await result.Content.ReadAsStringAsync();
            if (asset.EndsWith(".css"))
            {
                foreach (Match font in Regex.Matches(content, "url\\(([^)]+\\.woff2)\\)"))
                {
                    using var fontResponse = await client.GetAsync(new Uri(new Uri(host.Address, asset), font.Groups[1].Value.Trim('"', '\'')));
                    fontResponse.EnsureSuccessStatusCode();
                    Assert.AreEqual("font/woff2", fontResponse.Content.Headers.ContentType?.MediaType);
                }
            }
        }
        foreach (string file in new[] { "meta-react-LICENSE.txt", "trekcolors-LICENSE.txt", "antonio-OFL.txt" })
        {
            using var notice = await client.GetAsync(new Uri(host.Address, "/licenses/" + file));
            notice.EnsureSuccessStatusCode();
            Assert.AreEqual("text/plain", notice.Content.Headers.ContentType?.MediaType);
            Assert.AreEqual("no-cache", notice.Headers.CacheControl?.ToString());
            string text = await notice.Content.ReadAsStringAsync();
            Assert.IsFalse(string.IsNullOrWhiteSpace(text));
            if (file == "meta-react-LICENSE.txt")
            {
                StringAssert.Contains(text, "React 19.3.1 - MIT");
                StringAssert.Contains(text, "React DOM 19.3.1 - MIT");
                StringAssert.Contains(text, "Scheduler 0.28.0 - MIT");
                StringAssert.Contains(text, "Copyright (c) Meta Platforms, Inc. and affiliates.");
                StringAssert.Contains(text, "Permission is hereby granted");
                StringAssert.Contains(text, "THE SOFTWARE IS PROVIDED");
            }
        }
        foreach (string removed in new[] { "index.html", "react-LICENSE.txt", "react-dom-LICENSE.txt", "scheduler-LICENSE.txt" })
        {
            using var oldNotice = await client.GetAsync(new Uri(host.Address, "/licenses/" + removed));
            Assert.AreEqual(HttpStatusCode.NotFound, oldNotice.StatusCode);
        }
        using var missing = await client.GetAsync(new Uri(host.Address, "not-a-page"));
        Assert.AreEqual(HttpStatusCode.NotFound, missing.StatusCode);
        using var missingApi = await client.GetAsync(new Uri(host.Address, "/api/not-an-endpoint"));
        Assert.AreEqual(HttpStatusCode.NotFound, missingApi.StatusCode);
    }

    [TestMethod]
    public async Task ShutdownReleasesPortAndHostCanRestart()
    {
        await using var host = new WebDashboardHost(0);
        await host.StartAsync();
        int port = host.Address!.Port;
        await host.DisposeAsync();
        Assert.IsNull(host.Address);
        using var listener = new TcpListener(IPAddress.Loopback, port);
        listener.Start();
        listener.Stop();
        await host.StartAsync();
        Assert.IsNotNull(host.Address);
    }

    [TestMethod]
    public async Task OccupiedPortFailsWithoutFallbackAndCanBeRetried()
    {
        using var occupied = new TcpListener(IPAddress.Loopback, 0);
        occupied.Start();
        int port = ((IPEndPoint)occupied.LocalEndpoint).Port;
        await using var host = new WebDashboardHost(port);
        await Assert.ThrowsAsync<IOException>(() => host.StartAsync());
        Assert.IsNull(host.Address);
        occupied.Stop();
        await host.StartAsync();
        Assert.AreEqual(port, host.Address!.Port);
    }

    [TestMethod]
    public async Task SecondHostFailureDoesNotStopFirstHost()
    {
        await using var first = new WebDashboardHost(0);
        await first.StartAsync();
        await using var second = new WebDashboardHost(first.Address!.Port);
        await Assert.ThrowsAsync<IOException>(() => second.StartAsync());
        await second.DisposeAsync();
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        Assert.AreEqual(HttpStatusCode.OK, (await client.GetAsync(first.Address)).StatusCode);
    }

    [TestMethod]
    public async Task CancelledStartupLeavesHostReusable()
    {
        await using var host = new WebDashboardHost(0);
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        await Assert.ThrowsAsync<OperationCanceledException>(() => host.StartAsync(cancelled.Token));
        Assert.IsNull(host.Address);
        await host.StartAsync();
        Assert.IsNotNull(host.Address);
    }

    [TestMethod]
    public async Task ShutdownCompletesWithAnIncompleteBrowserRequest()
    {
        await using var host = new WebDashboardHost(0);
        await host.StartAsync();
        int port = host.Address!.Port;
        using var browser = new TcpClient();
        await browser.ConnectAsync(IPAddress.Loopback, port);
        await browser.GetStream().WriteAsync(Encoding.ASCII.GetBytes("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n"));
        await host.DisposeAsync().AsTask().WaitAsync(WebDashboardHost.ShutdownTimeout + TimeSpan.FromSeconds(3));
        using var listener = new TcpListener(IPAddress.Loopback, port);
        listener.Start();
    }
}
