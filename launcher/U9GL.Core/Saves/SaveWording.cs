using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace U9GL.Core.Saves;

public sealed record WordingAction(string Id, string Label, string Description);

public sealed record WordingEntry(string Title, string Meaning, string? WhatToDo, string LoadPolicy, string LoadText,
                                  IReadOnlyList<string> Actions, IReadOnlyList<string> Placeholders,
                                  bool AffectsVerdict = true, string DisplayKind = "finding");

public sealed record MapLabel(int Number, string Name, string Category, bool Development, string DisplayName);

public sealed record PlainSummary(string Result, string Title, string Meaning, string LoadPolicy, string LoadText,
                                  string? WhatToDo, IReadOnlyList<string> Actions);

public sealed record PlainCard(string Key, string CheckId, string Severity, bool AffectsVerdict, string DisplayKind,
                               string Title, IReadOnlyList<string> Meanings, string WhatToDo,
                               string FindingLoadPolicy, string LoadPolicy, string LoadText, IReadOnlyList<string> Actions,
                               IReadOnlyList<IntegrityFinding> Findings)
{
    public bool IsNote => DisplayKind == "note";
}

public sealed record SavePresentation(PlainSummary Summary, IReadOnlyList<PlainCard> Cards, IReadOnlyList<string> Actions,
                                      IReadOnlyList<string> Notes);

public sealed partial class SaveWording
{
    public const string TryLoad = "try_load";
    public const string NoSaveSelectedKey = "NO_SAVE_SELECTED";

    private static readonly HashSet<string> LoadPolicies = ["try_after_backup", "review_first", "do_not_load", "not_applicable"];
    private static readonly Lazy<SaveWording> SharedCatalog = new(() => new SaveWording(Resource("save_wording.json"), Resource("region_catalogue.json")));

    [GeneratedRegex(@"\{([a-z][a-z_0-9]*)\}")]
    private static partial Regex Placeholder();

    [GeneratedRegex(@"^[A-Z]{3}\d{2}/(PASS|INFO|WARN|ERROR|FATAL)$")]
    private static partial Regex FindingKey();

    [GeneratedRegex(@"[\x00-\x1f\x7f-\x9f]")]
    private static partial Regex ControlCharacters();

    private readonly Dictionary<string, WordingEntry> _findings;
    private readonly Dictionary<string, WordingEntry> _summaries;
    private readonly Dictionary<int, MapLabel> _maps;

    public static SaveWording Shared => SharedCatalog.Value;

    public SaveWording(string wordingJson, string regionJson)
    {
        using var wording = JsonDocument.Parse(wordingJson);
        var root = wording.RootElement;
        if (root.GetProperty("schema_version").GetInt32() != 1)
            throw new InvalidDataException("U9 save wording: unsupported schema version");
        Actions = root.GetProperty("actions").EnumerateObject().ToDictionary(a => a.Name, a =>
            new WordingAction(a.Name, a.Value.GetProperty("label").GetString()!, a.Value.GetProperty("description").GetString()!));
        AllowedLoadResults = ValidateTryLoad(root.GetProperty("actions").GetProperty(TryLoad));
        _findings = root.GetProperty("findings").EnumerateObject().ToDictionary(f => f.Name, f => Entry(f.Name, f.Value));
        foreach (string key in _findings.Keys)
            if (!FindingKey().IsMatch(key)) throw new InvalidDataException($"U9 save wording: invalid finding key {key}");
        _summaries = root.GetProperty("summaries").EnumerateObject().ToDictionary(f => f.Name, f => Entry(f.Name, f.Value));
        if (!_summaries.Keys.ToHashSet().SetEquals([Severity.Pass, Severity.Warn, Severity.Error, Severity.Fatal, NoSaveSelectedKey]))
            throw new InvalidDataException("U9 save wording: incomplete overall summaries");
        Unknown = Entry("unknown", root.GetProperty("unknown_finding"));
        SelfReferenceNote = root.GetProperty("self_reference_note").GetString()!;

        using var regions = JsonDocument.Parse(regionJson);
        _maps = regions.RootElement.GetProperty("maps").EnumerateObject().ToDictionary(
            m => int.Parse(m.Name, CultureInfo.InvariantCulture), m => Label(int.Parse(m.Name, CultureInfo.InvariantCulture), m.Value));
    }

