using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using U9GL.Core.Ini;
using U9GL.Core.Install;
using U9GL.Core.Settings;

namespace U9GL.Core.Tests;

[TestClass]
public sealed partial class DeployerTests
{
    private const string OptionsText =
        "[Sound]\r\nMusicVolume=64\r\n[Game]\r\nGame Rasterizer=2\r\nGameScreenWidth=640\r\nGameScreenHeight=480\r\nGameMipMapLevel=1\r\nDisableWinKey=0\r\n";

    private string _root = "";
    private GameFolder _folder = null!;
    private FakeProcesses _processes = null!;
    private DateTime _now;

    [TestInitialize]
    public void CreateFolder()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-core-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(_root, "u9gl"));
        File.WriteAllText(Path.Combine(_root, "u9.exe"), "a fake u9.exe");
        File.WriteAllBytes(Path.Combine(_root, "Options.ini"), Encoding.Latin1.GetBytes(OptionsText));
        _folder = new GameFolder(_root);
        _processes = new FakeProcesses();
        _now = new DateTime(2026, 9, 14, 12, 0, 0, DateTimeKind.Utc);
        WriteBundle("0.1.0");
    }

    [TestCleanup]
    public void DeleteFolder()
    {
        try { Directory.Delete(_root, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    private void WriteBundle(string version, string loaderVersion = "")
    {
        File.WriteAllText(_folder.OurDll, $"U9GL {version} u9log");
        File.WriteAllText(_folder.BundledLoader, $"U9GL {(loaderVersion == "" ? version : loaderVersion)} loader");
        File.WriteAllText(_folder.DefaultsIni, $"# u9gl.ini defaults {version}\ngl_window_size = 0\nmetrics_overlay = 1\n");
    }

    private Deployer NewDeployer(string launcherVersion = "0.1.0", bool knownBuild = true)
    {
        var known = knownBuild
            ? new[] { new KnownBuild("test", Sha256(Path.Combine(_root, "u9.exe")), new FileInfo(Path.Combine(_root, "u9.exe")).Length) }
            : [];
        return new Deployer(_folder, Version.Parse(launcherVersion), new FakeVersions(), _processes,
            () => _now = _now.AddSeconds(1), known);
    }

    [TestMethod]
    public void InstallOnACleanFolder()
    {
        NewDeployer().Install();

        Assert.AreEqual(File.ReadAllText(_folder.BundledLoader), File.ReadAllText(_folder.Loader));
        Assert.AreEqual("3", IniDocument.Load(_folder.OptionsIni).Get("Game", "Game Rasterizer"));

        var manifest = InstallManifest.Load(_folder.Manifest)!;
        Assert.AreEqual("0.1.0", manifest.Version);
        Assert.AreEqual("dsound.dll", manifest.Files.Single().Path);
        var edit = manifest.OptionsEdits.Single(e => e.Key == "Game Rasterizer");
        Assert.AreEqual(("Game Rasterizer", "2", "3", "install"), (edit.Key, edit.OldValue, edit.NewValue, edit.Reason));

        var optionsBackup = manifest.Backups.Single(b => b.OriginalPath == "Options.ini");
        Assert.AreEqual(OptionsText, File.ReadAllText(_folder.Absolute(optionsBackup.BackupPath), Encoding.Latin1));

        var effective = U9glIni.Effective(File.ReadAllText(_folder.U9glIni));
        Assert.AreEqual("1", effective["rasterizer5"]);
        Assert.AreEqual("1", effective["gl_window_size"], "the verified default, not the documented 0");
        Assert.AreEqual("u9log.dll", effective["loader_dll"]);

        var state = NewDeployer().Inspect();
        Assert.IsTrue(state.LoaderInstalled);
        Assert.IsTrue(state.Components.Where(c => c.Name != Deployer.LargeAddressRow).All(c => c.Status == ComponentStatus.Ok),
            string.Join(", ", state.Components.Select(c => $"{c.Name}={c.Status}")));
        Assert.AreEqual(ComponentStatus.Missing, state.Component(Deployer.LargeAddressRow).Status);
        Assert.AreEqual("3", state.GameRasterizer);
    }

    [TestMethod]
    public void AFirstInstallWritesTheLauncherDefaultsIntoOptionsIni()
    {
        File.WriteAllBytes(_folder.OptionsIni, Encoding.Latin1.GetBytes(OptionsText
            + "[Performance]\r\nMiddleClippingPlaneDistance=3000\r\nFarClippingPlaneDistance=5000\r\n"));

        NewDeployer().Install();
        var ini = IniDocument.Load(_folder.OptionsIni);
        Assert.AreEqual("4000", ini.Get("Performance", "MiddleClippingPlaneDistance"));
        Assert.AreEqual(SettingsCatalog.ById("draw-distance").Default.Values["FarClippingPlaneDistance"],
            ini.Get("Performance", "FarClippingPlaneDistance"));
        Assert.AreEqual(SettingsCatalog.ById("windows-key").Default.Values["DisableWinKey"],
            ini.Get("Game", "DisableWinKey"));
        Assert.IsNull(ini.Get("Performance", "CullStartDist"), "a missing key is not added");
        Assert.AreEqual("640", ini.Get("Game", "GameScreenWidth"), "the resolution is the player's");
        var edit = InstallManifest.Load(_folder.Manifest)!.OptionsEdits.Single(e => e.Key == "MiddleClippingPlaneDistance");
        Assert.AreEqual(("3000", "4000"), (edit.OldValue, edit.NewValue));

        ini.Set("Performance", "MiddleClippingPlaneDistance", "6000");
        File.WriteAllBytes(_folder.OptionsIni, ini.ToBytes());
        NewDeployer().Install();
        Assert.AreEqual("6000", IniDocument.Load(_folder.OptionsIni).Get("Performance", "MiddleClippingPlaneDistance"));
    }

    [TestMethod]
    public void AnUnsupportedBuildChangesNothing()
    {
        var before = Snapshot();
        var error = Assert.ThrowsExactly<DeployException>(() => NewDeployer(knownBuild: false).Install());
        Assert.AreEqual(DeployProblem.UnsupportedBuild, error.Problem);
        CollectionAssert.AreEquivalent(before, Snapshot());
    }

    [TestMethod]
    public void ARunningGameChangesNothing()
    {
        _processes.Running = true;
        var before = Snapshot();
        Assert.AreEqual(DeployProblem.GameRunning, Assert.ThrowsExactly<DeployException>(() => NewDeployer().Install()).Problem);
        Assert.AreEqual(DeployProblem.GameRunning, Assert.ThrowsExactly<DeployException>(() => NewDeployer().Uninstall()).Problem);
        CollectionAssert.AreEquivalent(before, Snapshot());
    }

    [TestMethod]
    public void AForeignLoaderIsRefusedThenReplacedOnConfirmationAndPutBackByUninstall()
    {
        File.WriteAllText(_folder.Loader, "another program's dsound.dll");
        var before = Snapshot();
        Assert.AreEqual(DeployProblem.ForeignLoader, Assert.ThrowsExactly<DeployException>(() => NewDeployer().Install()).Problem);
        CollectionAssert.AreEquivalent(before, Snapshot());

        NewDeployer().Install(new InstallOptions(ReplaceForeignLoader: true));
        Assert.AreEqual(File.ReadAllText(_folder.BundledLoader), File.ReadAllText(_folder.Loader));
        var backup = InstallManifest.Load(_folder.Manifest)!.Backups.Single(b => b.OriginalPath == "dsound.dll");
        Assert.AreEqual("another program's dsound.dll", File.ReadAllText(_folder.Absolute(backup.BackupPath)));

        NewDeployer().Uninstall();
        Assert.AreEqual("another program's dsound.dll", File.ReadAllText(_folder.Loader));
        Assert.AreEqual("2", IniDocument.Load(_folder.OptionsIni).Get("Game", "Game Rasterizer"));
        Assert.IsFalse(File.Exists(_folder.Manifest));
    }

    [TestMethod]
    public void UninstallLeavesAValueThePlayerChangedAgain()
    {
        NewDeployer().Install();
        NewDeployer().SaveSettings([new SettingWrite(SettingFile.OptionsIni, "Game", "GameScreenWidth", "1920")]);

        var ini = IniDocument.Load(_folder.OptionsIni);
        ini.Set("Game", "GameScreenWidth", "1280");
        File.WriteAllBytes(_folder.OptionsIni, ini.ToBytes());

        NewDeployer().Uninstall();
        Assert.AreEqual("1280", IniDocument.Load(_folder.OptionsIni).Get("Game", "GameScreenWidth"));
        Assert.AreEqual("2", IniDocument.Load(_folder.OptionsIni).Get("Game", "Game Rasterizer"));
    }

    [TestMethod]
    public void SettingsSurviveAnUpdateAndUninstallPutsThemBack()
    {
        NewDeployer().Install();
        NewDeployer().SaveSettings(
        [
            new SettingWrite(SettingFile.OptionsIni, "Game", "GameScreenWidth", "1920"),
            new SettingWrite(SettingFile.OptionsIni, "Game", "GameScreenHeight", "1080"),
            new SettingWrite(SettingFile.U9glIni, null, "gl_window_size", "2"),
        ]);
        Assert.AreEqual("1920", IniDocument.Load(_folder.OptionsIni).Get("Game", "GameScreenWidth"));
        Assert.AreEqual("2", U9glIni.Effective(File.ReadAllText(_folder.U9glIni))["gl_window_size"]);

        WriteBundle("0.2.0");
        var loaderRow = NewDeployer("0.2.0").Inspect().Component(Deployer.LoaderRow);
        Assert.AreEqual(ComponentStatus.UpdateAvailable, loaderRow.Status);

        NewDeployer("0.2.0").Install();
        Assert.AreEqual("U9GL 0.2.0 loader", File.ReadAllText(_folder.Loader));
        string ini = File.ReadAllText(_folder.U9glIni);
        StringAssert.StartsWith(ini, "# u9gl.ini defaults 0.2.0");
        Assert.AreEqual("2", U9glIni.Effective(ini)["gl_window_size"]);
        Assert.AreEqual("0.2.0", InstallManifest.Load(_folder.Manifest)!.Version);

        NewDeployer("0.2.0").Uninstall();
        var options = IniDocument.Load(_folder.OptionsIni);
        Assert.AreEqual("640", options.Get("Game", "GameScreenWidth"), "the stock game may not accept a U9GL resolution (I-5)");
        Assert.AreEqual("480", options.Get("Game", "GameScreenHeight"));
        Assert.AreEqual("2", options.Get("Game", "Game Rasterizer"), "Install's own repair is put back too");
        Assert.IsFalse(File.Exists(_folder.Loader));
        CollectionAssert.AreEqual(Encoding.Latin1.GetBytes(OptionsText), File.ReadAllBytes(_folder.OptionsIni),
            "Options.ini is byte-identical to the original");
    }

    [TestMethod]
    public void TheReleaseProfileStartsAFreshInstallButNotAnUpdate()
    {
        File.WriteAllText(_folder.ProfileIni, "# profile\ngold_cap = 999999\nlarge_address = 1\ngold_test_key = 1\nrasterizer5 = 0\n");
        NewDeployer().Install();
        var effective = U9glIni.Effective(File.ReadAllText(_folder.U9glIni));
        Assert.AreEqual("999999", effective["gold_cap"]);
        Assert.AreEqual("1", effective["large_address"]);
        Assert.AreEqual("1", effective["gold_test_key"], "a key the pages do not show is carried too");
        Assert.AreEqual("1", effective["rasterizer5"], "managed values still win");

        NewDeployer().SaveSettings([new SettingWrite(SettingFile.U9glIni, null, "gold_cap", "9999")]);
        WriteBundle("0.2.0");
        File.WriteAllText(_folder.ProfileIni, "gold_cap = 999999\nlarge_address = 0\n");
        NewDeployer("0.2.0").Install();
        effective = U9glIni.Effective(File.ReadAllText(_folder.U9glIni));
        Assert.AreEqual("9999", effective["gold_cap"]);
        Assert.AreEqual("1", effective["large_address"]);
    }

    [TestMethod]
    public void ASavedSettingTakesItsLineOutOfTheDiagIni()
    {
        NewDeployer().Install();
        File.WriteAllText(_folder.DiagIni, "# research\ndiag_bird_watch = 2\nstream_actors = 300\n");

        var actions = NewDeployer().SaveSettings([new SettingWrite(SettingFile.U9glIni, null, "diag_bird_watch", "0")]);

        var diag = U9glIni.Effective(File.ReadAllText(_folder.DiagIni));
        Assert.IsFalse(diag.ContainsKey("diag_bird_watch"), "the diag line no longer overrides the saved choice");
        Assert.AreEqual("300", diag["stream_actors"], "other research switches stay");
        Assert.AreEqual("0", U9glIni.Effective(File.ReadAllText(_folder.U9glIni))["diag_bird_watch"]);
        Assert.IsTrue(actions.Any(a => a.StartsWith("backed up u9gl\\u9gl.diag.ini", StringComparison.Ordinal)), string.Join("; ", actions));
        Assert.IsTrue(actions.Any(a => a.Contains("commented out diag_bird_watch", StringComparison.Ordinal)), string.Join("; ", actions));

        var stamp = File.GetLastWriteTimeUtc(_folder.DiagIni);
        actions = NewDeployer().SaveSettings([new SettingWrite(SettingFile.U9glIni, null, "gold_cap", "9999")]);
        Assert.AreEqual(stamp, File.GetLastWriteTimeUtc(_folder.DiagIni));
        Assert.IsFalse(actions.Any(a => a.Contains("diag.ini", StringComparison.Ordinal)), string.Join("; ", actions));
    }

    [TestMethod]
    public void ManagedSettingsCannotBeSet()
    {
        NewDeployer().Install();
        var error = Assert.ThrowsExactly<DeployException>(() =>
            NewDeployer().SaveSettings([new SettingWrite(SettingFile.U9glIni, null, "rasterizer5", "0")]));
        Assert.AreEqual(DeployProblem.ManagedSetting, error.Problem);
    }

    [TestMethod]
    public void AModifiedLoaderIsReportedAndRepaired()
    {
        NewDeployer().Install();
        File.WriteAllText(_folder.Loader, "U9GL 0.1.0 tampered");
        Assert.AreEqual(ComponentStatus.Modified, NewDeployer().Inspect().Component(Deployer.LoaderRow).Status);

        NewDeployer().Install();
        Assert.AreEqual(ComponentStatus.Ok, NewDeployer().Inspect().Component(Deployer.LoaderRow).Status);
        var backups = InstallManifest.Load(_folder.Manifest)!.Backups.Where(b => b.OriginalPath == "dsound.dll");
        Assert.IsTrue(backups.Any(b => File.ReadAllText(_folder.Absolute(b.BackupPath)) == "U9GL 0.1.0 tampered"));
    }

    [TestMethod]
    public void AHandWrittenU9glIniIsBackedUp()
    {
        File.WriteAllText(_folder.U9glIni, "handwritten = 1\n");
        NewDeployer().Install();

        var backup = InstallManifest.Load(_folder.Manifest)!.Backups.Single(b => b.OriginalPath == Path.Combine("u9gl", "u9gl.ini"));
        Assert.AreEqual("handwritten = 1\n", File.ReadAllText(_folder.Absolute(backup.BackupPath)));
        Assert.IsTrue(U9glIni.HasBlock(File.ReadAllText(_folder.U9glIni)));
    }

    [TestMethod]
    public void AMixedBundleIsRefused()
    {
        WriteBundle("0.1.0", loaderVersion: "0.2.0");
        Assert.AreEqual(DeployProblem.BundleMismatch, Assert.ThrowsExactly<DeployException>(() => NewDeployer().Install()).Problem);
    }

    [TestMethod]
    public void UninstallWhenNotInstalledIsRefused()
    {
        Assert.AreEqual(DeployProblem.NotInstalled, Assert.ThrowsExactly<DeployException>(() => NewDeployer().Uninstall()).Problem);
    }

    private Dictionary<string, string> Snapshot() =>
        Directory.EnumerateFiles(_root, "*", SearchOption.AllDirectories)
            .ToDictionary(p => Path.GetRelativePath(_root, p), Sha256);

    private static string Sha256(string path) => Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));

    private sealed partial class FakeVersions : IFileVersionReader
    {
        public ComponentVersion Read(string path)
        {
            if (!File.Exists(path)) return ComponentVersion.Missing;
            var match = OurFile().Match(File.ReadAllText(path));
            return match.Success
                ? new ComponentVersion(true, true, Version.Parse(match.Groups[1].Value), match.Groups[1].Value)
                : new ComponentVersion(true, false, null, null);
        }

        [GeneratedRegex(@"^U9GL (\d+\.\d+\.\d+)")]
        private static partial Regex OurFile();
    }

    private sealed class FakeProcesses : IGameProcessProbe
    {
        public bool Running { get; set; }
        public bool IsRunning(GameFolder folder) => Running;
    }
}
