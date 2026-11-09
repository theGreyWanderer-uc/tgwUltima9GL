using System.Diagnostics;
using System.Text.Json;
using U9GL.Core.Telemetry;
using U9GL.Core.Install;
using U9GL.WebDashboard.Telemetry;

namespace U9GL.WebDashboard.Tests;

[TestClass]
public sealed class DashboardTelemetryTests
{
    [TestMethod]
    public void SharedWireFixturesReachBrowserProjectionOnlyAfterAcceptance()
    {
        using var fixtures = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Data", "telemetry-samples.json")));
        int accepted = 0, rejected = 0;
        foreach (var item in fixtures.RootElement.GetProperty("cases").EnumerateArray())
        {
            var decoded = TelemetryContract.Decode(Convert.FromHexString(item.GetProperty("hex").GetString()!),
                uint.Parse(item.GetProperty("sequenceBefore").GetString()!), uint.Parse(item.GetProperty("sequenceAfter").GetString()!));
            var store = new DashboardTelemetryStore();
            store.Apply(decoded.Snapshot is { } snapshot ? new("live", "fixture", snapshot) : new("error", decoded.Reason ?? "Rejected"));
            if (item.GetProperty("status").GetString() == "Accepted")
            {
                accepted++;
                Assert.IsNotNull(store.GetView().Current);
                Assert.AreEqual(decoded.Snapshot!.SampleIndex.ToString(System.Globalization.CultureInfo.InvariantCulture), store.GetView().Current!.SampleIndex);
            }
            else { rejected++; Assert.IsNull(store.GetView().Current); Assert.IsEmpty(store.GetView().Sessions); }
        }
        Assert.AreEqual(24, accepted);
        Assert.AreEqual(37, rejected);
    }

    internal static TelemetrySnapshot Sample(ulong index = 1, ulong sampleMs = 1000, string session = "0123456789abcdef0123456789abcdef",
        bool memory = true) =>
        new(42, 133000000000000001, session, 1000, 1000, 1000 + sampleMs, 133000000000000002, index,
            TelemetryProducerKind.Synthetic, "test", 0, TelemetrySchema.AllRequestBits,
            [
                new(TelemetrySchema.Frames, 1000 + sampleMs, [index * 6, index * 6, 0, 0, 0]),
                new(TelemetrySchema.Resources, 1000 + sampleMs, [1, ulong.MaxValue, 2, 1]),
                .. memory ? new TelemetryGroup[] { new(TelemetrySchema.Memory, 1000, [ulong.MaxValue, 0, ulong.MaxValue, 0]) } : [],
            ], 0);