    public IReadOnlyDictionary<string, WordingAction> Actions { get; }

    public IReadOnlySet<string> AllowedLoadResults { get; }

    public WordingEntry Unknown { get; }
    public string SelfReferenceNote { get; }

    public WordingEntry Summary(string result) => _summaries[result];

    public WordingEntry Finding(string key) => _findings.GetValueOrDefault(key, Unknown);

    public bool HasFinding(string key) => _findings.ContainsKey(key);

    public MapLabel Map(int number) => _maps.TryGetValue(number, out var label) ? label
        : new MapLabel(number, $"Map {number}", "unknown", false, $"Map {number} (unidentified)");

    public string Fill(string template, IReadOnlyDictionary<string, object?> details) => Placeholder().Replace(template, match =>
    {
        string name = match.Groups[1].Value;
        if (!details.TryGetValue(name, out object? value))
            throw new InvalidDataException($"U9 save wording: missing detail {name}");
        if (name == "maps")
        {
            var names = ((System.Collections.IEnumerable)value!).Cast<object>()
                .Select(m => Map(Convert.ToInt32(m, CultureInfo.InvariantCulture)).DisplayName).ToList();
            return names.Count > 0 ? string.Join(", ", names) : "not specified";
        }
        return ControlCharacters().Replace(value == null ? "unknown" : Convert.ToString(value, CultureInfo.InvariantCulture) ?? "", " ");
    });

    public SavePresentation Describe(IntegrityReport report)
    {
        string result = report.NoSaveSelected ? NoSaveSelectedKey : report.Overall;
        var entry = Summary(result);
        var summary = new PlainSummary(result, entry.Title, entry.Meaning, entry.LoadPolicy, entry.LoadText, entry.WhatToDo, entry.Actions);
        bool blocks = report.Overall is Severity.Error or Severity.Fatal;
        bool mayLoad = AllowedLoadResults.Contains(report.Overall);

        var cards = new List<PlainCard>();
        if (!report.NoSaveSelected)
        {
            var groups = report.Findings
                .Where(f => f.Severity != Severity.Pass && !(report.SelfReference && f.CheckId == "LAY01"))
                .GroupBy(f => (f.CheckId, f.Severity))
                .OrderBy(g => -Severity.Rank(g.Key.Severity)).ThenBy(g => g.Key.CheckId, StringComparer.Ordinal);
            foreach (var group in groups)
            {
                var findings = group.ToList();
                string key = findings[0].Key;
                var wording = Finding(key);
                bool affects = findings.Any(f => f.AffectsVerdict);
                cards.Add(new PlainCard(key, group.Key.CheckId, group.Key.Severity, affects, affects ? "finding" : "note",
                    wording.Title,
                    findings.Select(f => Fill(wording.Meaning, f.Details)).Distinct().ToList(),
                    blocks && wording.LoadPolicy != "do_not_load" && summary.WhatToDo != null
                        ? summary.WhatToDo : Fill(wording.WhatToDo ?? "", findings[0].Details),
                    wording.LoadPolicy,
                    blocks ? summary.LoadPolicy : wording.LoadPolicy,
                    blocks ? summary.LoadText : wording.LoadText,
                    wording.Actions.Where(a => a != TryLoad || mayLoad).ToList(),
                    findings));
            }
        }
        var actions = summary.Actions.Concat(cards.SelectMany(c => c.Actions)).Distinct()
                                     .Where(a => a != TryLoad || !blocks).ToList();
        return new SavePresentation(summary, cards, actions, report.SelfReference ? [SelfReferenceNote] : []);
    }

