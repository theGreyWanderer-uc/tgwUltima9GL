using System.Text.Json;

namespace U9GL.Core.Saves;

public static class Severity
{
    public const string Pass = "PASS";
    public const string Info = "INFO";
    public const string Warn = "WARN";
    public const string Error = "ERROR";
    public const string Fatal = "FATAL";

    public const string NotApplicable = "N/A";

    public static int Rank(string severity) => severity switch
    {
        Warn => 1,
        Error => 2,
        Fatal => 3,
        _ => 0,
    };

    public static string FromRank(int rank) => rank switch
    {
        1 => Warn,
        2 => Error,
        3 => Fatal,
        _ => Pass,
    };
}

public static class Axis
{
    public const string Custody = "custody";
    public const string Structure = "structure";
    public const string Compatibility = "compatibility";
    public static IReadOnlyList<string> All { get; } = [Custody, Structure, Compatibility];
}

public sealed record IntegrityFinding(string CheckId, string Axis, string Severity, string Message,
                                      string? Path, long? Offset, IReadOnlyDictionary<string, object?> Details,
                                      bool AffectsVerdict = true)
{
    public string Family => CheckId.Length >= 3 ? CheckId[..3] : CheckId;

    public string Key => $"{CheckId}/{Severity}";
}

public sealed class IntegrityReport(string target)
{
    private static readonly JsonSerializerOptions JsonOptions =
        new() { WriteIndented = true, TypeInfoResolver = new System.Text.Json.Serialization.Metadata.DefaultJsonTypeInfoResolver() };

    private readonly List<IntegrityFinding> _findings = [];

    public string Target { get; } = target;
    public int? Slot { get; set; }
    public string? Archive { get; set; }

    public bool CustodyApplicable { get; set; } = true;

    public IReadOnlyList<IntegrityFinding> Findings => _findings;
    public Dictionary<string, Dictionary<string, object?>> Artifacts { get; } = [];

    public U9SaveHeader? Header { get; set; }

    public bool NoSaveSelected { get; set; }

    public bool SelfReference { get; set; }

    public void Add(string checkId, string axis, string severity, string message,
                    string? path = null, long? offset = null, params (string Key, object? Value)[] details) =>
        Add(checkId, axis, severity, message, path, offset, affectsVerdict: true, details);

    public void AddNote(string checkId, string axis, string severity, string message,
                        string? path = null, long? offset = null, params (string Key, object? Value)[] details) =>
        Add(checkId, axis, severity, message, path, offset, affectsVerdict: false, details);

    private void Add(string checkId, string axis, string severity, string message, string? path, long? offset,
                     bool affectsVerdict, (string Key, object? Value)[] details)
    {
        var map = new Dictionary<string, object?>();
        foreach (var (key, value) in details) map[key] = value;
        map.TryAdd("slot", Slot);
        map.TryAdd("count", 1);
        map.TryAdd("maps", new List<int>());
        _findings.Add(new IntegrityFinding(checkId, axis, severity, message, path, offset, map, affectsVerdict));
    }

    public string Verdict(string? axis = null)
    {
        if (axis == Saves.Axis.Custody && !CustodyApplicable) return Severity.NotApplicable;
        int rank = _findings.Where(f => f.AffectsVerdict && (axis == null || f.Axis == axis))
                            .Select(f => Severity.Rank(f.Severity)).DefaultIfEmpty(0).Max();
        return Severity.FromRank(rank);
    }

    public IReadOnlyList<(string CheckId, string Severity, string Message)> Recommendations() =>
        _findings.Where(f => Severity.Rank(f.Severity) > 0)
                 .Select(f => (f.CheckId, f.Severity)).Distinct()
                 .Where(IntegrityText.Recommendations.ContainsKey)
                 .OrderBy(k => k.CheckId, StringComparer.Ordinal).ThenBy(k => k.Severity, StringComparer.Ordinal)
                 .Select(k => (k.CheckId, k.Severity, IntegrityText.Recommendations[k])).ToList();

