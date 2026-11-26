using U9GL.Core.Saves;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class FanPatchTests
{
    private const int PageHeader = 0x60;
    private const int EntitySize = 0x20;
    private const uint BaseX = 33200, BaseY = 26300;
    private static readonly ShrineObject[] Valor = FanPatch.ShrineObjects.Where(o => o.Shrine == "Valor").ToArray();

    private string _root = "";

    [TestInitialize]
    public void SetUp()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-fanpatch-" + Guid.NewGuid().ToString("N"));
        foreach (string folder in new[] { "savegame", "static", "runtime" }) Directory.CreateDirectory(Path.Combine(_root, folder));
        File.WriteAllBytes(Path.Combine(_root, "static", "fixed.9"), SaveIntegrityTests.Fixed());
    }

    [TestCleanup]
    public void TearDown() => Directory.Delete(_root, recursive: true);

    private static byte[] Region(params (ushort Type, long X, long Y, ushort Z, ushort Link)[] objects)
    {
        byte[] page = new byte[0x1000];
        int[] cells = Enumerable.Range(0, 125).Select(i => PageHeader + i * EntitySize).ToArray();
        for (int i = 0; i < objects.Length; i++)
        {
            var (type, x, y, z, link) = objects[i];
            long localX = x - BaseX, localY = y - BaseY;
            Assert.IsTrue(localX is >= 0 and < 1024 && localY is >= 0 and < 1024, "one bucket keeps it simple");
            int at = cells[i];
            Put(page, at, i + 1 < objects.Length ? cells[i + 1] : 0);
            Put16(page, at + 4, (ushort)localX);
            Put16(page, at + 6, (ushort)localY);
            Put16(page, at + 8, z);
            Put16(page, at + 10, type);
            Put16(page, at + 18, 0x8000);
            Put16(page, at + 0x1A, link);
        }
        var free = cells.Skip(objects.Length).ToArray();
        for (int i = 0; i < free.Length; i++) Put(page, free[i], i + 1 < free.Length ? free[i + 1] : 0);
        Put(page, 0x04, free.Length > 0 ? free[0] : 0);
        Put(page, 0x0C, (int)BaseX);
        Put(page, 0x10, (int)BaseY);
        Put(page, 0x14, objects.Length);
        Put(page, 0x1C + 4, objects.Length > 0 ? cells[0] : 0);

        byte[] header = new byte[0x24];
        Put(header, 0x0C, page.Length);
        Put(header, 0x10, 0x00C00000);
        Put(header, 0x14, 1);
        Put(header, 0x18, 1);
        Put(header, 0x1C, 1);
        Put(header, 0x20, 1);
        return [.. header, 0, 0, 0, 0, .. page];
    }

    private static (ushort, long, long, ushort, ushort)[] Markers() =>
        FanPatch.CompanionMarkerLinks.Select((link, i) => (FanPatch.CompanionMarkerType, (long)BaseX + 500 + i, (long)BaseY + 500, (ushort)2900, link)).ToArray();

    private static (ushort, long, long, ushort, ushort)[] ValorAt(bool stock) =>
        Valor.Select(o => (o.TypeIndex, o.WorldX, o.WorldY, stock ? o.StockZ : o.PatchedZ, (ushort)1330)).ToArray();

    private static byte[] PatchedMap() => Region([.. Markers(), .. ValorAt(stock: false)]);
    private static byte[] StockMap() => Region(ValorAt(stock: true));

    private static FanPatchState StateOf(byte[] map, int number = 9) =>
        FanPatch.SaveState(new Dictionary<int, U9Nonfixed> { [number] = new(map) });

    [TestMethod]
    public void APatchedMapHasTheCompanionsAndLoweredRunes()
    {
        var state = StateOf(PatchedMap());
        Assert.IsTrue(state.Companions);
        CollectionAssert.AreEquivalent(Valor, state.LoweredShrines().ToArray());
        Assert.AreEqual(0, state.StockShrines().Count);
    }

    [TestMethod]
    public void AStockMapHasNeither()
    {
        var state = StateOf(StockMap());
        Assert.IsFalse(state.Companions);
        CollectionAssert.AreEquivalent(Valor, state.StockShrines().ToArray());
    }

    [TestMethod]
    public void NoMap9SaysNothingAboutCompanions()
    {
        var state = StateOf(StockMap(), number: 0);
        Assert.IsNull(state.Companions);
        Assert.AreEqual(0, state.ShrineHeights.Count);
    }

    [TestMethod]
    public void OneMarkerIsNotTheFix() => Assert.IsFalse(StateOf(Region(Markers()[0])).Companions);

    [TestMethod]
    public void AMovedRuneIsNeitherStockNorLowered()
    {
        var moved = ValorAt(stock: false).Select(o => (o.Item1, o.Item2, o.Item3, (ushort)(o.Item4 - 27), o.Item5)).ToArray();
        var state = StateOf(Region(moved));
        Assert.AreEqual(0, state.StockShrines().Count);
        Assert.AreEqual(0, state.LoweredShrines().Count);
    }

    [TestMethod]
    public void ShrineObjectsAreMatchedByPosition()
    {
        var dagger = Valor.Single(o => o.TypeIndex == 5288);
        var state = StateOf(Region((dagger.TypeIndex, dagger.WorldX + 40, dagger.WorldY, dagger.StockZ, 0)));
        Assert.AreEqual(0, state.ShrineHeights.Count);
    }

    [TestMethod]
    public void TheInstalledStateReadsRuntimeInAnyCase()
    {
        File.WriteAllBytes(Path.Combine(_root, "runtime", "NONFIXED.9"), PatchedMap());
        var state = FanPatch.InstalledState(Path.Combine(_root, "runtime"));
        Assert.IsTrue(state.Companions);
        Assert.IsTrue(FanPatch.InstalledState(Path.Combine(_root, "nowhere")).Companions is null);
    }

    [TestMethod]
    public void TheTableMatchesTitansMeasurements()
    {
        Assert.AreEqual(16, FanPatch.ShrineObjects.Count, "a Rune and an item at each of eight shrines");
        Assert.AreEqual(8, FanPatch.ShrineObjects.Select(o => o.Shrine).Distinct().Count());
        Assert.IsTrue(FanPatch.ShrineObjects.All(o => o.PatchedZ < o.StockZ), "1.19H only lowered them");
        Assert.IsTrue(FanPatch.ShrineObjects.Where(o => o.Shrine == "Spirituality").All(o => o.MapNumber == 22));
    }

    private void WriteSave(byte[] savedMap)
    {
        string save = Path.Combine(_root, "savegame");
        byte[] processes = SaveIntegrityTests.Processes();
        File.WriteAllBytes(Path.Combine(save, "start.dat"), SaveIntegrityTests.StartDat(4));
        File.WriteAllBytes(Path.Combine(save, "u9game4.sav"), SaveIntegrityTests.Archive(processes, savedMap));
        File.WriteAllBytes(Path.Combine(save, "processes.dat"), processes);
        File.WriteAllBytes(Path.Combine(save, "nonfixed.9"), savedMap);
    }

    private Dictionary<string, IntegrityFinding> Fwp(out IntegrityReport report)
    {
        report = new SaveIntegrityChecker().CheckSave(_root);
        return report.Findings.Where(f => f.CheckId.StartsWith("FWP", StringComparison.Ordinal)).ToDictionary(f => f.CheckId);
    }

    [TestMethod]
    public void AnOldSaveOnAPatchedInstallRaisesBoth()
    {
        File.WriteAllBytes(Path.Combine(_root, "runtime", "nonfixed.9"), PatchedMap());
        WriteSave(StockMap());

        var found = Fwp(out var report);

        CollectionAssert.AreEquivalent(new[] { "FWP01", "FWP02" }, found.Keys.ToArray());
        Assert.IsTrue(found.Values.All(f => f.Severity == Severity.Info));
        Assert.AreEqual("Valor", found["FWP02"].Details["shrine_names"]);
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Compatibility), "information, not damage");

        var cards = SaveWording.Shared.Describe(report).Cards;
        var one = cards.Single(c => c.Key == "FWP01/INFO");
        Assert.AreEqual("Started before the fan patch's companion fix", one.Title);
        var two = cards.Single(c => c.Key == "FWP02/INFO");
        StringAssert.Contains(two.Meanings.Single(), "Valor");
        StringAssert.Contains(two.WhatToDo, "start a new game");
    }

    [TestMethod]
    public void ASaveStartedOnThePatchRaisesNothing()
    {
        File.WriteAllBytes(Path.Combine(_root, "runtime", "nonfixed.9"), PatchedMap());
        WriteSave(PatchedMap());
        Assert.AreEqual(0, Fwp(out _).Count);
    }

    [TestMethod]
    public void AnOldSaveOnAStockInstallRaisesNothing()
    {
        File.WriteAllBytes(Path.Combine(_root, "runtime", "nonfixed.9"), StockMap());
        WriteSave(StockMap());
        Assert.AreEqual(0, Fwp(out _).Count);
    }

    [TestMethod]
    public void NoRuntimeFolderRaisesNothing()
    {
        Directory.Delete(Path.Combine(_root, "runtime"));
        WriteSave(StockMap());
        Assert.AreEqual(0, Fwp(out _).Count);
    }

    [TestMethod]
    public void EverySaveIsCountedAgainstTheInstall()
    {
        string save = Path.Combine(_root, "savegame"), runtime = Path.Combine(_root, "runtime");
        byte[] processes = SaveIntegrityTests.Processes();
        File.WriteAllBytes(Path.Combine(runtime, "nonfixed.9"), PatchedMap());
        File.WriteAllBytes(Path.Combine(save, "u9game3.sav"), SaveIntegrityTests.Archive(processes, StockMap()));
        File.WriteAllBytes(Path.Combine(save, "u9game12.sav"), SaveIntegrityTests.Archive(processes, PatchedMap()));
        File.WriteAllBytes(Path.Combine(save, "u9game20.sav"), "not a save"u8.ToArray());

        var result = FanPatch.CheckSaves(save, runtime);

        Assert.IsTrue(result.PatchInstalled);
        CollectionAssert.AreEqual(new[] { 3, 12 }, result.Saves.Select(s => s.Slot).ToArray());
        Assert.AreEqual(3, result.Partial.Single().Slot);
        Assert.IsTrue(result.Partial.Single().MissesCompanions);
        CollectionAssert.AreEqual(new[] { "Valor" }, result.Partial.Single().FloatingShrines.ToArray());
        Assert.AreEqual(1, result.Unreadable);

        File.WriteAllBytes(Path.Combine(runtime, "nonfixed.9"), StockMap());
        var stock = FanPatch.CheckSaves(save, runtime);
        Assert.IsFalse(stock.PatchInstalled);
        Assert.AreEqual(0, stock.Partial.Count, "nothing to miss on a stock install");
    }

    private static void Put(byte[] data, int offset, int value) => BitConverter.GetBytes(value).CopyTo(data, offset);
    private static void Put16(byte[] data, int offset, ushort value) => BitConverter.GetBytes(value).CopyTo(data, offset);
}
