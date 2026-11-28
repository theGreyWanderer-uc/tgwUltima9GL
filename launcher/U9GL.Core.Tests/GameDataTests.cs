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

    private static GameDataManifest PatchManifest() => Manifest(
        $"static\ta.flx\t3\t{OneSha}\t1.19F",
        $"static\ta.flx\t3\t{TwoSha}\tFW 1.19.01",
        $"static\tb.flx\t3\t{OneSha}\tFW 1.19.01\tbackup");

    [TestMethod]
    public void AFileOnlyAPatchBringsIsNotMissedOnAStockInstall()
    {
        Write("a.flx", "one");

        var result = new GameDataChecker(PatchManifest()).Check(_root, "static");

        Assert.AreEqual(1, result.Total, "the backup is not one of the release's files");
        Assert.AreEqual(1, result.Ok);
        Assert.AreEqual(0, result.Missing);
        Assert.AreEqual(0, result.PatchSets().Count);
        Assert.AreEqual(1, PatchManifest().FileCount("static"));
    }

    [TestMethod]
    public void APatchedInstallNamesItsPatch()
    {
        Write("a.flx", "two");
        Write("b.flx", "one");

        var result = new GameDataChecker(PatchManifest()).Check(_root, "static");

        Assert.AreEqual(2, result.Patched);
        Assert.AreEqual(0, result.Extra, "the backup is known, not extra");
        Assert.IsTrue(result.AllKnownGood);
        Assert.IsTrue(result.Files.Single(f => f.Name == "b.flx").Backup);
        Assert.AreEqual("FW 1.19.01", GameDataPatch.Resolve(result.PatchSets()).Build);
    }

    [TestMethod]
    public void ALeftoverBackupDoesNotReadAsThePatch()
    {
        Write("a.flx", "one");
        Write("b.flx", "one");

        var result = new GameDataChecker(PatchManifest()).Check(_root, "static");

        Assert.AreEqual(1, result.Ok);
        Assert.AreEqual(1, result.Patched);
        Assert.AreEqual(0, result.PatchSets().Count);
    }

    [TestMethod]
    public void ABackupWithOtherContentsIsChanged()
    {
        Write("a.flx", "two");
        Write("b.flx", "two");

        var result = new GameDataChecker(PatchManifest()).Check(_root, "static");

        Assert.AreEqual(GameDataState.Changed, State(result, "b.flx"));
    }

    [TestMethod]
    public void ManifestReadsTheKindColumn()
    {
        Assert.IsTrue(PatchManifest().For("static", "b.flx").Single().Backup);
        Assert.IsFalse(PatchManifest().For("static", "a.flx").Any(e => e.Backup));
        Assert.ThrowsExactly<InvalidDataException>(() =>
            GameDataManifest.Parse($"static\ta.flx\t3\t{OneSha}\t1.19F\tsomething\n"));
    }

    [TestMethod]
    public void TheEmbeddedManifestKnowsTheMusicFixPatch()
    {
        var manifest = GameDataManifest.Load();
        var triggers = manifest.For("static", "triggers.flx");
        Assert.AreEqual(1, triggers.Count(e => e.Build == GameDataManifest.BaseBuild));
        Assert.IsTrue(triggers.Any(e => e.Build == "FW 1.19.01" && e.Bytes == 242356 && !e.Backup));
        var backup = manifest.For("static", "triggers-backup.flx").Single();
        Assert.IsTrue(backup.Backup);
        Assert.AreEqual(triggers.Single(e => e.Build == GameDataManifest.BaseBuild).Sha256, backup.Sha256,
                        "the backup is the 1.19F triggers.flx");
    }

    [TestMethod]
    public void TheEmbeddedManifestKnowsTheCompanionPatch()
    {
        var manifest = GameDataManifest.Load();
        var g = manifest.Entries.Where(e => e.Build == "FW 1.19G").ToList();
        CollectionAssert.AreEquivalent(new[] { "static/triggers.flx", "static/activity.flx", "static/highway.dat", "runtime/nonfixed.9" },
            g.Select(e => $"{e.Folder}/{e.Name.ToLowerInvariant()}").ToArray());
        Assert.IsTrue(g.All(e => !e.Backup));
        foreach (var row in g)
            Assert.IsFalse(manifest.For(row.Folder, row.Name).Any(e => e.Sha256 == row.Sha256 &&
                           e.Build is GameDataManifest.BaseBuild or "FW 1.19.01"), row.Name);
    }

    private const string ThreeSha = "44778d82365e4af681c40d5f0eef5cf6f5899d3f0ac335050a7ed6779cf3f674";

    private static GameDataManifest SharedManifest() => Manifest(
        $"static\ta.flx\t3\t{OneSha}\t1.19F",
        $"static\ta.flx\t3\t{TwoSha}\tFW 1.19G",
        $"static\ta.flx\t3\t{ThreeSha}\tFW 1.19H",
        $"static\tb.flx\t3\t{OneSha}\t1.19F",
        $"static\tb.flx\t3\t{TwoSha}\tFW 1.19G",
        $"static\tb.flx\t3\t{TwoSha}\tFW 1.19H");

    private GameDataPatchVerdict Installed(string a, string b)
    {
        Write("a.flx", a);
        Write("b.flx", b);
        return GameDataPatch.Resolve(new GameDataChecker(SharedManifest()).Check(_root, "static").PatchSets());
    }

    [TestMethod]
    public void ASharedFileNamesEveryPatchThatShipsIt()
    {
        Write("a.flx", "one");
        Write("b.flx", "two");
        var b = new GameDataChecker(SharedManifest()).Check(_root, "static").Files.Single(f => f.Name == "b.flx");

        Assert.AreEqual(GameDataState.Patched, b.State);
        Assert.AreEqual("FW 1.19G", b.Build, "the oldest is the one named");
        CollectionAssert.AreEqual(new[] { "FW 1.19G", "FW 1.19H" }, b.Builds.ToArray());
    }

    [TestMethod]
    public void TheNewerPatchIsNamedWhenItSharesAFileWithTheOlder()
    {
        var verdict = Installed("six", "two");
        Assert.AreEqual(GameDataPatchKind.One, verdict.Kind);
        Assert.AreEqual("FW 1.19H", verdict.Build);
    }

    [TestMethod]
    public void TheOlderPatchIsStillNamedWhenItIsTheOneInstalled()
    {
        var verdict = Installed("two", "two");
        Assert.AreEqual(GameDataPatchKind.One, verdict.Kind);
        Assert.AreEqual("FW 1.19G", verdict.Build);
    }

    [TestMethod]
    public void FilesNoOnePatchShipsTogetherAreMixed()
    {
        Assert.AreEqual("FW 1.19H", Installed("six", "one").Build);

        var verdict = GameDataPatch.Resolve([["FW 1.19G"], ["FW 1.19H"], ["FW 1.19G", "FW 1.19H"]]);
        Assert.AreEqual(GameDataPatchKind.Mixed, verdict.Kind);
        CollectionAssert.AreEqual(new[] { "FW 1.19G", "FW 1.19H" }, verdict.Names.ToArray(), "the shared file adds no name");
    }

    [TestMethod]
    public void OnlySharedFilesPatchedNamesTheOlderPatch()
    {
        var verdict = Installed("one", "two");
        Assert.AreEqual(GameDataPatchKind.One, verdict.Kind);
        Assert.AreEqual("FW 1.19G", verdict.Build, "never the newer patch on a file it shares");
    }

    [TestMethod]
    public void NoPatchedFilesIsNone()
    {
        Assert.AreEqual(GameDataPatchKind.None, Installed("one", "one").Kind);
        Assert.AreEqual(GameDataPatchKind.None, GameDataPatch.Resolve([]).Kind);
    }

    private static IReadOnlyList<string> Set(params string[] builds) => builds;

    [TestMethod]
    public void PatchesOfDifferentFamiliesAreNotAMix()
    {
        var sets = new[] { Set("FW 1.19H"), Set("DP 1.5"), Set("DP 1.4", "DP 1.5", "Economy") };
        var all = GameDataPatch.ResolveAll(sets);
        CollectionAssert.AreEqual(new[] { "FW", "DP", "Economy" }, all.Select(p => p.Family).ToArray());
        Assert.AreEqual("FW 1.19H", all[0].Verdict.Build);
        Assert.AreEqual("DP 1.5", all[1].Verdict.Build);
        Assert.AreEqual(GameDataPatchKind.One, all[2].Verdict.Kind);
        Assert.AreEqual(GameDataPatchKind.None, GameDataPatch.ResolveFamily(sets, "Monster").Kind);
    }

    [TestMethod]
    public void AVersionOnlySharedFilesShowIsBothFits()
    {
        var verdict = GameDataPatch.ResolveFamily([Set("DP 1.4", "DP 1.5"), Set("DP 1.4", "DP 1.5", "Economy")], "DP");
        Assert.AreEqual(GameDataPatchKind.One, verdict.Kind);
        Assert.AreEqual("DP 1.4", verdict.Build);
        CollectionAssert.AreEqual(new[] { "DP 1.4", "DP 1.5" }, verdict.Fits.ToArray());
    }

    [TestMethod]
    public void FamilyAndVersionComeFromTheBuildName()
    {
        Assert.AreEqual("FW", GameDataPatch.FamilyOf("FW 1.19.01"));
        Assert.AreEqual("1.19.01", GameDataPatch.VersionOf("FW 1.19.01"));
        Assert.AreEqual("Economy", GameDataPatch.FamilyOf("Economy"));
        Assert.AreEqual("", GameDataPatch.VersionOf("Economy"));
    }

    [TestMethod]
    public void EveryPatchBuildInTheManifestHasAFamily()
    {
        var known = GameDataPatch.Families.Select(f => f.Id).ToHashSet();
        var builds = GameDataManifest.Load().Builds().Where(b => b != GameDataManifest.BaseBuild);
        foreach (string build in builds)
            Assert.IsTrue(known.Contains(GameDataPatch.FamilyOf(build)), $"{build} belongs to no family the launcher lists");
        foreach (var family in GameDataPatch.Families.Where(f => f.Latest != null))
            Assert.IsTrue(GameDataManifest.Load().Builds().Contains(family.Latest!), $"{family.Latest} has no manifest rows");
    }

    private static PatchedFile Pf(string folder, string name, params string[] builds) => new(folder, name, builds);

    private static List<PatchedFile> Fw(string nonfixed9Build) =>
    [
        Pf("static", "TRIGGERS.FLX", "FW 1.19H"),
        Pf("static", "activity.flx", "FW 1.19H"),
        Pf("static", "highway.dat", "FW 1.19G", "FW 1.19H"),
        Pf("runtime", "NONFIXED.22", "FW 1.19H"),
        Pf("runtime", "NONFIXED.9", nonfixed9Build),
    ];

    private static List<PatchedFile> Bb() =>
    [
        Pf("static", "bitmap16.flx", "BB 2011R3"),
        Pf("static", "sdInfo16.flx", "BB 2011R3"),
        Pf("static", "sappear.flx", "BB 2011R3"),
        Pf("static", "FIXED.9", "BB 2011R3"),
    ];

    [TestMethod]
    public void BbAfterFwReplacesFwsMapNine()
    {
        var replaced = GameDataPatch.Replaced(GameDataManifest.Load(), Fw("BB 2011R3").Concat(Bb()));
        var only = replaced.Single();
        Assert.AreEqual("FW", only.Family);
        Assert.AreEqual("BB", only.By);
        Assert.AreEqual("NONFIXED.9", only.Name);
        Assert.IsTrue(GameDataPatch.LostWhenReplaced.ContainsKey(("FW", "nonfixed.9")));
    }

    [TestMethod]
    public void FwAfterBbReplacesBbsMapNine()
    {
        var replaced = GameDataPatch.Replaced(GameDataManifest.Load(), Fw("FW 1.19H").Concat(Bb()));
        var only = replaced.Single();
        Assert.AreEqual("BB", only.Family);
        Assert.AreEqual("FW", only.By);
        Assert.IsTrue(GameDataPatch.LostWhenReplaced.ContainsKey(("BB", "nonfixed.9")));
    }

    [TestMethod]
    public void NothingReplacedWhenEachPatchHasItsOwnFiles()
    {
        var manifest = GameDataManifest.Load();
        Assert.AreEqual(0, GameDataPatch.Replaced(manifest, Bb().Append(Pf("runtime", "NONFIXED.9", "BB 2011R3"))).Count);
        Assert.AreEqual(0, GameDataPatch.Replaced(manifest, Fw("FW 1.19H")).Count);
        Assert.AreEqual(0, GameDataPatch.Replaced(manifest, Fw("FW 1.19H").Append(Pf("static", "text.flx", "DP 1.5"))).Count);
        Assert.AreEqual(0, GameDataPatch.Replaced(manifest, Fw("FW 1.19G")).Count);
    }

    [TestMethod]
    public void PatchNamesSortOldestFirst()
    {
        var names = new[] { "FW 1.19H", "FW 1.19.01", "FW 1.19G" }.Order(GameDataPatch.Oldest).ToArray();
        CollectionAssert.AreEqual(new[] { "FW 1.19.01", "FW 1.19G", "FW 1.19H" }, names);
    }

    [TestMethod]
    public void TheEmbeddedManifestKnowsTheLatestPatch()
    {
        var manifest = GameDataManifest.Load();
        var h = manifest.Entries.Where(e => e.Build == "FW 1.19H").ToList();
        CollectionAssert.AreEquivalent(
            new[] { "static/triggers.flx", "static/activity.flx", "static/highway.dat", "runtime/nonfixed.9", "runtime/nonfixed.22" },
            h.Select(e => $"{e.Folder}/{e.Name.ToLowerInvariant()}").ToArray());
        foreach (var row in h)
        {
            var same = manifest.For(row.Folder, row.Name).Where(e => e.Sha256 == row.Sha256).Select(e => e.Build).ToList();
            if (row.Name.Equals("highway.dat", StringComparison.OrdinalIgnoreCase))
                CollectionAssert.AreEquivalent(new[] { "FW 1.19G", "FW 1.19H" }, same);
            else
                CollectionAssert.AreEqual(new[] { "FW 1.19H" }, same, row.Name);
        }
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
    public void TheCacheRemembersThePatch()
    {
        Write("a.flx", "two");
        Write("b.flx", "one");
        string cachePath = Path.Combine(_root, "datacheck.json");
        GameDataCache.Load(cachePath).Put(_root, new GameDataChecker(PatchManifest()).Check(_root, "static"));

        var entry = GameDataCache.Load(cachePath).Get(_root, "static")!;
        Assert.AreEqual(1, entry.PatchSets.Count);
        CollectionAssert.AreEqual(new[] { "FW 1.19.01" }, entry.PatchSets[0].ToArray());
        Assert.AreEqual(2, entry.Problems.Count(f => f.State == GameDataState.Patched), "patched rows are kept to show");
        Assert.IsTrue(entry.Problems.Single(f => f.Name == "b.flx").Backup);
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
