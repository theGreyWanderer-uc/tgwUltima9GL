using U9GL.Core.Saves;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class NewGameSnapshotTests
{
    private string _root = "";
    private string Game => Path.Combine(_root, "game");
    private string Holding => Path.Combine(_root, "holding");
    private string Saves => Path.Combine(Game, "savegame");

    [TestInitialize]
    public void SetUp()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-snapshot-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Saves);
        Directory.CreateDirectory(Path.Combine(Game, "static"));
    }

    [TestCleanup]
    public void TearDown()
    {
        foreach (var f in new DirectoryInfo(_root).EnumerateFiles("*", SearchOption.AllDirectories)) f.Attributes = FileAttributes.Normal;
        Directory.Delete(_root, recursive: true);
    }

    private void Save(string name, string text) => File.WriteAllText(Path.Combine(Saves, name), text);

    [TestMethod]
    public void SetAsideEmptiesTheLooseFilesAndKeepsArchives()
    {
        Save("start.dat", "U9.008 slot 3");
        Save("nonfixed.14", "the old game's house");
        Save("u9game3.sav", "an archive");

        string folder = NewGameSnapshot.SetAside(Game, Holding);

        CollectionAssert.AreEquivalent(new[] { "u9game3.sav" }, Directory.GetFiles(Saves).Select(Path.GetFileName).ToArray());
        Assert.AreEqual("the old game's house", File.ReadAllText(Path.Combine(folder, "working", "nonfixed.14")));
        CollectionAssert.AreEqual(new[] { folder }, NewGameSnapshot.Unrestored(Holding, Game).ToArray());
    }

    [TestMethod]
    public void RestorePutsTheFilesBackAndKeepsWhatTheRunLeft()
    {
        Save("start.dat", "U9.008 slot 3");
        Save("nonfixed.14", "the old game's house");
        string folder = NewGameSnapshot.SetAside(Game, Holding);
        Save("nonfixed.14", "the new game's house");
        Save("processes.dat", "the run's");

        NewGameSnapshot.Restore(folder);

        Assert.AreEqual("U9.008 slot 3", File.ReadAllText(Path.Combine(Saves, "start.dat")));
        Assert.AreEqual("the old game's house", File.ReadAllText(Path.Combine(Saves, "nonfixed.14")));
        Assert.IsFalse(File.Exists(Path.Combine(Saves, "processes.dat")), "a file the run made is not left in savegame");
        Assert.AreEqual("the run's", File.ReadAllText(Path.Combine(folder, "from-the-run", "processes.dat")));
        Assert.AreEqual(0, NewGameSnapshot.Unrestored(Holding, Game).Count);

        NewGameSnapshot.Restore(folder);
        Assert.AreEqual("the old game's house", File.ReadAllText(Path.Combine(Saves, "nonfixed.14")));
    }

    [TestMethod]
    public void UnrestoredIsPerGameFolder()
    {
        Save("start.dat", "x");
        NewGameSnapshot.SetAside(Game, Holding);
        Assert.AreEqual(0, NewGameSnapshot.Unrestored(Holding, Path.Combine(_root, "another game")).Count);
    }

    [TestMethod]
    public void ResultFile()
    {
        string bundle = Path.Combine(Game, "u9gl");
        Directory.CreateDirectory(bundle);
        Assert.IsNull(NewGameSnapshot.ReadResult(bundle));
        File.WriteAllText(Path.Combine(bundle, NewGameSnapshot.ResultName), "ok\r\nwritten and checked\r\n");
        Assert.AreEqual(new SnapshotCheck(true, "written and checked"), NewGameSnapshot.ReadResult(bundle));
        File.WriteAllText(Path.Combine(bundle, NewGameSnapshot.ResultName), "failed\r\nno ankh\r\n");
        Assert.IsFalse(NewGameSnapshot.ReadResult(bundle)!.Ok);
    }

    [TestMethod]
    public void CheckWantsMaps0And14()
    {
        Assert.IsFalse(NewGameSnapshot.Check(Game).Ok);
        var bytes = new List<byte>("U9:008"u8.ToArray());
        void I32(int v) => bytes.AddRange(BitConverter.GetBytes(v));
        I32(0); I32(0);
        bytes.AddRange(new byte[52]);
        I32(0); I32(0); I32(0);
        I32(0); I32(0);
        I32(-1);
        File.WriteAllBytes(NewGameSnapshot.SnapshotPath(Game), bytes.ToArray());
        var check = NewGameSnapshot.Check(Game);
        Assert.IsFalse(check.Ok);
        StringAssert.Contains(check.Text, "map 14");
    }

    [TestMethod]
    public void TheInstalledSnapshotPasses()
    {
        const string game = @"C:\Ultima\Ultima9";
        if (!NewGameSnapshot.Exists(game)) Assert.Inconclusive("no snapshot installed here");
        var check = NewGameSnapshot.Check(game);
        Assert.IsTrue(check.Ok, check.Text);
    }
}