    [TestMethod]
    public void BrowserProjectionPreservesU64AndDistinguishesZeroAbsentAndOldMemory()
    {
        long clock = 3100;
        var store = new DashboardTelemetryStore(() => clock);
        store.Apply(new("live", "test", Sample(sampleMs: 2000)));
        var view = store.GetView();
        Assert.AreEqual("live", view.State);
        Assert.AreEqual("133000000000000001", view.Current!.Identity.ProcessCreatedFiletime);
        Assert.AreEqual(2, view.Version);
        Assert.AreEqual("18446744073709551615", view.Current.Groups["resources"].Values["textureBytes"]);
        Assert.AreEqual("0", view.Current.Groups["frames"].Values["drawCallsLast"]);
        CollectionAssert.AreEqual(TelemetrySchema.Groups.Select(g => g.Name).ToArray(), view.Current.RequestedGroups);
        Assert.AreEqual("ready", view.Current.Groups["memory"].Quality);
        Assert.AreEqual(10000, view.Current.Groups["memory"].StaleMs);
        Assert.AreEqual(2100, view.Current.Groups["memory"].AgeMs);
        Assert.AreEqual("ready", view.Current.Groups["frames"].Quality);
        clock = 11000;
        Assert.AreEqual("stale", store.GetView().Current!.Groups["memory"].Quality);
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(view, DashboardTelemetryStore.Json));
        Assert.AreEqual(JsonValueKind.String, json.RootElement.GetProperty("current").GetProperty("sampleIndex").ValueKind);
        Assert.AreEqual(JsonValueKind.Object, json.RootElement.GetProperty("current").GetProperty("groups").GetProperty("memory").ValueKind);
        Assert.AreEqual("18446744073709551615", json.RootElement.GetProperty("sessions")[0].GetProperty("history")[0]
            .GetProperty("values").GetProperty("memory.privateBytes").GetString());
        store.Apply(new("live", "test", Sample(2, 2100, memory: false)));
        Assert.IsFalse(store.GetView().Current!.Groups.ContainsKey("memory"));
        var point = store.GetView().Sessions[0].History[1];
        Assert.IsTrue(point.GapBefore);
        Assert.IsFalse(point.Values.ContainsKey("memory.privateBytes"));
        Assert.AreEqual("0", point.Values["frames.drawCallsLast"]);
    }

    [TestMethod]
    public void SignedFieldsAndSchemaReachTheBrowserAsTheProducerMeantThem()
    {
        var sample = Sample() with { Groups = [
            new(TelemetrySchema.World, 2000, [1, 9, 1, unchecked((ulong)-1234567L), 2345678, unchecked((ulong)-5L)]),
            new(TelemetrySchema.MemoryTrend, 1000, [unchecked((ulong)-1536250L), 2000, 480, 16, 16, 0]),
        ] };
        var store = new DashboardTelemetryStore(() => 2100);
        store.Apply(new("live", "test", sample));
        var view = store.GetView();
        Assert.AreEqual("-1234567", view.Current!.Groups["world"].Values["avatarX"]);
        Assert.AreEqual("9", view.Current.Groups["world"].Values["map"]);
        Assert.AreEqual("-1536250", view.Sessions[0].History[0].Values["memoryTrend.floorSlopeMilli"]);
        var fps = view.Schema["pacing"].Single();
        Assert.AreEqual(("fpsMilli", "unsigned", 1000, "fps"), (fps.Name, fps.Kind, fps.Scale, fps.Unit));
        CollectionAssert.AreEqual(new[] { "off", "nearest", "trilinear" }, view.Schema["renderer"][0].Names);
        Assert.AreEqual("signed", view.Schema["world"][3].Kind);
        Assert.AreEqual(("cells", "count", 256), (view.Arrays["areaCells"].Name, view.Arrays["areaCells"].CountField, view.Arrays["areaCells"].Capacity));
        Assert.AreEqual("objects", view.Arrays["areaCells"].Parts[2].Name);
        Assert.AreEqual(U9GL.Core.Saves.SaveWording.Shared.Map(9).DisplayName, view.Current.Labels["world.map"]);
        Assert.IsNull(view.Game);
        store.Apply(new("live", "test", sample, new DashboardGameDto("u9-laa.exe", true, "4294967296")));
        store.Apply(new("processExited", "Exited"));
        Assert.AreEqual("4294967296", store.GetView().Game!.AddressSpaceBytes);
    }

    [TestMethod]
    public void DuplicatePollsDoNotAppendHistoryOrRefreshCollectionTime()
    {
        long clock = 2000;
        var store = new DashboardTelemetryStore(() => clock);
        store.Apply(new("live", "test", Sample()));
        clock = 5000;
        for (int i = 0; i < 100; i++) store.Apply(new("live", "test", Sample()));
        var view = store.GetView();
        Assert.HasCount(1, view.Sessions[0].History);
        Assert.AreEqual("stale", view.State);
        Assert.AreEqual(3000, view.Current!.AgeMs);
        Assert.AreEqual(4000, view.Current.Groups["memory"].AgeMs);
    }

    [TestMethod]
    public void RetainsEvidenceAcrossErrorsExitAndNewSessionAndMarksGaps()
    {
        var store = new DashboardTelemetryStore(() => 10000);
        store.Apply(new("live", "test", Sample()));
        store.Apply(new("error", "Bad sample"));
        Assert.AreEqual("1", store.GetView().Current!.SampleIndex);
        store.Apply(new("live", "test", Sample(4, 1400)));
        Assert.IsTrue(store.GetView().Sessions[0].History[1].GapBefore);
        store.Apply(new("processExited", "Exited"));
        Assert.AreEqual("processExited", store.GetView().State);
        Assert.HasCount(2, store.GetView().Sessions[0].History);
        store.Apply(new("live", "new session", Sample(session: "11111111111111111111111111111111")));
        Assert.HasCount(2, store.GetView().Sessions);
        Assert.AreEqual("1", store.GetView().Current!.SampleIndex);
        store.Apply(new("incompatible", "Unsupported"));
        Assert.AreEqual("incompatible", store.GetView().State);
        Assert.IsNotNull(store.GetView().Current);
    }

    [TestMethod]
    public void HistoryIsBoundedByCountDurationAndSessions()
    {
        var store = new DashboardTelemetryStore(() => 1000000);
        for (int session = 0; session < 5; session++)
            for (ulong i = 1; i <= 1500; i++) store.Apply(new("live", "test", Sample(i, i * 100, session.ToString("D32"))));
        var view = store.GetView();
        Assert.HasCount(3, view.Sessions);
        foreach (var session in view.Sessions)
        {
            Assert.HasCount(600, session.History);
            Assert.IsTrue(session.History[^1].ElapsedMs - session.History[0].ElapsedMs <= 120000);
        }
        Assert.IsTrue(JsonSerializer.SerializeToUtf8Bytes(view.Sessions, DashboardTelemetryStore.Json).Length < DashboardTelemetryStore.HistoryByteLimit);
        store.Apply(new("live", "test", Sample(1501, 300000, "00000000000000000000000000000004")));
        Assert.HasCount(1, store.GetView().Sessions[^1].History);
    }

    [TestMethod]
    public void LongHistoryKeepsOnePointPerFiveSecondsForHalfAnHourAndKeepsGaps()
    {
        var store = new DashboardTelemetryStore(() => 10_000_000);
        for (ulong i = 1; i <= 2400; i++)
            if (i is <= 1200 or > 1260) store.Apply(new("live", "test", Sample(i, i * 1000)));
        var session = store.GetView().Sessions.Single();
        var coarse = session.LongHistory;
        Assert.IsTrue(coarse.Length <= DashboardTelemetryStore.LongSamplesPerSession, coarse.Length.ToString());
        Assert.IsTrue(coarse[^1].ElapsedMs - coarse[0].ElapsedMs <= DashboardTelemetryStore.LongHistorySeconds * 1000);
        for (int i = 1; i < coarse.Length; i++) Assert.IsTrue(coarse[i].ElapsedMs - coarse[i - 1].ElapsedMs >= 5000);
        Assert.AreEqual(1, coarse.Count(p => p.GapBefore));
        Assert.IsTrue(coarse.Single(p => p.GapBefore).ElapsedMs > 1_260_000);
        Assert.HasCount(121, session.History);
        Assert.AreEqual(DashboardTelemetryStore.LongIntervalSeconds, store.GetView().Limits.LongIntervalSeconds);
    }

    [TestMethod]
    public void BackwardSamplesAreRejectedWithoutOverwritingEvidence()
    {
        var store = new DashboardTelemetryStore(() => 10000);
        store.Apply(new("live", "test", Sample(2, 2000)));
        store.Apply(new("live", "bad", Sample(1, 1000)));
        Assert.AreEqual("error", store.GetView().State);
        Assert.AreEqual("2", store.GetView().Current!.SampleIndex);
        Assert.HasCount(1, store.GetView().Sessions[0].History);
    }

    [TestMethod]
    public void SlowSubscribersKeepOneLatestUpdateAndReleaseTheirSlots()
    {
        var store = new DashboardTelemetryStore(() => 10000);
        var hub = new DashboardTelemetryHub(store);
        var clients = Enumerable.Range(0, 4).Select(_ => hub.Subscribe()!).ToArray();
        Assert.IsNull(hub.Subscribe());
        for (ulong i = 1; i <= 1000; i++) { store.Apply(new("live", "test", Sample(i, i))); hub.Publish(); }
        Assert.IsTrue(clients[0].Reader.TryRead(out string? json));
        using var view = JsonDocument.Parse(json!);
        Assert.AreEqual("1000", view.RootElement.GetProperty("current").GetProperty("sampleIndex").GetString());
        Assert.IsFalse(clients[0].Reader.TryRead(out _));
        foreach (var client in clients) client.Dispose();
        using var replacement = hub.Subscribe();
        Assert.IsNotNull(replacement);
    }

    [TestMethod]
    public async Task WorkerSurvivesSourceErrorsAndDisposesOnHostShutdown()
    {
        var source = new ReplaySource();
        await using var host = new WebDashboardHost(0, () => source);
        await host.StartAsync();
        await WaitFor(() => source.Polls >= 3);
        await host.DisposeAsync();
        Assert.IsTrue(source.Disposed);
        Assert.IsNotNull(host.Telemetry.GetView().Current);
    }

    private static async Task WaitFor(Func<bool> ready)
    {
        var elapsed = Stopwatch.StartNew();
        while (!ready() && elapsed.Elapsed < TimeSpan.FromSeconds(5)) await Task.Delay(25);
        Assert.IsTrue(ready());
    }

    [TestMethod]
    public async Task GameDiscoverySelectsExactInstallAndDetectsExitWithoutAcceptingSyntheticAsGame()
    {
        var at = new DirectoryInfo(AppContext.BaseDirectory);
        while (at != null && !(File.Exists(Path.Combine(at.FullName, "VERSION")) && Directory.Exists(Path.Combine(at.FullName, "u9gl"))))
            at = at.Parent;
        string tool = Path.Combine(at?.FullName ?? "", "tests", "telemetry", ".out", "u9telemetry.exe");
        if (!File.Exists(tool)) Assert.Inconclusive("Run tests/telemetry/build-tools.sh first.");
        string tempRoot = Path.GetFullPath(Path.GetTempPath());
        string testFolder = Path.GetFullPath(Path.Combine(tempRoot, "u9gl-dashboard-test-" + Guid.NewGuid().ToString("N")));
        Directory.CreateDirectory(testFolder);
        Process? child = null;
        GameTelemetrySource? matching = null;
        GameTelemetrySource? other = null;
        try
        {
            string executable = Path.Combine(testFolder, "u9.exe");
            File.Copy(tool, executable);
            child = Process.Start(new ProcessStartInfo(executable, "--delay-ms 1500 --seconds 10") { UseShellExecute = false, CreateNoWindow = true });
            Assert.IsNotNull(child);
            matching = new(new GameFolder(testFolder));
            other = new(new GameFolder(Path.Combine(testFolder, "other-install")));
            Assert.AreEqual("waitingForGame", other.Poll().State);
            await WaitFor(() => matching.Poll().State == "waitingForTelemetry");
            await WaitFor(() => matching.Poll().State == "incompatible");
            child.Kill(); await child.WaitForExitAsync();
            Assert.AreEqual("processExited", matching.Poll().State);
        }
        finally
        {
            matching?.Dispose(); other?.Dispose();
            if (child != null) { if (!child.HasExited) { child.Kill(); await child.WaitForExitAsync(); } child.Dispose(); }
            if (Path.GetDirectoryName(testFolder) != Path.TrimEndingDirectorySeparator(tempRoot)) throw new IOException("Dashboard test cleanup escaped its temporary root.");
            for (int attempt = 0; ; attempt++)
            {
                try { Directory.Delete(testFolder, recursive: true); break; }
                catch (Exception e) when (attempt < 20 && e is IOException or UnauthorizedAccessException)
                { await Task.Delay(100); }
            }
        }
    }

    private sealed class ReplaySource : IDashboardTelemetrySource
    {
        internal int Polls;
        internal bool Disposed;
        public TelemetrySourceUpdate Poll()
        {
            int poll = Interlocked.Increment(ref Polls);
            if (poll == 2) throw new IOException("synthetic reader error");
            return new("live", "replay", Sample((ulong)poll));
        }
        public void Dispose() => Disposed = true;
    }
}
