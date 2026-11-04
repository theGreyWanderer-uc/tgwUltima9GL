using System.Text.Json;
using U9GL.Core.Saves;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class SaveBackupTests
{
    private static readonly DateTimeOffset Moment = new(2026, 10, 10, 2, 3, 4, 123, TimeSpan.Zero);
    private string _root = "";
    private string Save => Path.Combine(_root, "game", "savegame");
    private string Output => Path.Combine(_root, "backups");

    [TestInitialize]
    public void SetUp()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-backup-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(Save, "subfolder"));
        Directory.CreateDirectory(Path.Combine(_root, "game", "static"));
        foreach (var (name, text) in new[] { ("u9game4.sav", "slot four"), ("u9game5.sav", "slot five"), ("start.dat", "selector"),
                                             ("processes.dat", "processes"), ("nonfixed.9", "map nine"), ("nonfixed.240", "stray"),
                                             ("diary.txt", "diary"), ("notes.bak", "other") })
            File.WriteAllText(Path.Combine(Save, name), text);
        File.WriteAllText(Path.Combine(Save, "subfolder", "inner.txt"), "not copied");
    }

    [TestCleanup]
    public void TearDown() => Directory.Delete(_root, recursive: true);

    [TestMethod]
    public void BacksUpTheArchiveAndEveryLooseFileWithAManifest()
    {
        string folder = SaveBackup.Create(Path.Combine(Save, "u9game4.sav"), Save, Output, 4, Moment);
        Assert.AreEqual("u9-save-slot-4-20261010T020304123Z", Path.GetFileName(folder));
        Assert.AreEqual("slot four", File.ReadAllText(Path.Combine(folder, "archive", "u9game4.sav")));
        CollectionAssert.AreEquivalent(new[] { "diary.txt", "nonfixed.240", "nonfixed.9", "notes.bak", "processes.dat", "start.dat" },
            Directory.GetFiles(Path.Combine(folder, "working")).Select(Path.GetFileName).ToArray());
        Assert.IsFalse(File.Exists(Path.Combine(folder, "archive", "u9game5.sav")));
        Assert.IsFalse(Directory.Exists(Path.Combine(folder, "working", "subfolder")));

        using var manifest = JsonDocument.Parse(File.ReadAllText(Path.Combine(folder, "manifest.json")));
        var root = manifest.RootElement;
        Assert.AreEqual(1, root.GetProperty("schema_version").GetInt32());
        Assert.IsTrue(root.GetProperty("complete").GetBoolean());
        Assert.AreEqual(4, root.GetProperty("slot").GetInt32());
        Assert.IsTrue(root.GetProperty("start_dat_present").GetBoolean());
        Assert.IsTrue(root.GetProperty("working_files_are_current").GetBoolean());
        var files = root.GetProperty("files").EnumerateArray().ToList();
        Assert.AreEqual(7, files.Count);
        Assert.AreEqual("archive/u9game4.sav", files[0].GetProperty("path").GetString());
        Assert.AreEqual(64, files[0].GetProperty("sha256").GetString()!.Length);
        Assert.AreEqual(9, files[0].GetProperty("bytes").GetInt64());
    }

    [TestMethod]
    public void ASecondBackupInTheSameMillisecondGetsItsOwnFolder()
    {
        string first = SaveBackup.Create(Path.Combine(Save, "u9game4.sav"), Save, Output, 4, Moment);
        string second = SaveBackup.Create(Path.Combine(Save, "u9game4.sav"), Save, Output, 4, Moment);
        Assert.AreEqual(Path.GetFileName(first) + "_1", Path.GetFileName(second));
        Assert.IsTrue(File.Exists(Path.Combine(first, "manifest.json")));
    }

    [TestMethod]
    public void AMissingStartDatStaysMissing()
    {
        File.Delete(Path.Combine(Save, "start.dat"));
        string folder = SaveBackup.Create(Path.Combine(Save, "u9game4.sav"), Save, Output, 4, Moment);
        Assert.IsFalse(File.Exists(Path.Combine(folder, "working", "start.dat")));
        using var manifest = JsonDocument.Parse(File.ReadAllText(Path.Combine(folder, "manifest.json")));
        Assert.IsFalse(manifest.RootElement.GetProperty("start_dat_present").GetBoolean());
    }

    [TestMethod]
    public void BackupsNeverGoIntoTheGamesDataFolders()
    {
        string archive = Path.Combine(Save, "u9game4.sav");
        foreach (string output in new[] { Save, Path.Combine(Save, "backups"), Path.Combine(_root, "game", "static", "x"),
                                          Path.Combine(_root, "game", "runtime") })
            Assert.ThrowsExactly<SaveBackupException>(() => SaveBackup.Create(archive, Save, output, 4, Moment), output);
        Assert.IsFalse(Directory.Exists(Path.Combine(Save, "backups")));
    }

    [TestMethod]
    public void AMissingArchiveCannotBeBackedUp()
    {
        Assert.ThrowsExactly<SaveBackupException>(() => SaveBackup.Create(Path.Combine(Save, "u9game7.sav"), Save, Output, 7, Moment));
        Assert.IsFalse(Directory.Exists(Output));
    }
}
