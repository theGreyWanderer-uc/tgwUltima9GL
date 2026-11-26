using System.Text.RegularExpressions;

namespace U9GL.Core.Saves;

public sealed partial class SaveIntegrityChecker
{
    private readonly Dictionary<string, CachedFile> _files = new(StringComparer.OrdinalIgnoreCase);

    [GeneratedRegex(@"^(fixed|nonfixed)\.(\d+)$", RegexOptions.IgnoreCase)]
    private static partial Regex MapFile();

    public IntegrityReport CheckSave(string target, int? slot = null, string? staticDirectory = null,
                                     string? fixedReferenceDirectory = null, bool allowPartial = false)
    {
        var (saveDirectory, defaultStatic) = ResolveDirectories(target);
        string staticPath = staticDirectory ?? defaultStatic;
        var report = new IntegrityReport(target);

        if (!Directory.Exists(saveDirectory))
        {
            report.Slot = slot;
            report.NoSaveSelected = slot == null;
            report.Add("SEL02", Axis.Custody, Severity.Fatal, "save directory does not exist", saveDirectory);
            return report;
        }
        if (slot == null)
        {
            string startPath = Path.Combine(saveDirectory, "start.dat");
            try
            {
                slot = U9StartDat.FromFile(startPath).Slot;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or U9SaveException)
            {
                report.Add("SEL01", Axis.Custody, Severity.Fatal, ReadError(e), startPath);
                report.NoSaveSelected = e is FileNotFoundException or DirectoryNotFoundException;
                return report;
            }
        }
        report.Slot = slot;
        return Check(report, saveDirectory, slot.Value, staticPath, fixedReferenceDirectory, allowPartial, pairWithWorking: true);
    }

    public IntegrityReport CheckArchive(string saveDirectory, int slot, string? staticDirectory = null,
                                        string? fixedReferenceDirectory = null)
    {
        string staticPath = staticDirectory ?? ResolveDirectories(saveDirectory).Static;
        var report = new IntegrityReport(saveDirectory) { Slot = slot, CustodyApplicable = false };
        return Check(report, saveDirectory, slot, staticPath, fixedReferenceDirectory, allowPartial: false, pairWithWorking: false);
    }

