using U9GL.Core.Settings;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class CustomValuesTests
{
    [TestMethod]
    public void AResolutionStillTakesAWholeNumberFrom320To7680()
    {
        Assert.IsNull(CustomValues.Check("GameScreenWidth", " 1920 ", out string value));
        Assert.AreEqual("1920", value);
        Assert.IsNotNull(CustomValues.Check("GameScreenWidth", "319", out _));
        Assert.IsNotNull(CustomValues.Check("GameScreenHeight", "7681", out _));
        Assert.IsNotNull(CustomValues.Check("GameScreenHeight", "big", out _));
    }

    [TestMethod]
    [DataRow("Kestrel")]
    [DataRow("Lord Blackthorn")]
    [DataRow("Shamino's Friend")]
    [DataRow("Anne-Marie")]
    [DataRow("St. Elmo")]
    [DataRow("Ælfric")]
    [DataRow("Zoë")]
    public void TheseNamesAreAccepted(string name)
    {
        Assert.IsNull(CustomValues.Check("avatar_name", $"  {name} ", out string value), name);
        Assert.AreEqual(name, value, "trimmed, otherwise as typed");
    }

    [TestMethod]
    [DataRow("", "empty")]
    [DataRow("   ", "blank")]
    [DataRow("'Quoted", "does not start with a letter")]
    [DataRow("Name=Value", "an equals sign")]
    [DataRow("Tab\tName", "a control character")]
    [DataRow("Two  Spaces", "a double space")]
    [DataRow("Ŝabo", "a letter outside Latin-1, which the game's text cannot show")]
    [DataRow("ABCDEFGHIJKLMNOPQRSTUVWXY", "25 characters, past the DLL's 24")]
    public void TheseNamesAreRefused(string name, string why)
    {
        Assert.IsNotNull(CustomValues.Check("avatar_name", name, out _), why);
    }

    [TestMethod]
    public void EveryFreeValueOnAPageHasItsOwnChoiceWording()
    {
        foreach (var setting in SettingsCatalog.All.Where(d => d.CustomStatus is LedgerStatus.Works or LedgerStatus.Partial))
        {
            var (label, note) = CustomValues.AsChoice(setting.Keys[0].Key);
            Assert.AreNotEqual("A custom value", label, setting.Id);
            Assert.AreNotEqual("", note, setting.Id);
            Assert.IsFalse(setting.PageChoices.Any(c => c.Label == label), $"{setting.Id}: '{label}' is already a listed choice");
        }
    }

    [TestMethod]
    public void TheLimitIsTheDllsOwn()
    {
        string source = File.ReadAllText(Path.Combine(SettingsCatalogTests.RepositoryRoot(), "u9gl", "src", "fix", "avatarname.c"));
        StringAssert.Contains(source, $"#define NAME_MAX_CHARS {CustomValues.AvatarNameMax}");
        Assert.IsNull(CustomValues.Check("avatar_name", new string('A', CustomValues.AvatarNameMax), out _));
    }

    [TestMethod]
    public void TheSettingDefaultsToNormalGameplayAndOffersTheirOwnName()
    {
        var setting = SettingsCatalog.ById("avatar-name");
        Assert.AreEqual("", setting.Default.Values["avatar_name"], "empty: the DLL installs nothing");
        Assert.AreEqual(1, setting.Choices.Count, "normal gameplay, then the player's own name");
        Assert.IsFalse(setting.Choices.Any(c => c.Label.Contains("Grandor")));
        Assert.IsNotNull(setting.CustomStatus);
        Assert.AreEqual("Name", CustomValues.Label("avatar_name"));
        Assert.AreEqual("Or your own name:", CustomValues.Prompt("avatar_name"));
    }
}
