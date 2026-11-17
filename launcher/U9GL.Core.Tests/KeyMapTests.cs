using System.Security.Cryptography;
using System.Text;
using U9GL.Core.Install;
using U9GL.Core.Settings;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class KeyMapTests
{
    private const string Stock =
        "; a key map for the tests\r\n" +
        "; ;= would bind the semicolon key\r\n" +
        "\r\n" +
        "[MOVEMENT COMMANDS]\r\n" +
        "space       = jump\r\n" +
        "Left        = sidestep_left\r\n" +
        "Right       = sidestep_right\r\n" +
        "Up          = forward\r\n" +
        "Down        = backward\r\n" +
        "\r\n" +
        "[INTERACTION COMMANDS]\r\n" +
        "q       = activate_hand ; hand mode\r\n" +
        "s\t= open_spellbook\t\r\n" +
        "ctrl+s = journal_save\r\n" +
        "\r\n" +
        "[COMBAT COMMANDS]\r\n" +
        "t      = target_lock\r\n" +
        "a      = abort_targeting \t; quit targeting\r\n" +
        "\r\n" +
        "[CHEAT COMMANDS]\r\n" +
        "alt+shift+a = toggle_avatar_invulnerable\r\n";

    [TestMethod]
    public void StockReadsOffAndOnReadsOn()
    {
        Assert.AreEqual(KeyMap.Off, KeyMap.State(Stock));
        Assert.AreEqual(KeyMap.On, KeyMap.State(KeyMap.Apply(Stock, on: true)));
    }

    [TestMethod]
    public void OnBindsWasdAndMovesTheTwoInTheWay()
    {
        string on = KeyMap.Apply(Stock, on: true);
        var lines = on.Split("\r\n");

        foreach (string expected in new[] { "w\t= forward", "a\t= sidestep_left", "s\t= backward", "d\t= sidestep_right" })
            Assert.IsTrue(lines.Any(l => l.StartsWith(expected, StringComparison.Ordinal)), expected);
        Assert.IsTrue(lines.Any(l => l.StartsWith("e\t= open_spellbook", StringComparison.Ordinal) && l.Contains(KeyMap.Marker)));
        Assert.IsTrue(lines.Any(l => l.StartsWith("; a      = abort_targeting \t; quit targeting\t" + KeyMap.Marker, StringComparison.Ordinal)),
            "the dead binding commented out, not moved");
        Assert.IsFalse(lines.Any(l => l.TrimStart().StartsWith('r')), "R stays free");
        Assert.IsFalse(lines.Any(l => l.StartsWith("s\t= open_spellbook", StringComparison.Ordinal)));

        int down = Array.IndexOf(lines, "Down        = backward");
        Assert.IsTrue(lines[down + 1].StartsWith("w\t= forward", StringComparison.Ordinal));
        Assert.AreEqual("[INTERACTION COMMANDS]", lines[down + 6]);

        StringAssert.Contains(on, "Up          = forward\r\n");
        StringAssert.Contains(on, "ctrl+s = journal_save\r\n");
        StringAssert.Contains(on, "alt+shift+a = toggle_avatar_invulnerable\r\n");
        StringAssert.Contains(on, "; ;= would bind the semicolon key\r\n");
    }

    [TestMethod]
    public void OffAfterOnGivesBackTheBindings()
    {
        string back = KeyMap.Apply(KeyMap.Apply(Stock, on: true), on: false);
        Assert.AreEqual(KeyMap.Off, KeyMap.State(back));
        StringAssert.Contains(back, "s\t= open_spellbook\r\n");
        StringAssert.Contains(back, "a      = abort_targeting \t; quit targeting\r\n");
        Assert.IsFalse(back.Contains(KeyMap.Marker, StringComparison.Ordinal));
        Assert.AreEqual(Stock.Replace("s\t= open_spellbook\t\r\n", "s\t= open_spellbook\r\n"), back,
            "only trailing white space differs from the original");
    }

    [TestMethod]
    public void SwitchingTwiceChangesNothingMore()
    {
        string on = KeyMap.Apply(Stock, on: true);
        Assert.AreEqual(on, KeyMap.Apply(on, on: true));
        string off = KeyMap.Apply(on, on: false);
        Assert.AreEqual(off, KeyMap.Apply(off, on: false));
    }

    [TestMethod]
    public void OnRefusesAKeyThePlayerUsesForSomethingElse()
    {
        string custom = Stock.Replace("t      = target_lock", "e      = target_lock");
        var e = Assert.ThrowsExactly<KeyMapException>(() => KeyMap.Apply(custom, on: true));
        StringAssert.Contains(e.Message, "e is bound to target_lock");

        custom = Stock.Replace("space       = jump", "w           = jump");
        e = Assert.ThrowsExactly<KeyMapException>(() => KeyMap.Apply(custom, on: true));
        StringAssert.Contains(e.Message, "w is bound to jump");
    }

    [TestMethod]
    public void AHandMadeLayoutIsMixedOrOnAndOffLeavesItsOwnLines()
    {
        string partial = Stock.Replace("space       = jump", "space       = jump\r\nw = forward");
        Assert.IsNull(KeyMap.State(partial), "only one of the four");

        string hand = Stock.Replace("s\t= open_spellbook\t", "e = open_spellbook")
                           .Replace("a      = abort_targeting", "r      = abort_targeting")
                           .Replace("Down        = backward", "Down        = backward\r\nw = forward\r\na = sidestep_left\r\ns = backward\r\nd = sidestep_right");
        Assert.AreEqual(KeyMap.On, KeyMap.State(hand));
        string off = KeyMap.Apply(hand, on: false);
        Assert.AreEqual(KeyMap.Off, KeyMap.State(off));
        StringAssert.Contains(off, "e = open_spellbook\r\n");
    }

    [TestMethod]
    public void RIsNoLongerNeeded()
    {
        string custom = Stock.Replace("t      = target_lock", "r      = target_lock");
        Assert.AreEqual(KeyMap.On, KeyMap.State(KeyMap.Apply(custom, on: true)));
        StringAssert.Contains(KeyMap.Apply(custom, on: true), "r      = target_lock\r\n");
    }

    [TestMethod]
    public void AnOlderOnWithAbortTargetingOnRConverts()
    {
        string older = KeyMap.Apply(Stock, on: true)
            .Replace("; a      = abort_targeting \t; quit targeting\t" + KeyMap.Marker + ", not a command in u9.exe",
                     "r      = abort_targeting \t; quit targeting\t" + KeyMap.Marker + ", was a");
        StringAssert.Contains(older, "r      = abort_targeting");

        string off = KeyMap.Apply(older, on: false);
        StringAssert.Contains(off, "a      = abort_targeting \t; quit targeting\r\n");
        Assert.IsFalse(off.Contains(KeyMap.Marker, StringComparison.Ordinal));

        Assert.AreEqual(KeyMap.Apply(Stock, on: true), KeyMap.Apply(older, on: true), "On rewrites it as the commented line");
    }

    [TestMethod]
    public void ACommandLeftWithNoKeyGetsItsStockKeyBack()
    {
        string none = Stock.Replace("s\t= open_spellbook\t\r\n", "");
        string off = KeyMap.Apply(KeyMap.Apply(none, on: true), on: false);
        StringAssert.Contains(off, "s\t= open_spellbook");
    }

    [TestMethod]
    public void LfFilesStayLf()
    {
        string lf = Stock.Replace("\r\n", "\n");
        string on = KeyMap.Apply(lf, on: true);
        Assert.IsFalse(on.Contains('\r'));
        Assert.IsTrue(on.EndsWith('\n'));
    }

    private string _root = "";
    private GameFolder _folder = null!;

    [TestInitialize]
    public void CreateFolder()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-keymap-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(_root, "u9gl"));
        File.WriteAllText(Path.Combine(_root, "u9.exe"), "a fake u9.exe");
        File.WriteAllBytes(Path.Combine(_root, "Options.ini"),
            Encoding.Latin1.GetBytes("[Interface]\r\nCurrent_Keymap=default.kmp\r\n[Game]\r\nGame Rasterizer=2\r\n"));
        File.WriteAllBytes(Path.Combine(_root, "Default.kmp"), Encoding.Latin1.GetBytes(Stock));
        _folder = new GameFolder(_root);
        File.WriteAllText(_folder.OurDll, "U9GL 0.1.0 u9log");
        File.WriteAllText(_folder.BundledLoader, "U9GL 0.1.0 loader");
        File.WriteAllText(_folder.DefaultsIni, "# u9gl.ini defaults 0.1.0\ngl_window_size = 0\n");
    }

    [TestCleanup]
    public void DeleteFolder()
    {
        try { Directory.Delete(_root, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    private Deployer NewDeployer()
    {
        string exe = Path.Combine(_root, "u9.exe");
        var known = new[] { new KnownBuild("test", Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(exe))), new FileInfo(exe).Length) };
        return new Deployer(_folder, Version.Parse("0.1.0"), new KeyMapFakeVersions(), new KeyMapNoGame(), () => DateTime.UtcNow, known);
    }

    private string KeyMapText => File.ReadAllText(Path.Combine(_root, "Default.kmp"), Encoding.Latin1);

    private static SettingWrite Wasd(string value) => new(SettingFile.KeyMap, null, SettingsCatalog.WasdMovementKey, value);

    [TestMethod]
    public void InstallBacksUpTheKeyMapOnce()
    {
        NewDeployer().Install();
        NewDeployer().Install();
        var manifest = InstallManifest.Load(_folder.Manifest)!;
        var backup = manifest.Backups.Single(b => b.OriginalPath.Equals("Default.kmp", StringComparison.OrdinalIgnoreCase));
        Assert.AreEqual(Stock, File.ReadAllText(_folder.Absolute(backup.BackupPath), Encoding.Latin1));
        Assert.AreEqual(Stock, KeyMapText, "Install itself does not change the key map");
    }

    [TestMethod]
    public void SavingTheSettingSetsAndResetsTheKeys()
    {
        NewDeployer().Install();
        NewDeployer().SaveSettings([Wasd("1")]);
        Assert.AreEqual(KeyMap.On, KeyMap.State(KeyMapText));
        Assert.AreEqual(KeyMap.On, SettingsReader.Read(_folder).Single(s => s.Definition.Id == "wasd").Values[SettingsCatalog.WasdMovementKey]);

        NewDeployer().SaveSettings([Wasd("0")]);
        Assert.AreEqual(KeyMap.Off, KeyMap.State(KeyMapText));
        StringAssert.Contains(KeyMapText, "s\t= open_spellbook");

        var manifest = InstallManifest.Load(_folder.Manifest)!;
        Assert.IsTrue(manifest.Backups.Count(b => b.OriginalPath.Equals("Default.kmp", StringComparison.OrdinalIgnoreCase)) >= 2,
            "backed up at Install, and by each later launcher session before its first change");
    }

    [TestMethod]
    public void TheFileKeepsItsOwnSpelling()
    {
        NewDeployer().SaveSettings([Wasd("1")]);
        Assert.AreEqual("Default.kmp", Path.GetFileName(Directory.GetFiles(_root, "*.kmp").Single()));
    }

    [TestMethod]
    public void ARefusedLayoutChangesNothing()
    {
        File.WriteAllBytes(Path.Combine(_root, "Default.kmp"), Encoding.Latin1.GetBytes(Stock.Replace("t      = target_lock", "e      = target_lock")));
        string before = KeyMapText;
        var e = Assert.ThrowsExactly<DeployException>(() => NewDeployer().SaveSettings([Wasd("1")]));
        Assert.AreEqual(DeployProblem.KeyMapUnusable, e.Problem);
        Assert.AreEqual(before, KeyMapText);
    }

    [TestMethod]
    public void UninstallTakesTheLaunchersLinesOut()
    {
        NewDeployer().Install();
        NewDeployer().SaveSettings([Wasd("1")]);
        NewDeployer().Uninstall();
        Assert.AreEqual(KeyMap.Off, KeyMap.State(KeyMapText));
        Assert.IsFalse(KeyMapText.Contains(KeyMap.Marker, StringComparison.Ordinal));
    }

    [TestMethod]
    public void TheKeyMapOptionsIniNamesIsTheOneEdited()
    {
        File.WriteAllBytes(Path.Combine(_root, "Options.ini"),
            Encoding.Latin1.GetBytes("[Interface]\r\nCurrent_Keymap=mine.kmp\r\n[Game]\r\nGame Rasterizer=2\r\n"));
        File.WriteAllBytes(Path.Combine(_root, "mine.kmp"), Encoding.Latin1.GetBytes(Stock));
        NewDeployer().SaveSettings([Wasd("1")]);
        Assert.AreEqual(KeyMap.On, KeyMap.State(File.ReadAllText(Path.Combine(_root, "mine.kmp"), Encoding.Latin1)));
        Assert.AreEqual(Stock, KeyMapText);
    }

    [TestMethod]
    public void AKeyMapOutsideTheGameFolderIsRefused()
    {
        File.WriteAllBytes(Path.Combine(_root, "Options.ini"),
            Encoding.Latin1.GetBytes("[Interface]\r\nCurrent_Keymap=..\\elsewhere.kmp\r\n[Game]\r\nGame Rasterizer=2\r\n"));
        Assert.IsNull(KeyMap.PathFor(_folder));
        Assert.ThrowsExactly<DeployException>(() => NewDeployer().SaveSettings([Wasd("1")]));
    }
}

internal sealed class KeyMapFakeVersions : IFileVersionReader
{
    public ComponentVersion Read(string path)
    {
        if (!File.Exists(path)) return ComponentVersion.Missing;
        string text = File.ReadAllText(path);
        if (!text.StartsWith("U9GL ", StringComparison.Ordinal)) return new ComponentVersion(true, false, null, null);
        string v = text.Split(' ')[1];
        return new ComponentVersion(true, true, Version.Parse(v), v);
    }
}

internal sealed class KeyMapNoGame : IGameProcessProbe
{
    public bool IsRunning(GameFolder folder) => false;
}
