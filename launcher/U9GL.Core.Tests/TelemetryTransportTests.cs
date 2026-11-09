using System.Diagnostics;
using U9GL.Core.Telemetry;

namespace U9GL.Core.Tests;

[TestClass]
[DoNotParallelize]
public sealed class TelemetryTransportTests
{
    private static string PublisherPath
    {
        get
        {
            string root;
            try { root = SettingsCatalogTests.RepositoryRoot(); }
            catch (InvalidOperationException) { root = ""; }
            return Path.Combine(root, "tests", "telemetry", ".out", "u9telemetry.exe");
        }
    }

    [ClassInitialize]
    public static void CheckNativeTool(TestContext context)
    {
        if (!File.Exists(PublisherPath)) Assert.Inconclusive("Run tests/telemetry/build-tools.sh before transport integration tests.");
    }

    [TestMethod]
    public async Task ReaderCanStartBeforePublisherCreatesMapping()
    {
        using var publisher = await NativePublisher.StartAsync("--delay-ms", "1500", "--seconds", "5");
        Assert.Throws<FileNotFoundException>(() => new TelemetryMemoryReader(publisher.Process.Id));
        using var reader = await OpenEventually(publisher.Process.Id);
        var sample = await ReadAccepted(reader);
        Assert.AreEqual(TelemetryProducerKind.Synthetic, sample.ProducerKind);
    }

    [TestMethod]
    public async Task TwoReadOnlyReadersAcceptNoMixedSnapshotsUnderStress()
    {
        using var publisher = await NativePublisher.StartAsync("--stress", "--seconds", "5");
        using var first = await OpenEventually(publisher.Process.Id);
        using var second = await OpenEventually(publisher.Process.Id);
        async Task<int> Observe(TelemetryMemoryReader reader)
        {
            await Task.Yield();
            int accepted = 0;
            var watch = Stopwatch.StartNew();
            while (watch.Elapsed < TimeSpan.FromSeconds(1))
            {
                var result = reader.Read();
                if (result.Snapshot is not { } s)
                {
                    Assert.IsTrue(result.Status is TelemetryReadStatus.Busy or TelemetryReadStatus.Changed or TelemetryReadStatus.Uninitialized,
                        result.Status + ": " + result.Reason);
                    continue;
                }
                ulong n = s.SampleIndex;
                CollectionAssert.AreEqual(new[] { n * 6, n * 6, n % 5, n % 4096, (n % 4096) * 3 }, s.Group(TelemetrySchema.Frames)!.Values.ToArray());
                CollectionAssert.AreEqual(new[] { n, n * 4096, n * 2, n }, s.Group(TelemetrySchema.Resources)!.Values.ToArray());
                CollectionAssert.AreEqual(new[] { n * 1024, n * 512, n * 2048, n * 1024 }, s.Group(TelemetrySchema.Memory)!.Values.ToArray());
                Assert.AreEqual(1, s.SkippedGroups);
                Assert.HasCount(18, s.Groups);
                Assert.AreEqual(5UL, s.Group(TelemetrySchema.Player)!["classId"]);
                Assert.HasCount(40, s.Group(TelemetrySchema.TextureList)!.Array);
                Assert.HasCount(25, s.Group(TelemetrySchema.MeshList)!.Array);
                Assert.HasCount(30, s.Group(TelemetrySchema.AreaCells)!.Array);
                Assert.AreEqual(n + 1, s.Group(TelemetrySchema.Area)!["gridW"]);
                Assert.AreEqual(-(long)n, unchecked((long)s.Group(TelemetrySchema.World)!["avatarX"]));
                Assert.AreEqual(n * 65536, s.Group(TelemetrySchema.TextureCache)!["cacheBytes"]);
                accepted++;
            }
            return accepted;
        }
        int[] counts = await Task.WhenAll(Task.Run(() => Observe(first)), Task.Run(() => Observe(second)));
        Assert.IsTrue(counts.All(n => n > 10), string.Join(",", counts));
        Console.WriteLine($"Stress: {counts.Sum()} coherent snapshots; {first.Attempts + second.Attempts} attempts; " +
            $"busy={first.BusyAttempts + second.BusyAttempts}, changed={first.ChangedAttempts + second.ChangedAttempts}");
    }