    private IntegrityReport Check(IntegrityReport report, string saveDirectory, int slot, string staticPath,
                                  string? fixedReferenceDirectory, bool allowPartial, bool pairWithWorking)
    {
        string archivePath = Path.Combine(saveDirectory, $"u9game{slot}.sav");
        report.Archive = archivePath;
        report.SelfReference = fixedReferenceDirectory != null && string.Equals(FullDirectory(fixedReferenceDirectory),
            FullDirectory(staticPath), StringComparison.OrdinalIgnoreCase);
        byte[] archiveData;
        U9SaveArchive archive;
        try
        {
            archiveData = File.ReadAllBytes(archivePath);
            archive = U9SaveArchive.FromBytes(archiveData);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or U9SaveException)
        {
            report.Add("ARC01", pairWithWorking ? Axis.Custody : Axis.Structure, Severity.Fatal, ReadError(e), archivePath);
            return report;
        }
        report.Header = archive.Header;
        report.RecordArtifact("archive", archiveData, archivePath,
            ("description", archive.Header.Description), ("header_map", archive.Header.HeaderMap),
            ("label", archive.Header.Label), ("time", archive.Header.Time), ("day", archive.Header.Day),
            ("position", archive.Header.Position.ToList()),
            ("maps", archive.Nonfixed.Select(m => m.MapNumber!.Value).ToList()));
        report.RecordArtifact("archive/processes.dat", archive.Processes.Data.Span, $"{archivePath}!processes.dat");

        var strayMaps = archive.Nonfixed.Select(m => m.MapNumber!.Value)
                                        .Where(m => m > U9SaveArchive.MaxShippedMapNumber).ToList();
        if (strayMaps.Count > 0)
            report.Add("ARC10", Axis.Custody, Severity.Warn,
                $"{strayMaps.Count} archived map members are numbered above {U9SaveArchive.MaxShippedMapNumber}: " +
                $"[{string.Join(", ", strayMaps)}]. The game archives and restores nonfixed.0-{U9SaveArchive.MaxMapNumber}, " +
                "so this is legal, but no shipped map has these numbers; they come from stray loose files in the save directory",
                archivePath, null, ("count", strayMaps.Count), ("maps", strayMaps));

        U9ItemHandleTable? handles = null;
        try
        {
            handles = U9ItemHandleTable.FromBytes(archive.Processes.Data.Span);
        }
        catch (U9ProcessDataException e)
        {
            report.Add("HND01", Axis.Structure, Severity.Fatal, e.Message, $"{archivePath}!processes.dat");
        }

        var looseData = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        byte[] ReadLoose(string path) => looseData.TryGetValue(path, out var data) ? data : looseData[path] = File.ReadAllBytes(path);
        var looseNonfixed = pairWithWorking ? FilesByMap(saveDirectory, "nonfixed") : [];
        if (pairWithWorking) CheckCustody(report, archive, saveDirectory, looseNonfixed, allowPartial, ReadLoose);

        var archiveNonfixed = new Dictionary<int, U9Nonfixed>();
        foreach (var member in archive.Nonfixed)
            if (CheckNonfixed(report, $"archive/{member.Name}", member.Data.ToArray(), $"{archivePath}!{member.Name}",
                              member.MapNumber!.Value) is { } parsed)
                archiveNonfixed[member.MapNumber!.Value] = parsed;
        if (Path.GetDirectoryName(Path.GetFullPath(staticPath)) is { } installRoot)
            CheckFanPatch(report, archiveNonfixed, Path.Combine(installRoot, "runtime"), archivePath);
        foreach (var (map, path) in looseNonfixed.OrderBy(kv => kv.Key))
            CheckNonfixed(report, $"working/nonfixed.{map}", ReadLoose(path), path, map);

        var relevant = new SortedSet<int> { archive.Header.HeaderMap };
        relevant.UnionWith(archive.Nonfixed.Select(m => m.MapNumber!.Value)
                                           .Where(m => m <= U9SaveArchive.MaxShippedMapNumber));
        if (handles != null) relevant.UnionWith(handles.LiveEntries.Select(e => e.MapNumber));
        var fixedFiles = FilesByMap(staticPath, "fixed");
        var toCheck = allowPartial ? new SortedSet<int>(relevant.Where(fixedFiles.ContainsKey)) : relevant;
        var parsedFixed = new Dictionary<int, U9Fixed>();
        foreach (int map in toCheck)
        {
            if (!fixedFiles.TryGetValue(map, out string? fixedPath))
            {
                report.Add("REF01", Axis.Compatibility, Severity.Fatal, $"fixed.{map} is unavailable for a save-relevant map", staticPath,
                    null, ("map_number", map), ("maps", new List<int> { map }));
                continue;
            }
            if (CheckFixed(report, fixedPath, map) is { } parsed) parsedFixed[map] = parsed;
        }
        if (allowPartial)
        {
            var missing = relevant.Where(m => !fixedFiles.ContainsKey(m)).ToList();
            if (missing.Count > 0)
                report.Add("REF08", Axis.Compatibility, Severity.Info,
                    $"partial bundle omits fixed files for {missing.Count} archive maps", null, null,
                    ("map_numbers", missing), ("count", missing.Count), ("maps", missing));
        }

        CheckLayout(report, fixedReferenceDirectory, fixedFiles, allowPartial ? toCheck : relevant, allowPartial);

        if (handles != null)
        {
            IReadOnlyList<int> freeChain;
            try
            {
                freeChain = handles.WalkFreeChain();
            }
            catch (U9ProcessDataException e)
            {
                freeChain = [];
                report.Add("HND02", Axis.Structure, Severity.Error, e.Message, $"{archivePath}!processes.dat");
            }
            var artifact = report.Artifacts["archive/processes.dat"];
            artifact["handle_offset"] = handles.Offset;
            artifact["handle_count"] = handles.Count;
            artifact["free_handles"] = freeChain.Count;
            artifact["live_handles"] = handles.LiveEntries.Count;
            artifact["fixed_handles"] = handles.FixedEntries.Count;
            artifact["nonfixed_handles"] = handles.NonfixedEntries.Count;
            var invalid = handles.LiveEntries.Where(e => e.UsageCount <= 0 || e.MapNumber > U9SaveArchive.MaxMapNumber)
                                             .Select(e => e.Index).ToList();
            if (invalid.Count > 0)
                report.Add("HND04", Axis.Structure, Severity.Error, $"{invalid.Count} live handles have invalid usage/map fields",
                    $"{archivePath}!processes.dat", null, ("reference_indices", invalid), ("handle_indices", invalid),
                    ("count", invalid.Count), ("maps", MapsOf(handles, invalid)));
            ResolveHandles(report, handles, archiveNonfixed, parsedFixed);
        }
        return report;
    }

