using System.Text.Json;
using U9GL.Core.Saves;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class SaveWordingTests
{
    private static readonly string ExamplesPath = Path.Combine(AppContext.BaseDirectory, "Data", "save_browser_examples.json");

    public static IEnumerable<object[]> Cases()
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(ExamplesPath));
        foreach (string group in new[] { "cases", "self_reference_cases" })
            foreach (var c in doc.RootElement.GetProperty(group).EnumerateObject())
                yield return [group, c.Name];
    }

    [TestMethod]
    [DynamicData(nameof(Cases))]
    public void KnownSavesReadAsTitanReadsThem(string group, string name)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(ExamplesPath));
        var expected = doc.RootElement.GetProperty(group).GetProperty(name);
        var report = new IntegrityReport("fixture")
        {
            Slot = expected.GetProperty("slot").GetInt32(),
            CustodyApplicable = false,
            SelfReference = expected.TryGetProperty("self_reference", out var self) && self.GetBoolean(),
        };
        foreach (var card in expected.GetProperty("cards").EnumerateArray())
        {
            string checkId = card.GetProperty("check_id").GetString()!, severity = card.GetProperty("severity").GetString()!;
            bool affects = card.GetProperty("affects_verdict").GetBoolean();
            foreach (var context in card.GetProperty("contexts").EnumerateArray())
            {
                var details = context.EnumerateObject().Select(p => (p.Name, Value(p.Value))).ToArray();
                if (affects) report.Add(checkId, Axis.Structure, severity, "irrelevant technical text", null, null, details);
                else report.AddNote(checkId, Axis.Structure, severity, "irrelevant technical text", null, null, details);
            }
        }
        Assert.AreEqual(expected.GetProperty("overall").GetString(), report.Overall);

        var plain = SaveWording.Shared.Describe(report);
        var summary = expected.GetProperty("summary");
        Assert.AreEqual(summary.GetProperty("result").GetString(), plain.Summary.Result);
        Assert.AreEqual(summary.GetProperty("title").GetString(), plain.Summary.Title);
        Assert.AreEqual(summary.GetProperty("load_text").GetString(), plain.Summary.LoadText);
        CollectionAssert.AreEqual(Strings(expected, "actions"), plain.Actions.ToList(), "overall actions");

        var cards = expected.GetProperty("cards").EnumerateArray().ToList();
        CollectionAssert.AreEqual(cards.Select(c => c.GetProperty("key").GetString()).ToList(), plain.Cards.Select(c => c.Key).ToList());
        foreach (var (want, got) in cards.Zip(plain.Cards))
        {
            Assert.AreEqual(want.GetProperty("title").GetString(), got.Title, got.Key);
            CollectionAssert.AreEqual(Strings(want, "meanings"), got.Meanings.ToList(), got.Key);
            Assert.AreEqual(want.GetProperty("what_to_do").GetString(), got.WhatToDo, got.Key);
            Assert.AreEqual(want.GetProperty("finding_load_policy").GetString(), got.FindingLoadPolicy, got.Key);
            Assert.AreEqual(want.GetProperty("load_policy").GetString(), got.LoadPolicy, got.Key);
            Assert.AreEqual(want.GetProperty("load_text").GetString(), got.LoadText, got.Key);
            Assert.AreEqual(want.GetProperty("display_kind").GetString(), got.DisplayKind, got.Key);
            CollectionAssert.AreEqual(Strings(want, "actions"), got.Actions.ToList(), got.Key);
        }
        Assert.AreEqual(expected.GetProperty("plain_text").GetString(), SaveWording.Render(plain));
    }

    [TestMethod]
    public void TheBrokenSaveOffersNoWayToTryLoadingIt()
    {
        foreach (var row in Cases())
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(ExamplesPath));
            var c = doc.RootElement.GetProperty((string)row[0]).GetProperty((string)row[1]);
            if (c.GetProperty("overall").GetString() is not (Severity.Error or Severity.Fatal)) continue;
            CollectionAssert.DoesNotContain(Strings(c, "actions"), SaveWording.TryLoad);
            foreach (var card in c.GetProperty("cards").EnumerateArray())
                CollectionAssert.DoesNotContain(Strings(card, "actions"), SaveWording.TryLoad);
        }
    }

    [TestMethod]
    public void MapsAreNamedWithTheirNumberAndNeverGuessed()
    {
        var wording = SaveWording.Shared;
        Assert.AreEqual("Britannia [map 9]", wording.Map(9).DisplayName);
        Assert.AreEqual("Moonglow [map 4] (development/test)", wording.Map(4).DisplayName);
        Assert.AreEqual("Stonegate [map 41]", wording.Map(41).DisplayName);
        Assert.AreEqual("Stonegate Fix", wording.Map(41).Name);
        Assert.AreEqual("Map 999 (unidentified)", wording.Map(999).DisplayName);
    }

    [TestMethod]
    public void AnUnknownCheckGetsTheConservativeEntryNotItsFamilys()
    {
        var report = new IntegrityReport("fixture") { Slot = 4 };
        report.Add("NFS99", Axis.Structure, Severity.Warn, "new check");
        var card = SaveWording.Shared.Describe(report).Cards.Single();
        Assert.AreEqual(SaveWording.Shared.Unknown.Title, card.Title);
        Assert.IsFalse(SaveWording.Shared.HasFinding("NFS99/WARN"));
    }

    [TestMethod]
    public void AMissingPlaceholderValueIsAContractError()
    {
        var details = new Dictionary<string, object?> { ["slot"] = 4 };
        Assert.ThrowsExactly<InvalidDataException>(() => SaveWording.Shared.Fill("{count} areas", details));
        Assert.AreEqual("not specified", SaveWording.Shared.Fill("{maps}", new Dictionary<string, object?> { ["maps"] = new List<int>() }));
        Assert.AreEqual("unknown", SaveWording.Shared.Fill("{slot}", new Dictionary<string, object?> { ["slot"] = null }));
    }

    [TestMethod]
    public void NoSaveSelectedHasOnlyItsSummary()
    {
        var report = new IntegrityReport("fixture") { NoSaveSelected = true };
        report.Add("SEL01", Axis.Custody, Severity.Fatal, "missing");
        var plain = SaveWording.Shared.Describe(report);
        Assert.AreEqual(SaveWording.NoSaveSelectedKey, plain.Summary.Result);
        Assert.AreEqual(0, plain.Cards.Count);
        CollectionAssert.AreEqual(new[] { "choose_save" }, plain.Actions.ToList());
    }

    [TestMethod]
    public void AnUnsafeTryLoadContractIsRefused()
    {
        string wording = Embedded("save_wording.json").ReplaceLineEndings("\n"), regions = Embedded("region_catalogue.json");
        _ = new SaveWording(wording, regions);
        string automatic = wording.Replace("\"automatic_launch_allowed\": false", "\"automatic_launch_allowed\": true");
        Assert.AreNotEqual(wording, automatic);
        Assert.ThrowsExactly<InvalidDataException>(() => new SaveWording(automatic, regions));
        int at = wording.IndexOf("\"allowed_overall_results\"", StringComparison.Ordinal);
        int pass = wording.IndexOf("\"PASS\"", at, StringComparison.Ordinal);
        string onFatal = wording[..pass] + "\"FATAL\"" + wording[(pass + "\"PASS\"".Length)..];
        Assert.ThrowsExactly<InvalidDataException>(() => new SaveWording(onFatal, regions));
    }

    private static string Embedded(string name)
    {
        using var stream = typeof(SaveWording).Assembly.GetManifestResourceStream($"U9GL.Core.Saves.Wording.{name}")!;
        return new StreamReader(stream).ReadToEnd();
    }

    private static List<string> Strings(JsonElement e, string name) => e.GetProperty(name).EnumerateArray().Select(s => s.GetString()!).ToList();

    private static object? Value(JsonElement e) => e.ValueKind switch
    {
        JsonValueKind.Number => e.TryGetInt32(out int i) ? i : e.GetInt64(),
        JsonValueKind.Array => e.EnumerateArray().Select(x => x.GetInt32()).ToList(),
        JsonValueKind.String => e.GetString(),
        JsonValueKind.True => true,
        JsonValueKind.False => false,
        _ => null,
    };
}
