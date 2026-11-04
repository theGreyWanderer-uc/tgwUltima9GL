using U9GL.WebDashboard.Telemetry;

namespace U9GL.WebDashboard.Tests;

[TestClass]
public sealed class GameLogFollowerTests
{
    private string _dir = "";

    [TestInitialize]
    public void Create() { _dir = Path.Combine(Path.GetTempPath(), "u9gl-log-test-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(_dir); }

    [TestCleanup]
    public void Remove() { try { Directory.Delete(_dir, recursive: true); } catch (IOException) { } }

    private string LogPath => Path.Combine(_dir, "u9log.txt");
    private static void Append(string path, string text) => File.AppendAllText(path, text);

    [TestMethod]
    public void LinesBecomeTaggedEventsWithContinuationsAndPartialLinesWait()
    {
        var log = new GameLogFollower(LogPath);
        File.WriteAllText(LogPath, "u9log attached  pid=4242  version 0.5.3+abc\nCFG  loaded u9gl.ini\nINLINE 0x00649350 hooked\n  spell vtable 0x00755C30\nF 000000 slot48 state 0\nTELEMETRY unavailable: mapping install fa");
        log.Poll();
        var page = log.Page(0);
        Assert.HasCount(4, page.Events);
        Assert.AreEqual("FRAME", page.Events[3].Tag);
        Assert.AreEqual("-", page.Events[0].Tag);
        Assert.AreEqual("CFG", page.Events[1].Tag);
        Assert.AreEqual("INLINE", page.Events[2].Tag);
        Assert.AreEqual("  spell vtable 0x00755C30", page.Events[2].Detail);
        Assert.AreEqual(4242u, page.Summary.RunPid);
        Assert.AreEqual("0.5.3+abc", page.Summary.RunVersion);
        Append(LogPath, "iled, winerr=5\nMETRICS floor is RISING +2.00 KB/frame\n");
        log.Poll();
        var more = log.Page(page.Next);
        Assert.HasCount(2, more.Events);
        Assert.AreEqual("TELEMETRY unavailable: mapping install failed, winerr=5", more.Events[0].Text);
        Assert.AreEqual("warning", more.Events[0].Level);
        Assert.AreEqual("warning", more.Events[1].Level);
        Assert.AreEqual(2, more.Summary.Warnings);
        Assert.IsEmpty(log.Page(more.Next).Events);
    }

    [TestMethod]
    public void CrashReportFaultBlocksArriveThroughTheLogsPointer()
    {
        string crash = Path.Combine(_dir, "u9diag_test.txt");
        File.WriteAllText(crash, "CRASH report for pid 4242\nframe 0: alive, no exceptions seen\n");
        File.WriteAllText(LogPath, $"u9log attached  pid=4242  version 0.5.3\nCRASH diagnostics -> {crash}\n");
        var log = new GameLogFollower(LogPath);
        log.Poll();
        Assert.AreEqual(crash, log.Summary().CrashReport);
        Assert.AreEqual(0, log.Summary().Faults);
        Append(crash, "========== FAULT: ACCESS_VIOLATION (0xC0000005) ==========\nat 0x005C3FA0 = u9-laa.exe+0x1C3FA0\n  EAX=00000143\n========== end of fault report ==========\nframe 300: alive\n");
        log.Poll();
        var fault = log.Page(0).Events.Single(e => e.Source == "crash");
        Assert.AreEqual("FAULT", fault.Tag);
        Assert.AreEqual("fault", fault.Level);
        Assert.AreEqual("FAULT: ACCESS_VIOLATION (0xC0000005)", fault.Text);
        Assert.AreEqual("at 0x005C3FA0 = u9-laa.exe+0x1C3FA0\n  EAX=00000143", fault.Detail);
        Assert.AreEqual(1, log.Summary().Faults);
    }

    [TestMethod]
    public void ARecreatedLogStartsANewRunWithoutLosingEarlierEvents()
    {
        var log = new GameLogFollower(LogPath);
        File.WriteAllText(LogPath, "u9log attached  pid=1  version a\nGLPOST failed to compile\n");
        log.Poll();
        Assert.AreEqual(1, log.Summary().Warnings);
        File.Delete(LogPath);
        File.WriteAllText(LogPath, "u9log attached  pid=2  version b\n");
        File.SetCreationTimeUtc(LogPath, DateTime.UtcNow.AddMinutes(1));
        log.Poll();
        var summary = log.Summary();
        Assert.AreEqual(2u, summary.RunPid);
        Assert.AreEqual(0, summary.Warnings);
        var events = log.Page(0).Events;
        Assert.HasCount(3, events);
        Assert.AreEqual(2u, events[^1].RunPid);
    }

    [TestMethod]
    public void EventsAreBoundedAndPagesAreLimited()
    {
        var log = new GameLogFollower(LogPath);
        File.WriteAllText(LogPath, string.Concat(Enumerable.Range(0, GameLogFollower.EventLimit + 100).Select(i => $"TAG line {i}\n")));
        log.Poll();
        var first = log.Page(0, 10_000);
        Assert.HasCount(GameLogFollower.PageLimit, first.Events);
        Assert.AreEqual(101, first.Oldest);
        Assert.AreEqual($"TAG line 100", first.Events[0].Text);
        long after = first.Next; int total = first.Events.Length;
        while (log.Page(after) is { Events.Length: > 0 } next) { total += next.Events.Length; after = next.Next; }
        Assert.AreEqual(GameLogFollower.EventLimit, total);
    }

    [TestMethod]
    public void AMissingLogIsQuietAndTheSummaryReportsIt()
    {
        var log = new GameLogFollower(LogPath);
        log.Poll();
        var summary = log.Summary();
        Assert.AreEqual(0, summary.LastSeq);
        Assert.AreEqual(LogPath, summary.LogFile);
        Assert.IsNull(summary.LogGrewAgoMs);
    }
}