    private static void CheckCustody(IntegrityReport report, U9SaveArchive archive, string saveDirectory,
                                     Dictionary<int, string> looseNonfixed, bool allowPartial, Func<string, byte[]> read)
    {
        var pairs = new List<(U9SaveMember Member, string Loose)> { (archive.Processes, Path.Combine(saveDirectory, "processes.dat")) };
        pairs.AddRange(archive.Nonfixed.Select(m => (m, Path.Combine(saveDirectory, m.Name))));
        var missing = new List<string>();
        foreach (var (member, loosePath) in pairs)
        {
            string checkId = member.Name == "processes.dat" ? "CUS01" : "CUS03";
            if (!File.Exists(loosePath))
            {
                if (allowPartial)
                {
                    missing.Add(member.Name);
                    continue;
                }
                report.Add(member.MapNumber != null ? "CUS02" : checkId, Axis.Custody, Severity.Fatal,
                    $"archived member {member.Name} is missing from working directory", loosePath, null,
                    ("member_name", member.Name), ("maps", MemberMaps(member)));
                continue;
            }
            byte[] loose = read(loosePath);
            if (member.Name == "processes.dat")
                report.RecordArtifact("working/processes.dat", loose, loosePath);
            if (!loose.AsSpan().SequenceEqual(member.Data.Span))
                report.Add(checkId, Axis.Custody, Severity.Fatal, $"working {member.Name} differs from selected archive member",
                    loosePath, null, ("archive_sha256", U9Bytes.Sha256(member.Data.Span)), ("working_sha256", U9Bytes.Sha256(loose)),
                    ("member_name", member.Name), ("maps", MemberMaps(member)));
        }
        if (missing.Count > 0)
            report.Add("CUS07", Axis.Custody, Severity.Info, $"partial bundle omits {missing.Count} archived working files",
                null, null, ("omitted", missing), ("count", missing.Count));

        var archived = archive.Nonfixed.Select(m => m.MapNumber!.Value).ToHashSet();
        foreach (var (map, path) in looseNonfixed.OrderBy(kv => kv.Key))
            if (!archived.Contains(map))
                report.Add("CUS04", Axis.Custody, Severity.Warn, $"working nonfixed.{map} has no member in selected archive", path,
                    null, ("map_number", map), ("maps", new List<int> { map }));
    }

    private void CheckLayout(IntegrityReport report, string? referenceDirectory, Dictionary<int, string> fixedFiles,
                             IEnumerable<int> maps, bool allowPartial)
    {
        if (referenceDirectory == null)
        {
            var all = maps.ToList();
            report.AddNote("LAY04", Axis.Compatibility, Severity.Warn, "no creation-time fixed reference directory was supplied",
                null, null, ("count", all.Count), ("maps", all));
            return;
        }
        var references = FilesByMap(referenceDirectory, "fixed");
        foreach (int map in allowPartial ? maps.Where(references.ContainsKey) : maps)
        {
            fixedFiles.TryGetValue(map, out string? current);
            references.TryGetValue(map, out string? reference);
            if (current == null || reference == null)
            {
                report.AddNote("LAY04", Axis.Compatibility, Severity.Warn, $"fixed.{map} cannot be compared with a trusted reference",
                    reference ?? current, null, ("map_number", map), ("maps", new List<int> { map }), ("read_error", null));
                continue;
            }
            string currentSha = Load(current).Sha256, referenceSha = Load(reference).Sha256;
            if (currentSha != referenceSha)
                report.Add("LAY02", Axis.Compatibility, Severity.Error, $"fixed.{map} differs byte-for-byte from the trusted reference",
                    current, null, ("current_sha256", currentSha), ("reference_sha256", referenceSha),
                    ("map_number", map), ("maps", new List<int> { map }));
            else
                report.Add("LAY01", Axis.Compatibility, Severity.Info, $"fixed.{map} is byte-identical to the trusted reference", current,
                    null, ("map_number", map), ("maps", new List<int> { map }));
        }
    }

