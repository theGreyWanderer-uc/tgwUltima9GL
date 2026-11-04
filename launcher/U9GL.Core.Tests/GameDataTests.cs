using System.Text;
using System.Text.Json;
using U9GL.Core.Data;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class GameDataTests
{
    private string _root = "";
    private string Static => Path.Combine(_root, "static");

    private const string OneSha = "7692c3ad3540bb803c020b3aee66cd8887123234ea0c6e7143c0add73ff431ed";
    private const string TwoSha = "3fc4ccfe745870e2c0d99f71f30ff0656c8dedd41cc1d7d3d376b0dbe685e2f3";

    [TestInitialize]
    public void SetUp()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-data-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Static);
    }

    [TestCleanup]
    public void TearDown() => Directory.Delete(_root, recursive: true);

    private static GameDataManifest Manifest(params string[] rows) =>
        GameDataManifest.Parse("# a note\n#folder\tfile\tbytes\tsha256\tbuild\n" + string.Join("\n", rows) + "\n");

    private void Write(string name, string text) => File.WriteAllText(Path.Combine(Static, name), text);

    [TestMethod]
    public void ManifestKeepsNotesAndAlternates()
    {
        var manifest = Manifest($"static\ta.flx\t3\t{OneSha}\t1.19F", $"static\ta.flx\t3\t{TwoSha}\t1.19H");
        CollectionAssert.AreEqual(new[] { "a note" }, manifest.Notes.ToArray());
        Assert.AreEqual(2, manifest.For("static", "A.FLX").Count, "matched without case");
        Assert.AreEqual(1, manifest.FileCount("static"), "two builds of one file are one file");
        CollectionAssert.AreEqual(new[] { "1.19F", "1.19H" }, manifest.Builds().ToArray());
    }

    [TestMethod]
    public void ManifestRejectsAMalformedRow()
    {
        Assert.ThrowsExactly<InvalidDataException>(() => GameDataManifest.Parse("static\ta.flx\tnot-a-size\tabc\t1.19F\n"));
        Assert.ThrowsExactly<InvalidDataException>(() => GameDataManifest.Parse("# only a comment\n"));
    }

    [TestMethod]
    public void TheEmbeddedManifestCoversEveryGroup()
    {
        var manifest = GameDataManifest.Load();
        foreach (var group in GameDataManifest.Groups)
            Assert.IsTrue(manifest.FileCount(group.Folder) > 0, $"{group.Folder} has entries");
        Assert.IsTrue(manifest.Builds().Contains("1.19F"));
        Assert.IsTrue(manifest.Notes.Count > 0, "provenance is recorded");
        Assert.IsTrue(manifest.Entries.All(e => e.Sha256.Length == 64 && e.Bytes > 0));
        var root = manifest.InFolder(".").Select(e => e.Name.ToLowerInvariant()).ToList();
        CollectionAssert.Contains(root, "u9.exe");
        foreach (string unwanted in new[] { "options.ini", "manual.pdf", "unins000.exe", "goglog.ini", "keymap.new" })
            CollectionAssert.DoesNotContain(root, unwanted);
    }

    [TestMethod]
    public void TheGameFolderGroupIgnoresFilesTheReleaseDidNotShip()
    {
        var manifest = Manifest($".\tu9.exe\t3\t{OneSha}\t1.19F");
        File.WriteAllText(Path.Combine(_root, "u9.exe"), "one");
        File.WriteAllText(Path.Combine(_root, "dsound.dll"), "ours");
        Directory.CreateDirectory(Path.Combine(_root, "u9gl"));

        var result = new GameDataChecker(manifest).Check(_root, ".");

        Assert.AreEqual(1, result.Ok);
        Assert.AreEqual(0, result.Extra, "a file beside the game is not reported");
        Assert.AreEqual(0, result.Subfolders.Count);
        Assert.IsTrue(result.AllKnownGood);
    }

    [TestMethod]
    public void FilesAreOkChangedMissingOrExtra()
    {
        var manifest = Manifest(
            $"static\tgood.flx\t3\t{OneSha}\t1.19F",
            $"static\tedited.flx\t3\t{OneSha}\t1.19F",
            $"static\tresized.flx\t3\t{OneSha}\t1.19F",
            $"static\tgone.flx\t3\t{OneSha}\t1.19F");
        Write("good.flx", "one");
        Write("edited.flx", "two");
        Write("resized.flx", "one and a half");
        Write("stray.flx", "hello");

        var result = new GameDataChecker(manifest).Check(_root, "static");

        Assert.AreEqual(4, result.Total);
        Assert.AreEqual(1, result.Ok);
        Assert.AreEqual(2, result.Changed);
        Assert.AreEqual(1, result.Missing);
        Assert.AreEqual(1, result.Extra);
        Assert.IsFalse(result.AllKnownGood);
        Assert.AreEqual(GameDataState.Ok, State(result, "good.flx"));
        Assert.AreEqual(GameDataState.Changed, State(result, "edited.flx"));
        Assert.AreEqual(GameDataState.Missing, State(result, "gone.flx"));
        Assert.AreEqual(GameDataState.Extra, State(result, "stray.flx"));
        Assert.AreEqual(6, result.BytesRead, "read good.flx and edited.flx only");
    }

    [TestMethod]
    public void AnAlternateBuildIsKnownGoodAndNamed()
    {
        var manifest = Manifest($"static\ta.flx\t3\t{OneSha}\t1.19F", $"static\ta.flx\t3\t{TwoSha}\t1.19H");
        Write("a.flx", "two");

        var result = new GameDataChecker(manifest).Check(_root, "static");

        Assert.AreEqual(1, result.Patched);
        Assert.AreEqual(0, result.Changed);
        Assert.IsTrue(result.AllKnownGood, "a patched file is known good");
        Assert.AreEqual("1.19H", result.Files.Single().Build);
    }

    [TestMethod]
    public void AMissingFolderIsReportedWithoutReadingAnything()
    {
        var manifest = Manifest($"sound\ta.flx\t3\t{OneSha}\t1.19F");
        var result = new GameDataChecker(manifest).Check(_root, "sound");

        Assert.IsFalse(result.Exists);
        Assert.AreEqual(1, result.Missing);
        Assert.AreEqual(0, result.BytesRead);
        Assert.AreEqual("absent", result.Fingerprint);
    }

    [TestMethod]
    public void SubfoldersAreNamedAndNotWalked()
    {
        var manifest = Manifest($"static\ta.flx\t3\t{OneSha}\t1.19F");
        Write("a.flx", "one");
        Directory.CreateDirectory(Path.Combine(Static, "v1.19F_originals"));
        File.WriteAllText(Path.Combine(Static, "v1.19F_originals", "a.flx"), "two");

        var result = new GameDataChecker(manifest).Check(_root, "static");

        Assert.AreEqual(1, result.Ok);
        Assert.AreEqual(0, result.Extra, "a file in a subfolder is not an extra file");
        CollectionAssert.AreEqual(new[] { "v1.19F_originals" }, result.Subfolders.ToArray());
    }

    [TestMethod]
    public void ProgressCountsEveryFileAndCancellingStops()
    {
        var manifest = Manifest($"static\ta.flx\t3\t{OneSha}\t1.19F", $"static\tb.flx\t3\t{OneSha}\t1.19F");
        Write("a.flx", "one");
        Write("b.flx", "one");
        var seen = new List<GameDataProgress>();
        var progress = new Progress<GameDataProgress>(seen.Add);

        var result = new GameDataChecker(manifest).Check(_root, "static", progress);
        Assert.AreEqual(2, result.Ok);

        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        Assert.ThrowsExactly<OperationCanceledException>(
            () => new GameDataChecker(manifest).Check(_root, "static", null, cancelled.Token));
    }

    [TestMethod]
    public void TheFingerprintFollowsTheFolder()
    {
        Write("a.flx", "one");
        string before = GameDataChecker.Fingerprint(Static);

        Write("a.flx", "two");
        File.SetLastWriteTimeUtc(Path.Combine(Static, "a.flx"), DateTime.UtcNow.AddMinutes(1));
        Assert.AreNotEqual(before, GameDataChecker.Fingerprint(Static), "an edited file changes it");

        Write("b.flx", "one");
        Assert.AreNotEqual(before, GameDataChecker.Fingerprint(Static), "so does a new file");
    }

    [TestMethod]
    public void ARememberedResultIsUsedOnlyWhileTheFolderIsUnchanged()
    {
        var manifest = Manifest($"static\ta.flx\t3\t{OneSha}\t1.19F");
        Write("a.flx", "one");
        string cachePath = Path.Combine(_root, "datacheck.json");
        var result = new GameDataChecker(manifest).Check(_root, "static");

        var cache = GameDataCache.Load(cachePath);
        cache.Put(_root, result);

        var reloaded = GameDataCache.Load(cachePath);
        var entry = reloaded.Get(_root, "static");
        Assert.IsNotNull(entry);
        Assert.AreEqual(1, entry.Ok);
        Assert.IsTrue(GameDataCache.IsCurrent(entry, _root, manifest.Id));
        Assert.IsFalse(GameDataCache.IsCurrent(entry, _root, "another-manifest"),
                       "a launcher update can change what known good means");

        Write("a.flx", "two");
        Assert.IsFalse(GameDataCache.IsCurrent(entry, _root, manifest.Id), "an edited folder makes it stale");
    }

    [TestMethod]
    public void TheCacheKeepsOneEntryPerFolderAndSurvivesABadFile()
    {
        string cachePath = Path.Combine(_root, "datacheck.json");
        File.WriteAllText(cachePath, "{ not json", Encoding.UTF8);
        var cache = GameDataCache.Load(cachePath);
        Assert.IsNull(cache.Get(_root, "static"));

        var manifest = Manifest($"static\ta.flx\t3\t{OneSha}\t1.19F");
        Write("a.flx", "one");
        var checker = new GameDataChecker(manifest);
        cache.Put(_root, checker.Check(_root, "static"));
        Write("a.flx", "two");
        cache.Put(_root, checker.Check(_root, "static"));

        var reloaded = GameDataCache.Load(cachePath);
        Assert.AreEqual(1, reloaded.Get(_root, "static")!.Changed, "the newer result replaced the older one");
    }

    [TestMethod]
    public void TheCacheRemembersWhatWasWrongWithEachFile()
    {
        var manifest = Manifest($"static	a.flx	3	{OneSha}	1.19F", $"static	b.flx	3	{OneSha}	1.19F");
        Write("a.flx", "changed");
        string cachePath = Path.Combine(_root, "datacheck.json");
        GameDataCache.Load(cachePath).Put(_root, new GameDataChecker(manifest).Check(_root, "static"));

        var problems = GameDataCache.Load(cachePath).Get(_root, "static")!.Problems;
        var a = problems.Single(f => f.Name == "a.flx");
        Assert.AreEqual(GameDataState.Changed, a.State);
        Assert.AreEqual(7, a.ActualBytes);
        Assert.AreEqual(GameDataState.Missing, problems.Single(f => f.Name == "b.flx").State);
    }

    [TestMethod]
    public void ACacheOfNamesAloneIsDropped()
    {
        string cachePath = Path.Combine(_root, "datacheck.json");
        File.WriteAllText(cachePath, $$"""[{ "Root": {{JsonSerializer.Serialize(_root)}}, "Folder": "static", "Problems": ["a.flx"] }]""");
        Assert.IsNull(GameDataCache.Load(cachePath).Get(_root, "static"));
    }

    private static GameDataState State(GameDataFolderResult result, string name) =>
        result.Files.Single(f => string.Equals(f.Name, name, StringComparison.OrdinalIgnoreCase)).State;
}