    public string Overall => Verdict();

    public void RecordArtifact(string name, ReadOnlySpan<byte> data, string source, params (string Key, object? Value)[] details)
    {
        var map = new Dictionary<string, object?> { ["source"] = source, ["bytes"] = data.Length, ["sha256"] = U9Bytes.Sha256(data) };
        foreach (var (key, value) in details) map[key] = value;
        Artifacts[name] = map;
    }

    public string ToJson()
    {
        var root = new Dictionary<string, object?>
        {
            ["target"] = Target,
            ["slot"] = Slot,
            ["archive"] = Archive,
            ["verdicts"] = Saves.Axis.All.ToDictionary(a => a, a => Verdict(a)),
            ["overall"] = Overall,
            ["artifacts"] = Artifacts,
            ["findings"] = _findings.Select(f => new Dictionary<string, object?>
            {
                ["check_id"] = f.CheckId,
                ["axis"] = f.Axis,
                ["severity"] = f.Severity,
                ["message"] = f.Message,
                ["path"] = f.Path,
                ["offset"] = f.Offset,
                ["details"] = f.Details,
                ["affects_verdict"] = f.AffectsVerdict,
            }).ToList(),
            ["recommendations"] = Recommendations().Select(r => new Dictionary<string, object?>
            {
                ["check_id"] = r.CheckId,
                ["severity"] = r.Severity,
                ["message"] = r.Message,
            }).ToList(),
            ["no_save_selected"] = NoSaveSelected,
            ["custody_applicable"] = CustodyApplicable,
            ["self_reference"] = SelfReference,
        };
        return JsonSerializer.Serialize(root, JsonOptions);
    }
}

public static class IntegrityText
{
    public static IReadOnlyDictionary<string, string> Assessments { get; } = new Dictionary<string, string>
    {
        [Severity.Pass] = "PASS - no integrity problems detected",
        [Severity.Warn] = "REVIEW ADVISED - evidence is incomplete or uncertain",
        [Severity.Error] = "INTEGRITY ERRORS - repair or investigate before loading",
        [Severity.Fatal] = "UNSAFE TO LOAD - a required invariant is broken",
    };

    public static IReadOnlyDictionary<string, string> AxisDescriptions { get; } = new Dictionary<string, string>
    {
        [Axis.Custody] = "archive and working-file provenance",
        [Axis.Structure] = "binary layout and allocator health",
        [Axis.Compatibility] = "saved references against map data",
    };

    private const string TryAfterBackup =
        "Keep a backup, then try loading if no more serious findings apply. If the game crashes, or things are missing or out of place, go back to your backup or an older save and save a report to share.";