    private static void CheckFanPatch(IntegrityReport report, Dictionary<int, U9Nonfixed> archiveNonfixed,
                                      string runtimeDirectory, string archivePath)
    {
        var installed = FanPatch.InstalledState(runtimeDirectory);
        var saved = FanPatch.SaveState(archiveNonfixed);
        if (installed.Companions == true && saved.Companions == false)
            report.Add("FWP01", Axis.Compatibility, Severity.Info,
                "the installed maps carry the Forgotten World companion fix (1.19G and later), but this save's own " +
                "copy of map 9 predates it: Mariah and Jaana will not come to Moonglow and Yew after their shrines; " +
                "the patch's other fixes work in this save",
                $"{archivePath}!nonfixed.{FanPatch.CompanionMap}", null, ("maps", new List<int> { FanPatch.CompanionMap }));

        var lowered = installed.LoweredShrines().ToHashSet();
        var floating = saved.StockShrines().Where(lowered.Contains).ToList();
        if (floating.Count == 0) return;
        var shrines = floating.Select(o => o.Shrine).Distinct().Order(StringComparer.Ordinal).ToList();
        var maps = floating.Select(o => o.MapNumber).Distinct().Order().ToList();
        report.Add("FWP02", Axis.Compatibility, Severity.Info,
            "the installed maps carry the Forgotten World 1.19H Rune fix, but this save's own maps still hold " +
            $"{floating.Count} Runes and virtue items at their old floating heights, at {shrines.Count} shrines: " +
            string.Join(", ", shrines),
            $"{archivePath}!" + string.Join(", ", maps.Select(m => $"nonfixed.{m}")), null,
            ("count", shrines.Count), ("shrine_names", string.Join(", ", shrines)), ("maps", maps));
    }

    private static U9Nonfixed? CheckNonfixed(IntegrityReport report, string name, byte[] data, string source, int map)
    {
        var maps = new List<int> { map };
        report.RecordArtifact(name, data, source);
        U9Nonfixed nonfixed;
        IReadOnlyList<U9NonfixedChunk> chunks;
        try
        {
            nonfixed = new U9Nonfixed(data);
            chunks = nonfixed.Chunks;
        }
        catch (Exception e) when (IsParseFailure(e))
        {
            report.Add("NFS01", Axis.Structure, Severity.Fatal, e.Message, source, null, ("map_number", map), ("maps", maps));
            return null;
        }
        var incomplete = chunks.Where(c => !c.AllocationIsComplete).Select(c => c.Index).ToList();
        var artifact = report.Artifacts[name];
        artifact["grid"] = $"{nonfixed.Width}x{nonfixed.Height}";
        artifact["chunks"] = chunks.Count;
        artifact["pages"] = chunks.Sum(c => c.Pages.Count);
        artifact["indexed_entities"] = chunks.Sum(c => c.Entities.Count);
        artifact["allocated_entities"] = chunks.Sum(c => c.AllocatedEntities.Count);
        artifact["extra_data"] = chunks.Sum(c => c.ExtraData.Count);
        artifact["incomplete_chunks"] = incomplete.Count;
        if (nonfixed.DeclaredPayloadSize != 0 && nonfixed.DeclaredPayloadSize != nonfixed.PayloadSize)
            report.Add("NFS01", Axis.Structure, Severity.Warn, "declared payload watermark differs from physical payload", source, null,
                ("declared", nonfixed.DeclaredPayloadSize), ("actual", nonfixed.PayloadSize), ("map_number", map), ("maps", maps));
        if (incomplete.Count > 0)
            report.Add("NFS07", Axis.Structure, Severity.Warn, $"allocator recovery is incomplete in {incomplete.Count} chunks",
                source, null, ("chunk_indices", incomplete), ("count", incomplete.Count), ("map_number", map), ("maps", maps));
        return nonfixed;
    }

