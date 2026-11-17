using U9GL.Core.Settings;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class NewPlayerProfileTests
{
    private static readonly SettingPage[] Carried = [SettingPage.Graphics, SettingPage.Game, SettingPage.Fixes];
    private static readonly string[] AlwaysDefault = ["metrics_overlay"];

    internal static string ProfilePath() =>
        Path.Combine(SettingsCatalogTests.RepositoryRoot(), "u9gl", "u9gl.profile.ini");

    internal static List<KeyValuePair<string, string>> ReadProfile()
    {
        var pairs = new List<KeyValuePair<string, string>>();
        foreach (string raw in File.ReadLines(ProfilePath()))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith('#') || line.StartsWith(';')) continue;
            int eq = line.IndexOf('=');
            if (eq <= 0) continue;
            pairs.Add(new(line[..eq].Trim().ToLowerInvariant(), line[(eq + 1)..].Trim()));
        }
        return pairs;
    }

    private static IEnumerable<(SettingDefinition Definition, string Key)> U9glKeys(Func<SettingPage, bool> page) =>
        SettingsCatalog.All.Where(d => page(d.Page))
            .SelectMany(d => d.Keys.Where(k => k.File == SettingFile.U9glIni).Select(k => (d, k.Key.ToLowerInvariant())));

    [TestMethod]
    public void HoldsEveryGraphicsGameAndFixesKeyAtOneOfItsValues()
    {
        var profile = ReadProfile();
        var values = profile.GroupBy(p => p.Key).ToDictionary(g => g.Key, g => g.Last().Value);
        var failures = new List<string>();
        foreach (var repeated in profile.GroupBy(p => p.Key).Where(g => g.Count() > 1))
            failures.Add($"{repeated.Key}: in it {repeated.Count()} times");
        foreach (var (definition, key) in U9glKeys(p => Carried.Contains(p)))
        {
            if (AlwaysDefault.Contains(key)) continue;
            string def = definition.Default.Values.TryGetValue(key, out var d) ? d : "?";
            if (!values.TryGetValue(key, out var value))
            {
                failures.Add($"{key}: missing - \"{definition.Title}\" ({definition.Page} page), catalog default {def}. " +
                             "Choose the value a new player starts with and add it.");
                continue;
            }
            bool known = definition.Choices.Any(c => c.Values.TryGetValue(key, out var v) &&
                                                     string.Equals(v, value, StringComparison.OrdinalIgnoreCase));
            if (!known && definition.CustomStatus == null)
                failures.Add($"{key} = {value}: not one of \"{definition.Title}\"'s values " +
                             $"({string.Join(", ", definition.Choices.Select(c => c.Values.TryGetValue(key, out var v) ? v : "?").Distinct())})");
        }
        Assert.AreEqual(0, failures.Count, "u9gl/u9gl.profile.ini, the new-player settings:\n" + string.Join("\n", failures));
    }

    [TestMethod]
    public void CarriesNoDisplayDiagnosticManagedOrAlwaysDefaultKey()
    {
        var keys = ReadProfile().Select(p => p.Key).ToHashSet();
        var failures = new List<string>();
        foreach (var (definition, key) in U9glKeys(p => !Carried.Contains(p)))
            if (keys.Contains(key))
                failures.Add($"{key}: \"{definition.Title}\" is on the {definition.Page} page - a new player gets its default, so it does not belong here");
        foreach (string key in keys)
        {
            if (AlwaysDefault.Contains(key))
                failures.Add($"{key}: every player starts at its default");
            else if (key.StartsWith("diag_", StringComparison.Ordinal) || key.StartsWith("stream_", StringComparison.Ordinal))
                failures.Add($"{key}: a diagnostic - a new player has every one off");
        }
        Assert.AreEqual(0, failures.Count, "u9gl/u9gl.profile.ini, the new-player settings:\n" + string.Join("\n", failures));
    }

    [TestMethod]
    public void KeysBeyondThePagesAreTheKnownOnes()
    {
        var catalog = SettingsCatalog.All.SelectMany(d => d.Keys).Select(k => k.Key.ToLowerInvariant()).ToHashSet();
        var beyond = ReadProfile().Select(p => p.Key).Where(k => !catalog.Contains(k)).OrderBy(k => k).ToList();
        CollectionAssert.AreEqual(new[] { "gl_moon_bloom" }, beyond,
            "keys in u9gl/u9gl.profile.ini that no launcher page writes - add a new one to this list on purpose: " +
            string.Join(", ", beyond));
    }
}
