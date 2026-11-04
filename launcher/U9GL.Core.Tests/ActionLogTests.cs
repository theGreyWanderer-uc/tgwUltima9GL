using U9GL.Core.Install;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class ActionLogTests
{
    private string _dir = "";

    [TestInitialize]
    public void CreateFolder()
    {
        _dir = Path.Combine(Path.GetTempPath(), "u9gl-core-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_dir);
    }

    [TestCleanup]
    public void DeleteFolder()
    {
        try { Directory.Delete(_dir, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    [TestMethod]
    public void LinesAreTimestampedAndAppended()
    {
        var clock = new DateTime(2026, 9, 14, 18, 42, 4);
        var log = new ActionLog(Path.Combine(_dir, "u9gl", "launcher.log"), () => clock);
        log.Write("Install");
        log.Write("  installed the loader\nwrote u9gl.ini");

        CollectionAssert.AreEqual(
            new[]
            {
                "2026-09-14 18:42:04  Install",
                "2026-09-14 18:42:04    installed the loader",
                "2026-09-14 18:42:04  wrote u9gl.ini",
            },
            File.ReadAllLines(log.Path));
    }

    [TestMethod]
    public void ALargeLogIsRotated()
    {
        var log = new ActionLog(Path.Combine(_dir, "launcher.log"), maxBytes: 200);
        for (int i = 0; i < 20; i++) log.Write(new string('x', 40));

        Assert.IsTrue(File.Exists(log.Path + ".1"));
        Assert.IsTrue(new FileInfo(log.Path).Length < 400);
    }

    [TestMethod]
    public void AnUnwritableLogNeverThrows()
    {
        string notAFolder = Path.Combine(_dir, "a-file");
        File.WriteAllText(notAFolder, "");
        new ActionLog(Path.Combine(notAFolder, "launcher.log")).Write("still fine");
    }
}