    private U9Fixed? CheckFixed(IntegrityReport report, string path, int map)
    {
        var file = Load(path);
        string name = $"fixed.{map}";
        report.Artifacts[name] = new Dictionary<string, object?> { ["source"] = path, ["bytes"] = file.Length, ["sha256"] = file.Sha256 };
        var maps = new List<int> { map };
        if (file.Fixed == null)
        {
            report.Add("FXS01", Axis.Structure, Severity.Fatal, file.Error ?? "unreadable", path, null, ("map_number", map), ("maps", maps));
            return null;
        }
        var fixedMap = file.Fixed;
        var pages = fixedMap.Chunks.SelectMany(c => c.Pages).ToList();
        var invalid = pages.Where(p => !p.FreeListWalked).Select(p => p.Offset).ToList();
        var artifact = report.Artifacts[name];
        artifact["grid"] = $"{fixedMap.Width}x{fixedMap.Height}";
        artifact["regions"] = fixedMap.Chunks.Count;
        artifact["pages"] = pages.Count;
        artifact["objects"] = fixedMap.Chunks.Sum(c => c.Objects.Count);
        artifact["invalid_free_lists"] = invalid.Count;
        artifact["declared_heap"] = fixedMap.HeapSize;
        artifact["physical_payload"] = fixedMap.PayloadSize;
        if (fixedMap.HeapSize > fixedMap.PayloadSize)
            report.Add("FXS01", Axis.Structure, Severity.Fatal, "declared fixed heap extends beyond EOF", path, null,
                ("declared", fixedMap.HeapSize), ("actual", fixedMap.PayloadSize), ("map_number", map), ("maps", maps));
        if (invalid.Count > 0)
            report.Add("FXS04", Axis.Structure, Severity.Fatal, $"{invalid.Count} of {pages.Count} page free lists are invalid",
                path, null, ("page_offsets", invalid), ("count", invalid.Count), ("total_count", pages.Count),
                ("map_number", map), ("maps", maps));
        return fixedMap;
    }

