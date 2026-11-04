using System.Text;
using U9GL.Core.Install;
using U9GL.Core.Settings;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class SettingsReaderTests
{
    private string _root = "";
    private GameFolder _folder = null!;

    [TestInitialize]
    public void CreateFolder()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-core-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(_root, "u9gl"));
        _folder = new GameFolder(_root);
    }

    [TestCleanup]
    public void DeleteFolder()
    {
        try { Directory.Delete(_root, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    private static CurrentSetting Setting(IReadOnlyList<CurrentSetting> all, string id) => all.Single(s => s.Definition.Id == id);

    [TestMethod]
    public void CurrentValuesMatchTheirChoices()
    {
        File.WriteAllBytes(_folder.OptionsIni, Encoding.Latin1.GetBytes(
            "[Game]\r\nGameScreenWidth=1920\r\nGameScreenHeight=1080\r\nGameMipMapLevel=1\r\nDisableWinKey=1\r\n"));
        File.WriteAllText(_folder.U9glIni, "gl_window_size = 0\n# ==== U9GL launcher settings\ngl_window_size = 2\nmetrics_overlay = 1\n");

        var all = SettingsReader.Read(_folder);
        Assert.AreEqual("1920 × 1080", Setting(all, "resolution").Choice?.Label);
        Assert.AreEqual("Whole monitor, borderless", Setting(all, "window-size").Choice?.Label, "the last line wins");
        Assert.AreEqual("Disabled while playing", Setting(all, "windows-key").Choice?.Label);
        Assert.AreEqual("Full (the original)", Setting(all, "texture-detail").Choice?.Label);
        Assert.IsFalse(Setting(all, "window-anchor").IsSet);
        Assert.IsNull(Setting(all, "window-anchor").Choice);
    }

    [TestMethod]
    public void AnUnlistedValueHasNoChoiceButKeepsItsValues()
    {
        File.WriteAllBytes(_folder.OptionsIni, Encoding.Latin1.GetBytes("[Game]\r\nGameScreenWidth=1234\r\nGameScreenHeight=567\r\n"));
        var resolution = Setting(SettingsReader.Read(_folder), "resolution");
        Assert.IsNull(resolution.Choice);
        Assert.AreEqual("1234", resolution.Values["GameScreenWidth"]);
        Assert.AreEqual("567", resolution.Values["GameScreenHeight"]);
    }

    [TestMethod]
    public void BeforeInstallTheDefaultsFileStandsIn()
    {
        File.WriteAllText(_folder.DefaultsIni, "# u9gl.ini defaults 0.1.0\ngl_window_size = 1\n");
        Assert.AreEqual("The game's resolution", Setting(SettingsReader.Read(_folder), "window-size").Choice?.Label);
    }

    [TestMethod]
    public void BeforeInstallTheReleaseProfileIsReadOverTheDefaults()
    {
        File.WriteAllText(_folder.DefaultsIni, "# u9gl.ini defaults 0.1.0\ngl_window_size = 1\n");
        File.WriteAllText(_folder.ProfileIni, "gl_window_size = 2\n");
        Assert.AreEqual("Whole monitor, borderless", Setting(SettingsReader.Read(_folder), "window-size").Choice?.Label);

        File.WriteAllText(_folder.U9glIni, "# ==== U9GL launcher settings\ngl_window_size = 0\n");
        Assert.AreEqual("Fixed 1280 × 960", Setting(SettingsReader.Read(_folder), "window-size").Choice?.Label,
            "once installed, u9gl.ini says what is in force, not the release profile");
    }

    [TestMethod]
    public void KeysTheDiagIniSetsAreMarkedAsOverriding()
    {
        File.WriteAllText(_folder.U9glIni, "# ==== U9GL launcher settings\ngl_light_pools = 0\ngl_light_shadows = 0\ngl_window_size = 1\n");
        File.WriteAllText(_folder.DiagIni, "diag_light_watch = 1\ngl_light_pools = 1\n");
        var all = SettingsReader.Read(_folder);
        var lamp = Setting(all, "lamp-light");
        CollectionAssert.AreEqual(new[] { "gl_light_pools" }, lamp.Overridden.ToArray());
        Assert.AreEqual("1", lamp.Values["gl_light_pools"], "the diag value is the one in force");
        Assert.AreEqual(0, Setting(all, "window-size").Overridden.Count);
    }

    [TestMethod]
    public void AKeyAnOlderU9glIniLacksReadsAsTheShippedDefault()
    {
        File.WriteAllText(_folder.DefaultsIni, "# u9gl.ini defaults 0.4.1\ngl_night_grade = 100\ngl_window_size = 1\n");
        File.WriteAllText(_folder.U9glIni, "# ==== U9GL launcher settings\ngl_window_size = 0\n");
        var all = SettingsReader.Read(_folder);
        Assert.AreEqual("On", Setting(all, "night-grade").Choice?.Label);
        Assert.AreEqual("Fixed 1280 × 960", Setting(all, "window-size").Choice?.Label, "the installed value still wins");
    }

    [TestMethod]
    public void TheDiagFileIsReadOverU9glIni()
    {
        File.WriteAllText(_folder.U9glIni, "# ==== U9GL launcher settings\nui_watch = 0\nfix_npc_watch = 1\n");
        File.WriteAllText(_folder.DiagIni, "ui_watch = 1\n");

        var all = SettingsReader.Read(_folder);
        Assert.AreEqual("On", Setting(all, "ui_watch").Choice?.Label, "u9gl.diag.ini wins, as in config.c");
        Assert.AreEqual("On", Setting(all, "diag_npc_watch").Choice?.Label, "an old name reads as the new one");
        Assert.IsNull(Setting(all, "diag_bag_watch").Choice, "absent: the DLL's default applies");
    }

    [TestMethod]
    public void ManagedSettingsAreNotRead()
    {
        Assert.IsFalse(SettingsReader.Read(_folder).Any(s => s.Definition.Page == SettingPage.Managed));
    }
}