    public static IReadOnlyDictionary<(string CheckId, string Severity), string> Recommendations { get; } =
        new Dictionary<(string, string), string>
    {
        [("ARC01", Severity.Fatal)] = "Do not load this archive; preserve it and recover a readable, structurally valid copy from backup.",
        [("ARC10", Severity.Warn)] = TryAfterBackup,
        [("CUS01", Severity.Fatal)] = "Preserve the working files, then re-extract processes.dat from the selected archive before loading; do not mix slots.",
        [("CUS02", Severity.Fatal)] = "Restore the missing nonfixed members from the selected archive before loading.",
        [("CUS03", Severity.Fatal)] = "Preserve the working files, then re-extract the mismatched nonfixed members from the selected archive before loading; do not mix slots.",
        [("CUS04", Severity.Warn)] = TryAfterBackup,
        [("FXS01", Severity.Fatal)] = "Do not load with the affected fixed maps; restore structurally valid copies matching the save's creation-time installation.",
        [("FXS04", Severity.Fatal)] = "Do not load with invalid fixed free lists; preserve the maps and restore copies matching the save's creation-time installation.",
        [("HND01", Severity.Fatal)] = "Do not load; preserve processes.dat and recover a copy with a valid object-reference table from a matching save backup.",
        [("HND02", Severity.Error)] = "Investigate and repair the object-reference free chain in processes.dat before loading.",
        [("HND04", Severity.Error)] = "Investigate the named live object-reference count/map fields in processes.dat before loading.",
        [("LAY02", Severity.Error)] = "Investigate the fixed-file differences and restore the save's trusted creation-time fixed set before loading.",
        [("LAY04", Severity.Warn)] = "An original map comparison is optional. Keep a backup before trying the save; if loading causes problems, use the backup or an older save and save a report to share.",
        [("NFS01", Severity.Fatal)] = "Do not load the affected nonfixed maps; preserve them and recover structurally valid copies from a matching save backup.",
        [("NFS01", Severity.Warn)] = TryAfterBackup,
        [("NFS07", Severity.Warn)] = TryAfterBackup,
        [("PRC01", Severity.Fatal)] = "Do not load; preserve processes.dat and investigate the unreadable camera/control prefix or typed process list.",
        [("PRC02", Severity.Warn)] = TryAfterBackup,
        [("REF01", Severity.Fatal)] = "Do not load; supply the missing target maps from the matching archive or creation-time fixed installation.",
        [("REF02", Severity.Fatal)] = "Do not load or overwrite the slot; inspect the invalid object offsets against the matching map payloads.",
        [("REF03", Severity.Fatal)] = "Do not load or overwrite the slot; investigate fixed references into non-live slots and restore matching creation-time map data.",
        [("REF04", Severity.Error)] = "Investigate nonfixed references into non-live slots against the selected archive's maps before loading.",
        [("REF07", Severity.Warn)] = TryAfterBackup,
        [("SEL01", Severity.Fatal)] = "Select an explicit existing slot with --slot, or restore a valid start.dat before loading.",
        [("SEL02", Severity.Fatal)] = "Locate the savegame directory and select an existing slot before loading.",
    };

    private static readonly string[] EvidenceKeys =
        ["maps", "handle_count", "live_handles", "fixed_handles", "nonfixed_handles", "chunks", "pages", "objects",
         "incomplete_chunks", "invalid_free_lists"];