    private static void ResolveHandles(IntegrityReport report, U9ItemHandleTable table,
                                       Dictionary<int, U9Nonfixed> nonfixed, Dictionary<int, U9Fixed> fixedMaps)
    {
        var missingMaps = new SortedSet<(string, int)>();
        var invalidSlots = new List<int>();
        var fixedFree = new List<int>();
        var nonfixedFree = new List<int>();
        var unknown = new List<int>();

        var nonfixedState = nonfixed.ToDictionary(kv => kv.Key, kv => (
            Live: kv.Value.Chunks.SelectMany(c => c.AllocatedEntities).Select(e => e.Offset).ToHashSet(),
            Uncertain: kv.Value.Chunks.Where(c => !c.AllocationIsComplete).SelectMany(c => c.Pages).Select(p => p.Offset).ToHashSet(),
            Heap: kv.Value.PayloadSize));
        var fixedState = fixedMaps.ToDictionary(kv => kv.Key, kv => (
            Live: kv.Value.Chunks.SelectMany(c => c.Objects).Select(o => o.Offset).ToHashSet(),
            Pages: kv.Value.Chunks.SelectMany(c => c.Pages).GroupBy(p => p.Offset).ToDictionary(g => g.Key, g => g.Last().FreeListWalked),
            Heap: Math.Min(kv.Value.HeapSize, kv.Value.PayloadSize)));

        foreach (var entry in table.LiveEntries)
        {
            long offset = entry.ItemOffset;
            long pageOffset = offset & ~0xFFFL;
            long delta = offset - pageOffset - 0x60;
            if (entry.IsFixed)
            {
                if (!fixedState.TryGetValue(entry.MapNumber, out var state))
                {
                    missingMaps.Add(("fixed", entry.MapNumber));
                    continue;
                }
                if (offset >= state.Heap || delta < 0 || delta % 24 != 0 || delta / 24 >= 166) invalidSlots.Add(entry.Index);
                else if (!state.Pages.GetValueOrDefault(pageOffset)) unknown.Add(entry.Index);
                else if (!state.Live.Contains(offset)) fixedFree.Add(entry.Index);
            }
            else
            {
                if (!nonfixedState.TryGetValue(entry.MapNumber, out var state))
                {
                    missingMaps.Add(("nonfixed", entry.MapNumber));
                    continue;
                }
                if (offset >= state.Heap || delta < 0 || delta % 32 != 0 || delta / 32 >= 125) invalidSlots.Add(entry.Index);
                else if (state.Live.Contains(offset)) continue;
                else if (state.Uncertain.Contains(pageOffset)) unknown.Add(entry.Index);
                else nonfixedFree.Add(entry.Index);
            }
        }

        if (missingMaps.Count > 0)
            report.Add("REF01", Axis.Compatibility, Severity.Fatal, $"{missingMaps.Count} handle target map arenas are unavailable",
                null, null, ("arenas", missingMaps.Select(m => $"{m.Item1}.{m.Item2}").ToList()), ("count", missingMaps.Count),
                ("maps", missingMaps.Select(m => m.Item2).Distinct().Order().ToList()));
        if (invalidSlots.Count > 0)
            report.Add("REF02", Axis.Compatibility, Severity.Fatal, $"{invalidSlots.Count} handles have out-of-bounds or misaligned item offsets",
                null, null, ("reference_indices", invalidSlots), ("handle_indices", invalidSlots), ("count", invalidSlots.Count),
                ("maps", MapsOf(table, invalidSlots)));
        if (fixedFree.Count > 0)
        {
            var targets = fixedFree.Select(i => (table.Entries[i].MapNumber, (long)table.Entries[i].ItemOffset)).Distinct().Order().ToList();
            report.Add("REF03", Axis.Compatibility, Severity.Fatal, $"{fixedFree.Count} fixed handles resolve to {targets.Count} non-live slots",
                null, null, ("reference_indices", fixedFree), ("handle_indices", fixedFree), ("count", fixedFree.Count),
                ("target_count", targets.Count), ("maps", targets.Select(t => t.MapNumber).Distinct().Order().ToList()),
                ("targets", targets.Select(t => new Dictionary<string, object?> { ["map"] = t.MapNumber, ["offset"] = t.Item2 }).ToList()),
                ("target_records", TargetRecords(table, fixedFree, nonfixed, fixedMaps, "non-live")));
        }
        if (nonfixedFree.Count > 0)
            report.Add("REF04", Axis.Compatibility, Severity.Error, $"{nonfixedFree.Count} nonfixed handles resolve to non-live slots",
                null, null, ("reference_indices", nonfixedFree), ("handle_indices", nonfixedFree), ("count", nonfixedFree.Count),
                ("maps", MapsOf(table, nonfixedFree)),
                ("target_records", TargetRecords(table, nonfixedFree, nonfixed, fixedMaps, "non-live")));
        if (unknown.Count > 0)
            report.Add("REF07", Axis.Compatibility, Severity.Warn,
                $"{unknown.Count} handle targets cannot be classified on invalid/incomplete pages",
                null, null, ("reference_indices", unknown), ("handle_indices", unknown), ("count", unknown.Count),
                ("maps", MapsOf(table, unknown)),
                ("target_records", TargetRecords(table, unknown, nonfixed, fixedMaps, "unknown")));
    }

    private static List<int> MapsOf(U9ItemHandleTable table, IEnumerable<int> indices) =>
        indices.Select(i => table.Entries[i].MapNumber).Distinct().Order().ToList();

    private static List<int> MemberMaps(U9SaveMember member) => member.MapNumber is int m ? [m] : [];

