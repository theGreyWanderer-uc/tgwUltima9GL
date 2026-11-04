using System.Text;
using U9GL.Core.Saves;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class SaveIntegrityTests
{
    private string _root = "";
    private string Save => Path.Combine(_root, "savegame");
    private string Static => Path.Combine(_root, "static");
    private string Reference => Path.Combine(_root, "reference");

    [TestInitialize]
    public void SetUp()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-save-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Save);
        Directory.CreateDirectory(Static);
        Directory.CreateDirectory(Reference);
        byte[] processes = Processes();
        byte[] nonfixed = Nonfixed();
        File.WriteAllBytes(Path.Combine(Save, "start.dat"), StartDat(4));
        File.WriteAllBytes(Path.Combine(Save, "u9game4.sav"), Archive(processes, nonfixed));
        File.WriteAllBytes(Path.Combine(Save, "processes.dat"), processes);
        File.WriteAllBytes(Path.Combine(Save, "nonfixed.9"), nonfixed);
        File.WriteAllBytes(Path.Combine(Static, "fixed.9"), Fixed());
        File.WriteAllBytes(Path.Combine(Reference, "fixed.9"), Fixed());
    }

    [TestCleanup]
    public void TearDown() => Directory.Delete(_root, recursive: true);

    [TestMethod]
    public void StartDatReadsTheSelectedSlot() => Assert.AreEqual(631, U9StartDat.FromBytes(StartDat(631)).Slot);

    [TestMethod]
    public void StartDatRejectsWrongSizeOrMagic()
    {
        Assert.ThrowsExactly<U9SaveException>(() => U9StartDat.FromBytes("U9.008"u8));
        Assert.ThrowsExactly<U9SaveException>(() => U9StartDat.FromBytes([.. "U9:008"u8, 1, 0, 0, 0]));
    }

    [TestMethod]
    public void ArchiveReadsHeaderAndExactMembers()
    {
        var archive = U9SaveArchive.FromBytes(TestArchive());
        Assert.AreEqual("Test save", archive.Header.Description);
        Assert.AreEqual(9, archive.Header.HeaderMap);
        Assert.AreEqual(700, archive.Header.Time);
        CollectionAssert.AreEqual("process"u8.ToArray(), archive.Processes.Data.ToArray());
        CollectionAssert.AreEqual("map-nine"u8.ToArray(), archive.Member("NONFIXED.9")!.Data.ToArray());
    }

    [TestMethod]
    public void ArchiveRejectsDuplicateMapsTrailingBytesAndTruncation()
    {
        var duplicate = Assert.ThrowsExactly<U9SaveException>(() => U9SaveArchive.FromBytes(TestArchive((9, "a"u8.ToArray()), (9, "b"u8.ToArray()))));
        StringAssert.Contains(duplicate.Message, "duplicate");
        var trailing = Assert.ThrowsExactly<U9SaveException>(() => U9SaveArchive.FromBytes([.. TestArchive(), (byte)'x']));
        StringAssert.Contains(trailing.Message, "trailing");
        byte[] whole = TestArchive();
        Assert.ThrowsExactly<U9SaveException>(() => U9SaveArchive.FromBytes(whole[..^5]));
    }

    [TestMethod]
    public void HeaderAloneComesFromTheFirstBytes()
    {
        byte[] whole = TestArchive();
        var header = U9SaveArchive.ReadHeader(whole[..80]);
        Assert.AreEqual("Test save", header.Description);
        Assert.AreEqual(2, header.Day);
    }

    [TestMethod]
    public void HandleTableParsesTheDynamicTableAndFreeChain()
    {
        var table = U9ItemHandleTable.FromBytes(HandleData());
        Assert.AreEqual(4, table.Count);
        CollectionAssert.AreEqual(new[] { 1, 2 }, table.WalkFreeChain().ToArray());
        Assert.AreEqual(0x1068, table.FixedEntries[0].ItemOffset);
        Assert.AreEqual(U9ItemHandleTable.HandleDataOffset + 60, table.EndOffset);
    }

    [TestMethod]
    public void HandleTableRequiresTheFollowingCameraBoundary()
    {
        byte[] data = HandleData();
        Put(data, data.Length - 4, 99);
        var error = Assert.ThrowsExactly<U9ProcessDataException>(() => U9ItemHandleTable.FromBytes(data));
        StringAssert.Contains(error.Message, "camera version");
    }

    [TestMethod]
    public void HandleTableRejectsABrokenFreeChain()
    {
        byte[] data = HandleData();
        Put(data, U9ItemHandleTable.HandleDataOffset + 12 + 2 * 12, 1);
        var error = Assert.ThrowsExactly<U9ProcessDataException>(() => U9ItemHandleTable.FromBytes(data).WalkFreeChain());
        StringAssert.Contains(error.Message, "cycles");
    }

    [TestMethod]
    public void HandleTableRejectsANegativeFreeLink()
    {
        byte[] data = HandleData();
        Put(data, U9ItemHandleTable.HandleDataOffset + 12 + 2 * 12, -1);
        var error = Assert.ThrowsExactly<U9ProcessDataException>(() => U9ItemHandleTable.FromBytes(data).WalkFreeChain());
        StringAssert.Contains(error.Message, "leaves table");
    }

    [TestMethod]
    public void GameFolderWithNoSaveFolderYetSelectsNothing()
    {
        Directory.Delete(Save, recursive: true);
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.AreEqual("SEL02", report.Findings.Single().CheckId);
        Assert.AreEqual(Save, report.Findings.Single().Path);
        Assert.IsTrue(report.NoSaveSelected);
    }

    [TestMethod]
    public void SlotAskedForKeepsItWhenTheSaveFolderIsGone()
    {
        Directory.Delete(Save, recursive: true);
        var report = new SaveIntegrityChecker().CheckSave(_root, slot: 4, fixedReferenceDirectory: Reference);
        Assert.AreEqual("SEL02", report.Findings.Single().CheckId);
        Assert.AreEqual(4, report.Slot);
        Assert.IsFalse(report.NoSaveSelected);
    }

    [TestMethod]
    public void MissingStartDatSelectsNothing()
    {
        File.Delete(Path.Combine(Save, "start.dat"));
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.AreEqual("SEL01", report.Findings.Single().CheckId);
        Assert.AreEqual(Severity.Fatal, report.Overall);
        Assert.IsTrue(report.NoSaveSelected);
    }

    [TestMethod]
    public void CorruptStartDatIsNotNoSave()
    {
        File.WriteAllBytes(Path.Combine(Save, "start.dat"), "U9.008"u8.ToArray());
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.AreEqual("SEL01", report.Findings.Single().CheckId);
        Assert.IsFalse(report.NoSaveSelected);
    }

    [TestMethod]
    public void CleanFixtureHasThreePassVerdicts()
    {
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Custody));
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Structure));
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Compatibility));
        Assert.IsTrue(report.Findings.Any(f => f.CheckId == "LAY01"));
        Assert.AreEqual(9, (int)report.Artifacts["archive"]["header_map"]!);
        string text = IntegrityText.Render(report);
        StringAssert.Contains(text, "Assessment: PASS - no integrity problems detected");
        StringAssert.Contains(text, "Custody       PASS");
        StringAssert.Contains(text, "Problems (0)\n  None");
        StringAssert.Contains(text, "Confirmations (1)");
        Assert.IsTrue(text.IndexOf("Problems", StringComparison.Ordinal) < text.IndexOf("Evidence", StringComparison.Ordinal));
        StringAssert.Contains(report.ToJson(), "\"check_id\": \"LAY01\"");
    }

    [TestMethod]
    public void MemberMismatchIsACustodyFailure()
    {
        File.WriteAllBytes(Path.Combine(Save, "processes.dat"), "different"u8.ToArray());
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.AreEqual(Severity.Fatal, report.Verdict(Axis.Custody));
        Assert.IsTrue(report.Findings.Any(f => f.CheckId == "CUS01"));
        string text = IntegrityText.Render(report);
        StringAssert.Contains(text, "Assessment: UNSAFE TO LOAD");
        StringAssert.Contains(text, "CUS01 / FATAL: Preserve the working files, then re-extract processes.dat");
        StringAssert.Contains(text, "Source:");
        var finding = report.Findings.Single(f => f.CheckId == "CUS01");
        Assert.AreEqual("processes.dat", finding.Details["member_name"]);
        Assert.AreEqual(0, ((List<int>)finding.Details["maps"]!).Count);
        StringAssert.Contains(report.ToJson(), "\"recommendations\": [");
    }

    [TestMethod]
    public void NoOriginalMapCopyIsANoteThatRaisesNoVerdict()
    {
        var report = new SaveIntegrityChecker().CheckSave(_root);
        var note = report.Findings.Single(f => f.CheckId == "LAY04");
        Assert.AreEqual(Severity.Warn, note.Severity);
        Assert.IsFalse(note.AffectsVerdict);
        Assert.AreEqual(Severity.Pass, report.Overall);
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Compatibility));
        CollectionAssert.AreEqual(new[] { 9 }, ((List<int>)note.Details["maps"]!).ToArray());
        string text = IntegrityText.Render(report);
        StringAssert.Contains(text, "Problems (0)\n  None");
        StringAssert.Contains(text, "Comparison notes (1)\n  WARN LAY04: no creation-time fixed reference directory was supplied");
        StringAssert.Contains(text, "LAY04 / WARN: An original map comparison is optional.");
        StringAssert.Contains(report.ToJson(), "\"affects_verdict\": false");
    }

    [TestMethod]
    public void AnActualMapDifferenceStillRaisesTheVerdict()
    {
        File.WriteAllBytes(Path.Combine(Reference, "fixed.9"), Fixed(liveSlot: 0));
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        var finding = report.Findings.Single(f => f.CheckId == "LAY02");
        Assert.IsTrue(finding.AffectsVerdict);
        Assert.AreEqual(Severity.Error, report.Overall);
        Assert.AreEqual(9, finding.Details["map_number"]);
    }

    [TestMethod]
    public void EveryFindingCarriesSlotCountAndMaps()
    {
        byte[] processes = Processes(nonfixedOffset: 0x2060);
        File.WriteAllBytes(Path.Combine(Save, "processes.dat"), processes);
        File.WriteAllBytes(Path.Combine(Save, "u9game4.sav"), Archive(processes, Nonfixed()));
        var report = new SaveIntegrityChecker().CheckSave(_root);
        foreach (var f in report.Findings)
        {
            Assert.AreEqual(4, f.Details["slot"], f.CheckId);
            Assert.IsInstanceOfType<int>(f.Details["count"], f.CheckId);
            Assert.IsInstanceOfType<List<int>>(f.Details["maps"], f.CheckId);
        }
        var bound = report.Findings.Single(f => f.CheckId == "REF02");
        Assert.AreEqual(1, bound.Details["count"]);
        CollectionAssert.AreEqual(new[] { 9 }, ((List<int>)bound.Details["maps"]!).ToArray());
        Assert.AreSame(bound.Details["handle_indices"], bound.Details["reference_indices"]);
    }

    [TestMethod]
    public void NextStepsAreKeyedByCheckAndSeverity()
    {
        byte[] nonfixed = Nonfixed();
        File.WriteAllBytes(Path.Combine(Save, "u9game4.sav"),
            Build("", "Integrity fixture", [0, 0, 0, 0, 0, 0, 1, 0, 0, 0], [9, 0, 0],
                  [[], Processes(), []], [(9, nonfixed), (240, nonfixed)]));
        File.WriteAllBytes(Path.Combine(Save, "nonfixed.240"), nonfixed);
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        var stray = report.Findings.Single(f => f.CheckId == "ARC10");
        CollectionAssert.AreEqual(new[] { 240 }, ((List<int>)stray.Details["maps"]!).ToArray());
        Assert.AreEqual(1, stray.Details["count"]);
        Assert.AreEqual(("ARC10", Severity.Warn), (report.Recommendations().Single().CheckId, report.Recommendations().Single().Severity));
        StringAssert.Contains(IntegrityText.Render(report), "  ARC10 / WARN: Keep a backup, then try loading");
    }

    [TestMethod]
    public void TheInstalledMapsAsTheirOwnReferenceAreASelfReference()
    {
        Assert.IsTrue(new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Static + Path.DirectorySeparatorChar).SelfReference);
        Assert.IsFalse(new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference).SelfReference);
        Assert.IsFalse(new SaveIntegrityChecker().CheckSave(_root).SelfReference);
    }

    [TestMethod]
    public void AnOlderSlotsUnreadableArchiveIsAStructureFailure()
    {
        File.WriteAllBytes(Path.Combine(Save, "u9game2.sav"), "not a save"u8.ToArray());
        var older = new SaveIntegrityChecker().CheckArchive(Save, 2, fixedReferenceDirectory: Reference);
        Assert.AreEqual(Axis.Structure, older.Findings.Single().Axis);
        Assert.AreEqual(Severity.Fatal, older.Verdict(Axis.Structure));
        Assert.AreEqual(Severity.NotApplicable, older.Verdict(Axis.Custody));
        Assert.AreEqual(Severity.Fatal, older.Overall);

        File.WriteAllBytes(Path.Combine(Save, "u9game4.sav"), "not a save"u8.ToArray());
        var selected = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.AreEqual(Axis.Custody, selected.Findings.Single().Axis);
    }

    [TestMethod]
    public void RendererCollapsesDuplicateProblemsButKeepsEveryFinding()
    {
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        report.Add("NFS07", Axis.Structure, Severity.Warn, "same issue", "archive");
        report.Add("NFS07", Axis.Structure, Severity.Warn, "same issue", "working");
        string text = IntegrityText.Render(report);
        StringAssert.Contains(text, "Problems (1 distinct, 2 findings)");
        Assert.AreEqual(1, CountOf(text, "NFS07 structure: same issue"));
        StringAssert.Contains(text, "Source: archive");
        StringAssert.Contains(text, "Source: working");
        Assert.AreEqual(2, report.Findings.Count(f => f.CheckId == "NFS07"));
    }

    [TestMethod]
    public void MapMemberAboveTheShippedRangeWarnsInsteadOfFailing()
    {
        byte[] nonfixed = Nonfixed();
        File.WriteAllBytes(Path.Combine(Save, "u9game4.sav"),
            Build("", "Integrity fixture", [0, 0, 0, 0, 0, 0, 1, 0, 0, 0], [9, 0, 0],
                  [[], Processes(), []], [(9, nonfixed), (240, nonfixed)]));
        File.WriteAllBytes(Path.Combine(Save, "nonfixed.240"), nonfixed);

        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.IsTrue(report.Findings.Any(f => f.CheckId == "ARC10"));
        Assert.IsFalse(report.Findings.Any(f => f.CheckId == "ARC01"));
        Assert.IsFalse(report.Findings.Any(f => f.CheckId == "REF01"));
        Assert.AreEqual(Severity.Warn, report.Verdict(Axis.Custody));
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Structure));
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Compatibility));
    }

    [TestMethod]
    public void MissingFixedMapIsACompatibilityFailure()
    {
        File.Delete(Path.Combine(Static, "fixed.9"));
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.AreEqual(Severity.Fatal, report.Verdict(Axis.Compatibility));
        Assert.IsTrue(report.Findings.Any(f => f.CheckId == "REF01"));
    }

    [TestMethod]
    public void PartialBundleRecordsOmissionsWithoutFailingCustody()
    {
        File.Delete(Path.Combine(Save, "nonfixed.9"));
        File.Delete(Path.Combine(Static, "fixed.9"));
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference, allowPartial: true);
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Custody));
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Compatibility));
        Assert.IsTrue(report.Findings.Any(f => f.CheckId == "CUS07"));
        Assert.IsTrue(report.Findings.Any(f => f.CheckId == "REF08"));
    }

    [TestMethod]
    public void FixedHandleResolvesALiveSlotAndRejectsAFreeOne()
    {
        byte[] fixedMap = Fixed(liveSlot: 0);
        File.WriteAllBytes(Path.Combine(Static, "fixed.9"), fixedMap);
        File.WriteAllBytes(Path.Combine(Reference, "fixed.9"), fixedMap);
        foreach (var (offset, rejected) in new[] { (0x60, false), (0x78, true) })
        {
            byte[] processes = Processes(fixedOffset: offset);
            File.WriteAllBytes(Path.Combine(Save, "processes.dat"), processes);
            File.WriteAllBytes(Path.Combine(Save, "u9game4.sav"), Archive(processes, Nonfixed()));
            var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
            var finding = report.Findings.FirstOrDefault(f => f.CheckId == "REF03");
            if (!rejected)
            {
                Assert.IsNull(finding);
                continue;
            }
            Assert.IsNotNull(finding);
            var record = IntegrityText.TargetRecords([finding]).Single();
            CollectionAssert.AreEqual(new[] { 2 }, ((List<int>)record["handles"]!).ToArray());
            CollectionAssert.AreEqual(new long[] { 0, 0 }, (long[])record["page"]!);
            CollectionAssert.AreEqual(new long[] { 0, 0, 0 }, (long[])record["xyz"]!);
            Assert.AreEqual("00000", record["status_hex"]);
            StringAssert.Contains(IntegrityText.Render(report), "Evidence: handle(2) fixed.9 offset 0x78 slot 1 page(0,0) xyz(0,0,0) type 0");
        }
    }

    [TestMethod]
    public void NonfixedHandleBeyondThePayloadIsOutOfBounds()
    {
        byte[] processes = Processes(nonfixedOffset: 0x2060);
        File.WriteAllBytes(Path.Combine(Save, "processes.dat"), processes);
        File.WriteAllBytes(Path.Combine(Save, "u9game4.sav"), Archive(processes, Nonfixed()));
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.IsTrue(report.Findings.Any(f => f.CheckId == "REF02"));
        Assert.IsFalse(report.Findings.Any(f => f.CheckId == "REF04"));
    }

    [TestMethod]
    public void AnOlderSlotIsCheckedFromItsArchiveAloneWithCustodyNotApplicable()
    {
        byte[] processes = Processes(fixedOffset: null);
        processes[8] = 1;
        File.WriteAllBytes(Path.Combine(Save, "u9game2.sav"), Archive(processes, Nonfixed()));
        var report = new SaveIntegrityChecker().CheckArchive(Save, 2, fixedReferenceDirectory: Reference);
        Assert.AreEqual(Severity.NotApplicable, report.Verdict(Axis.Custody));
        Assert.AreEqual(Severity.Pass, report.Verdict(Axis.Structure));
        Assert.AreEqual(Severity.Pass, report.Overall);
        Assert.IsFalse(report.Findings.Any(f => f.Axis == Axis.Custody));
        Assert.IsFalse(report.Artifacts.ContainsKey("working/processes.dat"));
        StringAssert.Contains(IntegrityText.Render(report), "Custody       N/A");
    }

    [TestMethod]
    public void AMissingArchiveIsFatal()
    {
        File.WriteAllBytes(Path.Combine(Save, "start.dat"), StartDat(9));
        var report = new SaveIntegrityChecker().CheckSave(_root, fixedReferenceDirectory: Reference);
        Assert.AreEqual(Severity.Fatal, report.Overall);
        Assert.AreEqual("ARC01", report.Findings.Single().CheckId);
    }

    [TestMethod]
    public void FolderScanSortsSlotsByNumberAndNamesTheOtherFiles()
    {
        File.WriteAllBytes(Path.Combine(Save, "u9game10.sav"), TestArchive());
        File.WriteAllBytes(Path.Combine(Save, "u9game2.sav"), TestArchive());
        File.WriteAllBytes(Path.Combine(Save, "u9game7.tmp"), [1, 2, 3]);
        File.WriteAllBytes(Path.Combine(Save, "notes.txt"), [1]);
        File.WriteAllBytes(Path.Combine(Save, "u9game3.sav"), "not a save"u8.ToArray());

        var scan = SaveFolderScan.Read(Save);
        CollectionAssert.AreEqual(new[] { 2, 3, 4, 10 }, scan.Slots.Select(s => s.Slot).ToArray());
        Assert.AreEqual(4, scan.SelectedSlot);
        Assert.AreEqual("Integrity fixture", scan.Selected!.Header!.Description);
        Assert.IsNotNull(scan.Slots.Single(s => s.Slot == 3).HeaderError);
        Assert.AreEqual(SaveFileKind.UnfinishedSave, scan.OtherFiles.Single(f => f.Name == "u9game7.tmp").Kind);
        Assert.AreEqual(SaveFileKind.Nonfixed, scan.OtherFiles.Single(f => f.Name == "nonfixed.9").Kind);
        Assert.AreEqual(SaveFileKind.Other, scan.OtherFiles.Single(f => f.Name == "notes.txt").Kind);
        Assert.AreEqual(0, scan.DuplicateSlots.Count);
    }

    [TestMethod]
    public void FolderScanFindsTwoFilesClaimingOneSlotAndAMissingSelector()
    {
        File.WriteAllBytes(Path.Combine(Save, "u9game04.sav"), TestArchive());
        File.Delete(Path.Combine(Save, "start.dat"));
        var scan = SaveFolderScan.Read(Save);
        CollectionAssert.AreEqual(new[] { 4 }, scan.DuplicateSlots.ToArray());
        Assert.IsNull(scan.SelectedSlot);
        StringAssert.Contains(scan.SelectorError!, "missing");
    }

    private static byte[] StartDat(int slot) => [.. "U9.008"u8, .. BitConverter.GetBytes(slot)];

    private static byte[] Nonfixed()
    {
        byte[] data = new byte[40];
        Put(data, 0x10, 0x00C00000);
        Put(data, 0x14, 1);
        Put(data, 0x18, 1);
        Put(data, 0x1C, 1);
        return data;
    }

    private static byte[] Fixed(int? liveSlot = null)
    {
        int payload = liveSlot != null ? 4096 : 0;
        byte[] data = new byte[36 + payload];
        Put(data, 0x08, payload);
        Put(data, 0x10, 1);
        Put(data, 0x14, 1);
        if (liveSlot is int live)
        {
            Put(data, 0x1C, 1);
            var free = Enumerable.Range(0, 166).Where(s => s != live).ToList();
            Put(data, 36 + 4, 0x60 + free[0] * 24);
            Put(data, 36 + 0x14, 1);
            for (int i = 0; i < free.Count; i++)
                Put(data, 36 + 0x60 + free[i] * 24, i + 1 < free.Count ? 0x60 + free[i + 1] * 24 : 0);
        }
        return data;
    }

    private static byte[] Processes(int? fixedOffset = null, int? nonfixedOffset = null)
    {
        int? encoded = fixedOffset != null ? -fixedOffset : nonfixedOffset;
        int count = encoded != null ? 3 : 2;
        int at = U9ItemHandleTable.HandleDataOffset;
        int end = at + 12 + count * 12;
        byte[] data = new byte[end + 4];
        Put(data, 0, 8);
        Put(data, 4, 2);
        Put(data, at, 1);
        Put(data, at + 4, count);
        Put(data, at + 8, 1);
        Put(data, at + 12 + 4, -1);
        Put(data, at + 24 + 4, -1);
        if (encoded is int offset)
        {
            Put(data, at + 36, 1);
            Put(data, at + 40, 9);
            Put(data, at + 44, offset);
        }
        Put(data, end, 2);
        return data;
    }

    private static byte[] HandleData()
    {
        int at = U9ItemHandleTable.HandleDataOffset;
        int end = at + 12 + 4 * 12;
        byte[] data = new byte[end + 4];
        Put(data, 0, 8);
        Put(data, 4, 2);
        Put(data, at, 1);
        Put(data, at + 4, 4);
        Put(data, at + 8, 1);
        int[][] records = [[0, -1, 0], [2, -1, 0], [0, -1, 0], [1, 9, -0x1068]];
        for (int i = 0; i < records.Length; i++)
            for (int j = 0; j < 3; j++)
                Put(data, at + 12 + i * 12 + j * 4, records[i][j]);
        Put(data, end, 2);
        return data;
    }

    private static byte[] Archive(byte[] processes, byte[] nonfixed) =>
        Build("", "Integrity fixture", [0, 0, 0, 0, 0, 0, 1, 0, 0, 0], [9, 0, 0], [[], processes, []], [(9, nonfixed)]);

    private static byte[] TestArchive(params (int Map, byte[] Data)[] maps) =>
        Build("", "Test save", [1, 2, 3, 0, 0, 0, 1, 4, 5, 6], [9, 700, 2], ["shot"u8.ToArray(), "process"u8.ToArray(), []],
              maps.Length > 0 ? maps : [(9, "map-nine"u8.ToArray())]);

    private static byte[] Build(string label, string description, float[] floats, int[] ints, byte[][] members,
                                (int Map, byte[] Data)[] maps)
    {
        using var stream = new MemoryStream();
        using var w = new BinaryWriter(stream);
        w.Write("U9:008"u8);
        foreach (string text in new[] { label, description })
        {
            byte[] bytes = text.Length == 0 ? [] : [.. Encoding.ASCII.GetBytes(text), 0];
            w.Write(bytes.Length);
            w.Write(bytes);
        }
        foreach (float f in floats) w.Write(f);
        foreach (int i in ints) w.Write(i);
        foreach (byte[] member in members)
        {
            w.Write(member.Length);
            w.Write(member);
        }
        foreach (var (map, data) in maps)
        {
            w.Write(map);
            w.Write(data.Length);
            w.Write(data);
        }
        w.Write(-1);
        return stream.ToArray();
    }

    private static void Put(byte[] data, int offset, int value) => BitConverter.GetBytes(value).CopyTo(data, offset);

    private static int CountOf(string text, string part)
    {
        int count = 0;
        for (int i = text.IndexOf(part, StringComparison.Ordinal); i >= 0; i = text.IndexOf(part, i + part.Length, StringComparison.Ordinal))
            count++;
        return count;
    }
}