    public static string Render(IntegrityReport report)
    {
        var lines = new List<string>
        {
            "Ultima IX save integrity",
            $"Target: {report.Target}",
            $"Slot: {(report.Slot?.ToString() ?? "-")}",
            $"Archive: {report.Archive ?? "-"}",
            "",
            $"Assessment: {Assessments[report.Overall]}",
            "",
            "Checks",
        };
        foreach (string axis in Axis.All)
            lines.Add($"  {Title(axis),-13} {report.Verdict(axis),-5}  {AxisDescriptions[axis]}");

        var actionable = report.Findings.Where(f => f.Severity != Severity.Info && f.AffectsVerdict).ToList();
        var notes = report.Findings.Where(f => !f.AffectsVerdict).ToList();
        var confirmations = report.Findings.Where(f => f.Severity == Severity.Info && f.AffectsVerdict).ToList();
        var groups = actionable.GroupBy(f => (f.Severity, f.CheckId, f.Axis, f.Message)).ToList();
        string label = groups.Count == actionable.Count ? $"{groups.Count}" : $"{groups.Count} distinct, {actionable.Count} findings";
        lines.Add("");
        lines.Add($"Problems ({label})");
        if (actionable.Count == 0) lines.Add("  None");
        foreach (var group in groups.OrderBy(g => -Severity.Rank(g.Key.Severity)).ThenBy(g => g.Key.Axis, StringComparer.Ordinal)
                                    .ThenBy(g => g.Key.CheckId, StringComparer.Ordinal))
        {
            lines.Add($"  {group.Key.Severity,-5} {group.Key.CheckId} {group.Key.Axis}: {group.Key.Message}");
            foreach (string source in group.Select(f => f.Path).OfType<string>().Distinct().Order(StringComparer.Ordinal))
                lines.Add($"        Source: {source}");
            var records = TargetRecords(group).ToList();
            foreach (var record in records.Take(8)) lines.Add("        Evidence: " + EvidenceLine(record));
            if (records.Count > 8) lines.Add($"        Evidence: {records.Count - 8} more target records in JSON");
        }

        if (notes.Count > 0)
        {
            lines.Add("");
            lines.Add($"Comparison notes ({notes.Count})");
            foreach (var f in notes) lines.Add($"  {f.Severity} {f.CheckId}: {f.Message}{(f.Path != null ? $" [{f.Path}]" : "")}");
        }

        var recommendations = report.Recommendations();
        if (recommendations.Count > 0)
        {
            lines.Add("");
            lines.Add("Next steps");
            foreach (var r in recommendations) lines.Add($"  {r.CheckId} / {r.Severity}: {r.Message}");
        }

        lines.Add("");
        lines.Add($"Confirmations ({confirmations.Count})");
        if (confirmations.Count == 0) lines.Add("  None");
        foreach (var f in confirmations.OrderBy(f => f.Axis, StringComparer.Ordinal).ThenBy(f => f.CheckId, StringComparer.Ordinal))
            lines.Add($"  {f.CheckId} {f.Message}{(f.Path != null ? $" [{f.Path}]" : "")}");

        lines.Add("");
        lines.Add("Evidence");
        foreach (var (name, artifact) in report.Artifacts)
            lines.Add($"  {name}: {string.Join(", ", EvidenceSummary(artifact))}");
        return string.Join("\n", lines);
    }

    public static IEnumerable<IReadOnlyDictionary<string, object?>> TargetRecords(IEnumerable<IntegrityFinding> findings)
    {
        var seen = new HashSet<(object?, object?, object?)>();
        foreach (var finding in findings)
        {
            if (!finding.Details.TryGetValue("target_records", out var value) || value is not IEnumerable<Dictionary<string, object?>> records)
                continue;
            foreach (var record in records)
                if (seen.Add((record.GetValueOrDefault("arena"), record.GetValueOrDefault("map"), record.GetValueOrDefault("offset"))))
                    yield return record;
        }
    }

    public static string EvidenceLine(IReadOnlyDictionary<string, object?> r)
    {
        string handles = string.Join(",", (IEnumerable<int>)r["handles"]!);
        string prefix = $"handle({handles}) {r["arena"]}.{r["map"]} offset 0x{r["offset"]:X} slot {r["slot"]}";
        if (!r.ContainsKey("page")) return $"{prefix} state {r["allocation_state"]}";
        var page = (IReadOnlyList<long>)r["page"]!;
        var xyz = (IReadOnlyList<long>)r["xyz"]!;
        var rotation = (IReadOnlyList<short>)r["rotation"]!;
        return $"{prefix} page({page[0]},{page[1]}) xyz({xyz[0]},{xyz[1]},{xyz[2]}) type {r["type"]} " +
               $"rot({string.Join(", ", rotation)}) status {r["status_hex"]} state {r["allocation_state"]}";
    }

    private static IEnumerable<string> EvidenceSummary(Dictionary<string, object?> artifact)
    {
        yield return $"{artifact.GetValueOrDefault("bytes") ?? 0} bytes";
        string sha = artifact.GetValueOrDefault("sha256") as string ?? "";
        yield return sha.Length > 12 ? sha[..12] : sha;
        foreach (string key in EvidenceKeys)
            if (artifact.TryGetValue(key, out var value))
                yield return $"{key}={(value is IEnumerable<int> list ? "[" + string.Join(", ", list) + "]" : value)}";
    }

    private static string Title(string axis) => char.ToUpperInvariant(axis[0]) + axis[1..];
}