    [TestMethod]
    public async Task KilledMidWriteDoesNotBlockReaderOrMakeOldMappingLive()
    {
        using var publisher = await NativePublisher.StartAsync("--stress", "--odd-after", "5", "--seconds", "20");
        using var reader = await OpenEventually(publisher.Process.Id);
        var last = await ReadAccepted(reader);
        await publisher.WaitForLine("HOLDING_ODD");
        long attempts = reader.Attempts;
        var watch = Stopwatch.StartNew();
        var interrupted = reader.Read();
        Assert.AreEqual(TelemetryReadStatus.Busy, interrupted.Status);
        Assert.IsNull(interrupted.Snapshot);
        Assert.IsTrue(reader.Attempts - attempts <= TelemetryContract.ReadAttempts);
        Assert.IsTrue(watch.Elapsed < TimeSpan.FromSeconds(1));
        publisher.Stop();
        Assert.AreEqual(TelemetryReadStatus.ProcessExited, reader.Read().Status);
        Assert.IsNotNull(last);
    }

    [TestMethod]
    public async Task WrappedZeroGuardIsAcceptedAsInitialized()
    {
        using var publisher = await NativePublisher.StartAsync("--wrap", "--samples", "1", "--seconds", "5");
        await publisher.WaitForLine("WRAPPED 0");
        using var reader = await OpenEventually(publisher.Process.Id);
        Assert.AreEqual(1UL, (await ReadAccepted(reader)).SampleIndex);
    }

    [TestMethod]
    public async Task UnknownLayoutIsRefusedAndIdlePublisherCanBecomeStale()
    {
        using (var incompatible = await NativePublisher.StartAsync("--incompatible", "--seconds", "5"))
        using (var reader = await OpenEventually(incompatible.Process.Id))
        {
            TelemetryDecodeResult result;
            var watch = Stopwatch.StartNew();
            do { result = reader.Read(); await Task.Delay(10); }
            while (result.Status == TelemetryReadStatus.Uninitialized && watch.Elapsed < TimeSpan.FromSeconds(2));
            Assert.AreEqual(TelemetryReadStatus.Incompatible, result.Status);
            Assert.IsNull(result.Snapshot);
        }
        using var publisher = await NativePublisher.StartAsync("--samples", "1", "--seconds", "5");
        using var idle = await OpenEventually(publisher.Process.Id);
        var sample = await ReadAccepted(idle);
        Assert.IsTrue(TelemetryMemoryReader.IsStale(sample, (long)(sample.SampleQpc + sample.QpcFrequency * 2)));
        Assert.IsFalse(publisher.Process.HasExited);
        Assert.AreEqual(sample.SampleIndex, (await ReadAccepted(idle)).SampleIndex);
    }

    [TestMethod]
    public async Task RestartUsesNewProcessIdentityAndLeavesOldReaderExited()
    {
        using var first = await NativePublisher.StartAsync("--seconds", "5");
        using var oldReader = await OpenEventually(first.Process.Id);
        var old = await ReadAccepted(oldReader);
        first.Stop();
        using var second = await NativePublisher.StartAsync("--seconds", "5");
        using var newReader = await OpenEventually(second.Process.Id);
        var current = await ReadAccepted(newReader);
        Assert.AreNotEqual(oldReader.MappingName, newReader.MappingName);
        Assert.AreNotEqual(old.SessionId, current.SessionId);
        Assert.AreEqual(TelemetryReadStatus.ProcessExited, oldReader.Read().Status);
    }

