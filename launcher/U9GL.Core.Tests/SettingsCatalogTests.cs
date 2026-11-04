using U9GL.Core.Settings;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class SettingsCatalogTests
{
    [TestMethod]
    public void DefaultsOfVisibleSettingsAreVerified()
    {
        foreach (var definition in SettingsCatalog.All.Where(d => d.Page != SettingPage.Managed))
            Assert.IsTrue(definition.Default.Status is LedgerStatus.Works or LedgerStatus.Partial,
                $"{definition.Id}: its default '{definition.DefaultLabel}' is {definition.Default.Status}");
    }

    [TestMethod]
    public void ManagedKeysAreNeverAlsoVisible()
    {
        var managed = SettingsCatalog.All.Where(d => d.Page == SettingPage.Managed).SelectMany(d => d.Keys).ToHashSet();
        var visible = SettingsCatalog.All.Where(d => d.Page != SettingPage.Managed).SelectMany(d => d.Keys);
        foreach (var key in visible)
            Assert.IsFalse(managed.Contains(key), $"{key.Key} is both managed and visible");
    }

    [TestMethod]
    public void BrokenChoicesNeverReachAPage()
    {
        foreach (var definition in SettingsCatalog.All)
        {
            Assert.IsFalse(definition.PageChoices.Any(c => c.Status == LedgerStatus.Broken), definition.Id);
            Assert.IsFalse(definition.ExperimentalChoices.Any(c => c.Status == LedgerStatus.Broken), definition.Id);
        }
    }

    [TestMethod]
    public void EveryChoiceWritesExactlyTheSettingsKeys()
    {
        foreach (var definition in SettingsCatalog.All)
        foreach (var choice in definition.Choices)
            CollectionAssert.AreEquivalent(definition.Keys.Select(k => k.Key).ToList(), choice.Values.Keys.ToList(),
                $"{definition.Id} '{choice.Label}'");
    }

    [TestMethod]
    public void DiagnosticsAreListedInTheDiagIniAtTheirDefaults()
    {
        string path = Path.Combine(RepositoryRoot(), "u9gl", "u9gl.diag.ini");
        var listed = U9GL.Core.Ini.U9glIni.Effective(File.ReadAllText(path));
        foreach (var definition in SettingsCatalog.All.Where(d => d.Page == SettingPage.Diagnostics))
        {
            string key = definition.Keys.Single().Key;
            Assert.IsTrue(listed.ContainsKey(key), $"{key} is on the Diagnostics page but not in u9gl.diag.ini");
            if (definition.DefaultLabel == "On")
                Assert.AreEqual(listed[key], definition.Default.Values[key], $"{key}: Install would not write the DLL's default");
        }
    }

    internal static string RepositoryRoot()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "VERSION")) && Directory.Exists(Path.Combine(dir.FullName, "u9gl")))
                return dir.FullName;
        throw new InvalidOperationException("The repository root (with VERSION and u9gl/) was not found above " + AppContext.BaseDirectory);
    }
}