    private static List<Dictionary<string, object?>> TargetRecords(U9ItemHandleTable table, List<int> indices,
        Dictionary<int, U9Nonfixed> nonfixed, Dictionary<int, U9Fixed> fixedMaps, string allocationState)
    {
        var byTarget = new SortedDictionary<(string Arena, int Map, long Offset), List<int>>();
        foreach (int index in indices)
        {
            var entry = table.Entries[index];
            var key = (entry.IsFixed ? "fixed" : "nonfixed", entry.MapNumber, (long)entry.ItemOffset);
            if (!byTarget.TryGetValue(key, out var list)) byTarget[key] = list = [];
            list.Add(index);
        }

        var records = new List<Dictionary<string, object?>>();
        foreach (var ((arena, map, offset), handles) in byTarget)
        {
            long pageOffset = offset & ~0xFFFL;
            int slotSize = arena == "fixed" ? 24 : 32;
            var detail = new Dictionary<string, object?>
            {
                ["reference_indices"] = handles,
                ["handles"] = handles,
                ["arena"] = arena,
                ["map"] = map,
                ["offset"] = offset,
                ["page_offset"] = pageOffset,
                ["slot"] = U9Bytes.FloorDiv(offset - pageOffset - 0x60, slotSize),
                ["allocation_state"] = allocationState,
            };
            if (arena == "fixed" && fixedMaps.GetValueOrDefault(map)?.ObjectRecordAt(offset) is { } o)
                AddRecord(detail, o.BaseX, o.BaseY, o.X, o.Y, o.Z, o.WorldX, o.WorldY, o.TypeIndex, o.Rotation, o.Flags);
            else if (arena == "nonfixed" && nonfixed.GetValueOrDefault(map)?.EntityRecordAt(offset) is { } e)
                AddRecord(detail, e.BaseX, e.BaseY, e.OffsetX, e.OffsetY, e.Z, e.WorldX, e.WorldY, e.TypeIndex, e.Rotation, e.Flags);
            records.Add(detail);
        }
        return records;
    }

    private static void AddRecord(Dictionary<string, object?> detail, uint baseX, uint baseY, long x, long y, long z,
                                  long worldX, long worldY, ushort type, IReadOnlyList<short> rotation, uint flags)
    {
        detail["page"] = new long[] { baseX, baseY };
        detail["xyz"] = new long[] { x, y, z };
        detail["world_xyz"] = new long[] { worldX, worldY, z };
        detail["type"] = type;
        detail["rotation"] = rotation.ToArray();
        detail["status"] = flags;
        detail["status_hex"] = flags.ToString("x5");
    }

    private sealed record CachedFile(long Length, DateTime Modified, string Sha256, U9Fixed? Fixed, string? Error);

    private CachedFile Load(string path)
    {
        var info = new FileInfo(path);
        if (_files.TryGetValue(path, out var cached) && cached.Length == info.Length && cached.Modified == info.LastWriteTimeUtc)
            return cached;
        byte[] data = File.ReadAllBytes(path);
        U9Fixed? parsed = null;
        string? error = null;
        try
        {
            parsed = new U9Fixed(data);
            _ = parsed.Chunks;
        }
        catch (Exception e) when (IsParseFailure(e))
        {
            parsed = null;
            error = e.Message;
        }
        var file = new CachedFile(data.Length, info.LastWriteTimeUtc, U9Bytes.Sha256(data), parsed, error);
        _files[path] = file;
        return file;
    }

    private static Dictionary<int, string> FilesByMap(string directory, string kind)
    {
        var files = new Dictionary<int, string>();
        if (!Directory.Exists(directory)) return files;
        foreach (string path in Directory.EnumerateFiles(directory))
        {
            var match = MapFile().Match(Path.GetFileName(path));
            if (match.Success && match.Groups[1].Value.Equals(kind, StringComparison.OrdinalIgnoreCase)
                && int.TryParse(match.Groups[2].Value, out int map))
                files[map] = path;
        }
        return files;
    }

    public static (string Save, string Static) ResolveDirectories(string target)
    {
        string save = Path.Combine(target, "savegame"), staticPath = Path.Combine(target, "static");
        if (Directory.Exists(save) || Directory.Exists(staticPath)) return (save, staticPath);
        string parent = Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(Path.GetFullPath(target))) ?? target;
        return (target, Path.Combine(parent, "static"));
    }

    private static string FullDirectory(string path) => Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));

    private static bool IsParseFailure(Exception e) =>
        e is U9FixedException or U9NonfixedException or IndexOutOfRangeException or ArgumentException or OverflowException;

    private static string ReadError(Exception e) => e switch
    {
        FileNotFoundException f => $"[Errno 2] No such file or directory: '{f.FileName}'",
        _ => e.Message,
    };
}