    [TestMethod]
    public async Task RequestsReachTheProducerAndAreWithdrawnOnDisposeOrExpiry()
    {
        using var publisher = await NativePublisher.StartAsync("--seconds", "20");
        using var reader = await OpenEventually(publisher.Process.Id);
        var first = await ReadAccepted(reader);
        Assert.AreEqual(0u, first.RequestedGroups);
        using (var requests = new TelemetryRequestWriter(first.ProducerPid, first.ProcessCreatedFiletime))
        {
            using var renewing = new CancellationTokenSource();
            var renew = Task.Run(async () =>
            {
                while (!renewing.IsCancellationRequested) { requests.Renew(TelemetrySchema.AllRequestBits); await Task.Delay(100); }
            });
            await publisher.WaitForLine("REQUEST 0x" + TelemetrySchema.AllRequestBits.ToString("x8", System.Globalization.CultureInfo.InvariantCulture));
            var watch = Stopwatch.StartNew();
            TelemetrySnapshot sample;
            do sample = await ReadAccepted(reader);
            while (sample.RequestedGroups == 0 && watch.Elapsed < TimeSpan.FromSeconds(2));
            Assert.AreEqual(TelemetrySchema.AllRequestBits, sample.RequestedGroups);
            renewing.Cancel(); await renew;
        }
        await publisher.WaitForLine("REQUEST 0x00000000", TimeSpan.FromSeconds(2));

        using var silent = new TelemetryRequestWriter(first.ProducerPid, first.ProcessCreatedFiletime);
        silent.Renew(TelemetrySchema.Memory.RequestBit);
        await publisher.WaitForLine("REQUEST 0x00000008");
        var lapse = Stopwatch.StartNew();
        await publisher.WaitForLine("REQUEST 0x00000000", TimeSpan.FromSeconds(8));
        Assert.IsTrue(lapse.Elapsed >= TimeSpan.FromSeconds(4), lapse.Elapsed.ToString());
    }

    private static async Task<TelemetryMemoryReader> OpenEventually(int pid)
    {
        var watch = Stopwatch.StartNew();
        while (true)
        {
            try { return new TelemetryMemoryReader(pid); }
            catch (FileNotFoundException) when (watch.Elapsed < TimeSpan.FromSeconds(4)) { await Task.Delay(10); }
        }
    }

    private static async Task<TelemetrySnapshot> ReadAccepted(TelemetryMemoryReader reader)
    {
        var watch = Stopwatch.StartNew();
        while (watch.Elapsed < TimeSpan.FromSeconds(3))
        {
            var result = reader.Read();
            if (result.Snapshot is { } sample) return sample;
            Assert.IsTrue(result.Status is TelemetryReadStatus.Busy or TelemetryReadStatus.Changed or TelemetryReadStatus.Uninitialized,
                result.Status + ": " + result.Reason);
            await Task.Delay(1);
        }
        throw new AssertFailedException("No coherent snapshot within the test deadline.");
    }

    private sealed class NativePublisher(Process process) : IDisposable
    {
        public Process Process { get; } = process;
        public static async Task<NativePublisher> StartAsync(params string[] args)
        {
            var start = new ProcessStartInfo(PublisherPath) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
            foreach (string arg in args) start.ArgumentList.Add(arg);
            var publisher = new NativePublisher(Process.Start(start)!);
            try { await publisher.WaitForLine("PROCESS "); return publisher; }
            catch { publisher.Dispose(); throw; }
        }
        public async Task WaitForLine(string prefix, TimeSpan? timeout = null)
        {
            while (true)
            {
                string? line = await Process.StandardOutput.ReadLineAsync().WaitAsync(timeout ?? TimeSpan.FromSeconds(5));
                if (line == null) throw new AssertFailedException("Native publisher exited: " + await Process.StandardError.ReadToEndAsync());
                if (line.StartsWith(prefix, StringComparison.Ordinal)) return;
            }
        }
        public void Stop()
        {
            if (Process.HasExited) return;
            Process.Kill();
            Assert.IsTrue(Process.WaitForExit(3000), "Synthetic helper did not exit.");
        }
        public void Dispose() { Stop(); Process.Dispose(); }
    }
}