    public static string Render(SavePresentation plain)
    {
        var lines = new List<string> { plain.Summary.Title, plain.Summary.Meaning, plain.Summary.LoadText };
        lines.AddRange(plain.Notes);
        foreach (var card in plain.Cards)
        {
            lines.Add("");
            lines.Add($"{(card.IsNote ? "Note: " : "")}{card.Title} ({card.Key})");
            lines.AddRange(card.Meanings.Select(m => $"  What this means: {m}"));
            lines.Add($"  What you can do: {card.WhatToDo}");
            lines.Add($"  Loading: {card.LoadText}");
        }
        return string.Join("\n", lines);
    }

    private WordingEntry Entry(string key, JsonElement e)
    {
        string policy = e.GetProperty("load_policy").GetString()!;
        if (!LoadPolicies.Contains(policy)) throw new InvalidDataException($"U9 save wording: invalid loading policy for {key}");
        var actions = Strings(e, "actions");
        if (actions.Any(a => !Actions.ContainsKey(a))) throw new InvalidDataException($"U9 save wording: unknown action for {key}");
        bool affects = !e.TryGetProperty("affects_verdict", out var flag) || flag.ValueKind switch
        {
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            _ => throw new InvalidDataException($"U9 save wording: invalid verdict flag for {key}"),
        };
        string kind = e.TryGetProperty("display_kind", out var k) ? k.GetString()! : "finding";
        if (kind is not ("finding" or "note")) throw new InvalidDataException($"U9 save wording: invalid display kind for {key}");

        string? Text(string name) => e.TryGetProperty(name, out var v) ? v.GetString() : null;
        var entry = new WordingEntry(Text("title")!, Text("meaning")!, Text("what_to_do"), policy, Text("load_text")!, actions,
                                     e.TryGetProperty("placeholders", out _) ? Strings(e, "placeholders") : [], affects, kind);
        var found = new HashSet<string>();
        foreach (string? text in new[] { entry.Title, entry.Meaning, entry.WhatToDo, entry.LoadText })
        {
            if (text == null) continue;
            found.UnionWith(Placeholder().Matches(text).Select(m => m.Groups[1].Value));
            string rest = Placeholder().Replace(text, "");
            if (rest.Contains('{') || rest.Contains('}')) throw new InvalidDataException($"U9 save wording: only plain placeholders allowed for {key}");
        }
        if (!found.SetEquals(entry.Placeholders)) throw new InvalidDataException($"U9 save wording: placeholder declaration mismatch for {key}");
        return entry;
    }

    private static HashSet<string> ValidateTryLoad(JsonElement e)
    {
        bool Flag(string name) => e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.True;
        var allowed = Strings(e, "allowed_overall_results").ToHashSet();
        if (!Flag("requires_explicit_activation") || !Flag("launch_on_activation_allowed")
            || !e.TryGetProperty("automatic_launch_allowed", out var auto) || auto.ValueKind != JsonValueKind.False
            || !allowed.SetEquals([Severity.Pass, Severity.Warn]))
            throw new InvalidDataException("U9 save wording: unsafe try_load activation contract");
        return allowed;
    }

    private static List<string> Strings(JsonElement e, string name) =>
        e.TryGetProperty(name, out var list) ? list.EnumerateArray().Select(s => s.GetString()!).ToList() : [];

    private static MapLabel Label(int number, JsonElement e)
    {
        string name = e.GetProperty("name").GetString()!;
        string shown = e.TryGetProperty("player_name", out var player) ? player.GetString()! : name;
        string category = e.GetProperty("category").GetString()!;
        bool development = e.GetProperty("development").GetBoolean();
        string suffix = development ? " (development/test)" : category == "unresolved" ? " (classification uncertain)" : "";
        return new MapLabel(number, name, category, development, $"{shown} [map {number}]{suffix}");
    }

    private static string Resource(string name)
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream($"U9GL.Core.Saves.Wording.{name}")
            ?? throw new InvalidOperationException($"{name} is not embedded in U9GL.Core");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
