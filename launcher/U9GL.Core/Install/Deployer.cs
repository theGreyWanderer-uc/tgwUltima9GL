using System.Globalization;
using System.Text;
using System.Text.Json;
using U9GL.Core.Ini;
using U9GL.Core.Settings;

namespace U9GL.Core.Install;

public enum ComponentStatus { Ok, UpdateAvailable, VersionMismatch, Modified, Foreign, Missing }

public sealed record ComponentRow(string Name, string RelativePath, ComponentVersion Version, ComponentStatus Status, string Note);

public sealed record InstallState(
    GameFolder Folder,
    GameBuild? Build,
    bool GameRunning,
    bool Writable,
    string? VirtualStoreCopy,
    string? GameRasterizer,
    InstallManifest? Manifest,
    string? ManifestProblem,
    IReadOnlyList<ComponentRow> Components)
{
    public ComponentRow Component(string name) => Components.First(c => c.Name == name);

    public bool LoaderInstalled => Component(Deployer.LoaderRow).Version.IsOurs;
}

public enum DeployProblem
{
    GameMissing,
    UnsupportedBuild,
    GameRunning,
    BundleIncomplete,
    BundleMismatch,
    OptionsIniUnreadable,
    ForeignLoader,
    ManagedSetting,
    NotInstalled,
}

public sealed class DeployException(DeployProblem problem, string message) : Exception(message)
{
    public DeployProblem Problem { get; } = problem;
}

public sealed record InstallOptions(bool ReplaceForeignLoader = false);

public sealed record SettingWrite(SettingFile File, string? Section, string Key, string Value);

public sealed class Deployer
{
    public const string LauncherRow = "Launcher";
    public const string OurDllRow = "u9log.dll";
    public const string BundledLoaderRow = "Loader (bundled)";
    public const string LoaderRow = "Loader (installed)";
    public const string DefaultsRow = "Settings defaults";
    public const string LargeAddressRow = "Large address copy";

    private const string LoaderRelative = "dsound.dll";
    private const string ReasonInstall = "install";
    private const string ReasonSettings = "settings";

    private readonly GameFolder _folder;
    private readonly Version _launcherVersion;
    private readonly IFileVersionReader _versions;
    private readonly IGameProcessProbe _processes;
    private readonly Func<DateTime> _utcNow;
    private readonly IReadOnlyList<KnownBuild> _knownBuilds;
    private string? _sessionBackup;

    public Deployer(
        GameFolder folder,
        Version launcherVersion,
        IFileVersionReader? versions = null,
        IGameProcessProbe? processes = null,
        Func<DateTime>? utcNow = null,
        IReadOnlyList<KnownBuild>? knownBuilds = null)
    {
        _folder = folder;
        _launcherVersion = launcherVersion;
        _versions = versions ?? new FileVersionReader();
        _processes = processes ?? new GameProcessProbe();
        _utcNow = utcNow ?? (() => DateTime.UtcNow);
        _knownBuilds = knownBuilds ?? BuildIdentity.Known;
    }

    public InstallState Inspect()
    {
        GameBuild? build = File.Exists(_folder.Exe) ? BuildIdentity.Identify(_folder.Exe, _knownBuilds) : null;

        InstallManifest? manifest = null;
        string? manifestProblem = null;
        try { manifest = InstallManifest.Load(_folder.Manifest); }
        catch (Exception e) when (e is JsonException or IOException) { manifestProblem = e.Message; }

        string? rasterizer = null;
        try
        {
            if (File.Exists(_folder.OptionsIni))
                rasterizer = IniDocument.Load(_folder.OptionsIni).Get("Game", "Game Rasterizer");
        }
        catch (Exception e) when (e is IniException or IOException) { rasterizer = null; }

        var launcher = new ComponentVersion(true, true, _launcherVersion, Versions.Text(_launcherVersion));
        var rows = new List<ComponentRow>
        {
            new(LauncherRow, Path.Combine(GameFolder.BundleFolderName, "U9GL.exe"), launcher, ComponentStatus.Ok, ""),
            BundleRow(OurDllRow, _folder.OurDll),
            BundleRow(BundledLoaderRow, _folder.BundledLoader),
            InstalledLoaderRow(manifest),
            DefaultsRowState(),
            LargeAddressRowState(build),
        };

        return new InstallState(_folder, build, _processes.IsRunning(_folder), _folder.IsWritable(),
            _folder.VirtualStoreOptionsIni(), rasterizer, manifest, manifestProblem, rows);
    }

    private ComponentRow BundleRow(string name, string path)
    {
        var version = _versions.Read(path);
        string relative = _folder.Relative(path);
        if (!version.Exists) return new(name, relative, version, ComponentStatus.Missing, "not in the u9gl folder");
        if (!version.IsOurs || version.Version == null) return new(name, relative, version, ComponentStatus.Foreign, "not a U9GL file");
        if (Versions.Compare(version.Version, _launcherVersion) != 0)
            return new(name, relative, version, ComponentStatus.VersionMismatch, $"the launcher is {Versions.Text(_launcherVersion)}");
        return new(name, relative, version, ComponentStatus.Ok, "");
    }

    private ComponentRow InstalledLoaderRow(InstallManifest? manifest)
    {
        var version = _versions.Read(_folder.Loader);
        if (!version.Exists) return new(LoaderRow, LoaderRelative, version, ComponentStatus.Missing, "not installed");
        if (!version.IsOurs || version.Version == null)
            return new(LoaderRow, LoaderRelative, version, ComponentStatus.Foreign, "another program's dsound.dll");

        var recorded = manifest?.Files.FirstOrDefault(f => SamePath(f.Path, LoaderRelative));
        if (recorded != null && !string.Equals(recorded.Sha256, BuildIdentity.Sha256Of(_folder.Loader), StringComparison.OrdinalIgnoreCase))
            return new(LoaderRow, LoaderRelative, version, ComponentStatus.Modified, "differs from install.json");

        var bundled = _versions.Read(_folder.BundledLoader);
        if (bundled.IsOurs && bundled.Version != null)
        {
            int order = Versions.Compare(version.Version, bundled.Version);
            if (order < 0)
                return new(LoaderRow, LoaderRelative, version, ComponentStatus.UpdateAvailable, $"the u9gl folder has {bundled.Display}");
            if (order > 0)
                return new(LoaderRow, LoaderRelative, version, ComponentStatus.VersionMismatch, $"newer than the u9gl folder's {bundled.Display}");
        }
        return new(LoaderRow, LoaderRelative, version, ComponentStatus.Ok, "");
    }

    private ComponentRow LargeAddressRowState(GameBuild? build)
    {
        var state = LargeAddress.Check(_folder);
        if (!state.Exists)
            return new(LargeAddressRow, LargeAddress.CopyName, ComponentVersion.Missing, ComponentStatus.Missing,
                "none yet; made when you play with Large address space on");
        var version = new ComponentVersion(true, true, null, null);
        string sha = $"SHA-256 {state.Sha256![..16]}";
        if (!state.MatchesExe)
            return new(LargeAddressRow, LargeAddress.CopyName, version, ComponentStatus.Modified,
                $"not u9.exe with only the large address bit set ({sha}); Play rewrites it before starting it");
        string of = build?.Name is { } name ? $"u9.exe {name}" : "u9.exe";
        return new(LargeAddressRow, LargeAddress.CopyName, version, ComponentStatus.Ok,
            $"{of} with only the large address bit set; {sha}");
    }

    private ComponentRow DefaultsRowState()
    {
        string relative = _folder.Relative(_folder.DefaultsIni);
        if (!File.Exists(_folder.DefaultsIni))
            return new(DefaultsRow, relative, ComponentVersion.Missing, ComponentStatus.Missing, "not in the u9gl folder");
        string? text = U9glIni.DefaultsVersion(File.ReadAllText(_folder.DefaultsIni, Encoding.Latin1));
        if (text == null || !Version.TryParse(text, out var version))
            return new(DefaultsRow, relative, new ComponentVersion(true, false, null, null), ComponentStatus.VersionMismatch, "no version line");
        var component = new ComponentVersion(true, true, version, text);
        return Versions.Compare(version, _launcherVersion) == 0
            ? new(DefaultsRow, relative, component, ComponentStatus.Ok, "")
            : new(DefaultsRow, relative, component, ComponentStatus.VersionMismatch, $"the launcher is {Versions.Text(_launcherVersion)}");
    }

    public IReadOnlyList<string> Install(InstallOptions? options = null)
    {
        options ??= new InstallOptions();
        var (build, bundleVersion) = CheckGameAndBundle();
        var optionsIni = LoadOptionsIni();
        var installWrites = new List<(string Section, string Key, string Value)> { ("Game", "Game Rasterizer", "3") };
        if (!File.Exists(_folder.Manifest))
            installWrites.AddRange(SettingsCatalog.InitialOptionsValues((s, k) => optionsIni.Get(s, k)));
        PlanOptionsEdits(optionsIni, installWrites);

        var installed = _versions.Read(_folder.Loader);
        if (installed.Exists && !installed.IsOurs && !options.ReplaceForeignLoader)
            throw new DeployException(DeployProblem.ForeignLoader,
                "The dsound.dll beside u9.exe belongs to another program. U9GL needs that file name; " +
                "it can back the file up and replace it, and Uninstall puts it back.");

        var manifest = LoadManifestOrNew();
        var now = _utcNow();
        var actions = new List<string>();

        string bundledSha = BuildIdentity.Sha256Of(_folder.BundledLoader);
        if (!installed.Exists)
        {
            SafeFile.Copy(_folder.BundledLoader, _folder.Loader);
            actions.Add($"installed the loader {bundleVersion} beside u9.exe");
        }
        else if (!string.Equals(BuildIdentity.Sha256Of(_folder.Loader), bundledSha, StringComparison.OrdinalIgnoreCase))
        {
            Backup(manifest, _folder.Loader, now, actions);
            SafeFile.Copy(_folder.BundledLoader, _folder.Loader);
            actions.Add(installed.IsOurs
                ? $"replaced the loader {installed.Display} with {bundleVersion}"
                : $"replaced another program's dsound.dll with the loader {bundleVersion}");
        }
        else
        {
            actions.Add($"the loader {bundleVersion} is already in place");
        }

        WriteU9glIni(manifest, [], bundleVersion, now, actions);
        GoldLetters.EnsureFiles(_folder, actions);
        ApplyOptionsEdits(manifest, optionsIni, installWrites, ReasonInstall, now, actions);

        manifest.Version = bundleVersion;
        manifest.InstalledUtc = now;
        manifest.GameSha256 = build.Sha256;
        manifest.Files.RemoveAll(f => SamePath(f.Path, LoaderRelative));
        manifest.Files.Add(new DeployedFile(LoaderRelative, bundledSha, bundleVersion));
        manifest.Save(_folder.Manifest);
        actions.Add("recorded the install in u9gl\\install.json");
        return actions;
    }

    public IReadOnlyList<string> SaveSettings(IEnumerable<SettingWrite> writes)
    {
        var list = writes.ToList();
        foreach (var write in list)
            if (SettingsCatalog.IsManaged(write.File, write.Key))
                throw new DeployException(DeployProblem.ManagedSetting, $"{write.Key} is managed by the launcher and cannot be set.");

        var (_, bundleVersion) = CheckGameAndBundle();
        var optionWrites = list.Where(w => w.File == SettingFile.OptionsIni)
            .Select(w => (w.Section ?? "", w.Key, w.Value)).ToArray();
        IniDocument? optionsIni = null;
        if (optionWrites.Length > 0)
        {
            optionsIni = LoadOptionsIni();
            PlanOptionsEdits(optionsIni, optionWrites);
        }

        var manifest = LoadManifestOrNew();
        var now = _utcNow();
        var actions = new List<string>();
        if (optionsIni != null)
            ApplyOptionsEdits(manifest, optionsIni, optionWrites, ReasonSettings, now, actions);

        var u9glWrites = list.Where(w => w.File == SettingFile.U9glIni)
            .Select(w => new KeyValuePair<string, string>(w.Key.ToLowerInvariant(), w.Value)).ToList();
        if (u9glWrites.Count > 0)
        {
            WriteU9glIni(manifest, u9glWrites, bundleVersion, now, actions);
            ReleaseFromDiagIni(manifest, u9glWrites.Select(w => w.Key), now, actions);
        }

        if (File.Exists(_folder.Manifest) || manifest.OptionsEdits.Count > 0 || manifest.Backups.Count > 0)
            manifest.Save(_folder.Manifest);
        return actions;
    }

    public IReadOnlyList<string> Uninstall()
    {
        if (!File.Exists(_folder.Exe))
            throw new DeployException(DeployProblem.GameMissing, $"u9.exe was not found in {_folder.Root}.");
        if (_processes.IsRunning(_folder))
            throw new DeployException(DeployProblem.GameRunning, "u9.exe is running. Quit the game first.");

        var manifest = LoadManifestOrNull();
        var installed = _versions.Read(_folder.Loader);
        if (manifest == null && !installed.IsOurs)
            throw new DeployException(DeployProblem.NotInstalled, "U9GL is not installed in this game folder.");
        manifest ??= new InstallManifest();

        var reverts = new List<(string Section, string Key, string Value)>();
        IniDocument? optionsIni = null;
        var edits = manifest.OptionsEdits.ToList();
        if (edits.Count > 0 && File.Exists(_folder.OptionsIni))
        {
            optionsIni = LoadOptionsIni();
            foreach (var group in edits.GroupBy(e => (e.Section.ToLowerInvariant(), e.Key.ToLowerInvariant())))
            {
                var first = group.First();
                var last = group.Last();
                if (first.OldValue == null) continue;
                string? current = SafeGet(optionsIni, last.Section, last.Key);
                if (current == last.NewValue) reverts.Add((first.Section, first.Key, first.OldValue));
            }
            PlanOptionsEdits(optionsIni, reverts);
        }

        var now = _utcNow();
        var actions = new List<string>();

        if (optionsIni != null && reverts.Count > 0)
            ApplyOptionsEdits(manifest, optionsIni, reverts, ReasonInstall, now, actions);

        if (installed.IsOurs)
        {
            File.Delete(_folder.Loader);
            actions.Add("removed the loader from beside u9.exe");
        }
        else if (installed.Exists)
        {
            actions.Add("left another program's dsound.dll in place");
        }

        if (LargeAddress.RemoveCopy(_folder))
            actions.Add($"removed {LargeAddress.CopyName}, the large address copy; u9.exe was never changed");

        var original = manifest.Backups
            .Where(b => SamePath(b.OriginalPath, LoaderRelative))
            .FirstOrDefault(b => File.Exists(_folder.Absolute(b.BackupPath)) && !_versions.Read(_folder.Absolute(b.BackupPath)).IsOurs);
        if (original != null && !File.Exists(_folder.Loader))
        {
            SafeFile.Copy(_folder.Absolute(original.BackupPath), _folder.Loader);
            actions.Add("put back the dsound.dll that was there before U9GL");
        }

        if (File.Exists(_folder.Manifest))
        {
            string destination = Path.Combine(SessionBackup(now), InstallManifest.FileName);
            manifest.Save(_folder.Manifest);
            File.Move(_folder.Manifest, destination, overwrite: true);
            actions.Add($"moved install.json to {_folder.Relative(destination)}");
        }
        actions.Add("U9GL is uninstalled: u9.exe starts the stock game. The u9gl folder can be deleted.");
        return actions;
    }

    private (GameBuild Build, string BundleVersion) CheckGameAndBundle()
    {
        if (!File.Exists(_folder.Exe))
            throw new DeployException(DeployProblem.GameMissing, $"u9.exe was not found in {_folder.Root}.");
        var build = BuildIdentity.Identify(_folder.Exe, _knownBuilds);
        if (!build.IsSupported)
            throw new DeployException(DeployProblem.UnsupportedBuild,
                $"This u9.exe is not a build U9GL supports (SHA-256 {build.Sha256}). U9GL supports the GOG release, 1.19F.");
        if (_processes.IsRunning(_folder))
            throw new DeployException(DeployProblem.GameRunning, "u9.exe is running. Quit the game first.");

        var dll = _versions.Read(_folder.OurDll);
        var loader = _versions.Read(_folder.BundledLoader);
        if (!dll.IsOurs || dll.Version == null || !loader.IsOurs || loader.Version == null || !File.Exists(_folder.DefaultsIni))
            throw new DeployException(DeployProblem.BundleIncomplete,
                "The u9gl folder is incomplete. It needs u9log.dll, dsound.dll and u9gl.defaults.ini from one U9GL release.");
        if (Versions.Compare(dll.Version, loader.Version) != 0)
            throw new DeployException(DeployProblem.BundleMismatch,
                $"The u9gl folder mixes releases: u9log.dll is {dll.Display} and dsound.dll is {loader.Display}.");
        return (build, Versions.Text(dll.Version));
    }

    private IniDocument LoadOptionsIni()
    {
        if (!File.Exists(_folder.OptionsIni))
            throw new DeployException(DeployProblem.OptionsIniUnreadable, "Options.ini was not found in the game folder.");
        try { return IniDocument.Load(_folder.OptionsIni); }
        catch (IOException e) { throw new DeployException(DeployProblem.OptionsIniUnreadable, $"Options.ini could not be read: {e.Message}"); }
    }

    private static void PlanOptionsEdits(IniDocument ini, IEnumerable<(string Section, string Key, string Value)> edits)
    {
        try
        {
            foreach (var (section, key, value) in edits)
                if (ini.Get(section, key) != value) ini.Set(section, key, value);
        }
        catch (Exception e) when (e is IniException or ArgumentException)
        {
            throw new DeployException(DeployProblem.OptionsIniUnreadable, $"Options.ini cannot be edited safely: {e.Message}");
        }
    }

    private void ApplyOptionsEdits(InstallManifest manifest, IniDocument planned,
        IEnumerable<(string Section, string Key, string Value)> edits, string reason, DateTime now, List<string> actions)
    {
        var onDisk = IniDocument.Load(_folder.OptionsIni);
        var changes = edits
            .Select(e => (e.Section, e.Key, Old: onDisk.Get(e.Section, e.Key), New: e.Value))
            .Where(c => c.Old != c.New)
            .ToList();
        if (changes.Count == 0) return;

        Backup(manifest, _folder.OptionsIni, now, actions);
        SafeFile.WriteAllBytes(_folder.OptionsIni, planned.ToBytes());
        foreach (var (section, key, old, value) in changes)
        {
            manifest.OptionsEdits.Add(new IniEdit(section, key, old, value, reason, now));
            actions.Add($"Options.ini [{section}] {key}: {old ?? "(none)"} -> {value}");
        }
    }

    private void WriteU9glIni(InstallManifest manifest, IEnumerable<KeyValuePair<string, string>> playerValues,
        string bundleVersion, DateTime now, List<string> actions)
    {
        string defaults = File.ReadAllText(_folder.DefaultsIni, Encoding.Latin1);
        var values = new List<KeyValuePair<string, string>>(SettingsCatalog.InitialU9glValues());
        values.AddRange(U9glIni.ReadProfile(_folder.ProfileIni));
        if (File.Exists(_folder.U9glIni))
        {
            string current = File.ReadAllText(_folder.U9glIni, Encoding.Latin1);
            if (U9glIni.HasBlock(current)) values.AddRange(U9glIni.ReadBlock(current));
            else Backup(manifest, _folder.U9glIni, now, actions);
        }
        values.AddRange(playerValues);
        values.AddRange(SettingsCatalog.ManagedU9glValues());

        string text = U9glIni.Compose(defaults, values, bundleVersion);
        SafeFile.WriteAllBytes(_folder.U9glIni, Encoding.Latin1.GetBytes(text));
        actions.Add("wrote u9gl\\u9gl.ini");
    }

    private void ReleaseFromDiagIni(InstallManifest manifest, IEnumerable<string> keys, DateTime now, List<string> actions)
    {
        if (!File.Exists(_folder.DiagIni)) return;
        string text = File.ReadAllText(_folder.DiagIni, Encoding.Latin1);
        string note = $"set by the launcher {now:yyyy-MM-dd}";
        string edited = U9glIni.CommentOut(text, keys, note, out var commented);
        if (commented.Count == 0) return;
        Backup(manifest, _folder.DiagIni, now, actions);
        SafeFile.WriteAllBytes(_folder.DiagIni, Encoding.Latin1.GetBytes(edited));
        actions.Add($"commented out {string.Join(", ", commented.Distinct())} in u9gl\\u9gl.diag.ini, which overrode the setting");
    }

    private void Backup(InstallManifest manifest, string path, DateTime now, List<string> actions)
    {
        string relative = _folder.Relative(path);
        string destination = Path.Combine(SessionBackup(now), relative);
        if (File.Exists(destination)) return;
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        File.Copy(path, destination);
        manifest.Backups.Add(new BackupEntry(relative, _folder.Relative(destination), BuildIdentity.Sha256Of(destination), now));
        actions.Add($"backed up {relative} to {_folder.Relative(destination)}");
    }

    private string SessionBackup(DateTime now)
    {
        if (_sessionBackup != null) return _sessionBackup;
        var local = now.Kind == DateTimeKind.Utc ? now.ToLocalTime() : now;
        string stamp = local.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
        string candidate = Path.Combine(_folder.BackupRoot, stamp);
        for (int n = 2; Directory.Exists(candidate); n++)
            candidate = Path.Combine(_folder.BackupRoot, $"{stamp}-{n}");
        Directory.CreateDirectory(candidate);
        return _sessionBackup = candidate;
    }

    private InstallManifest LoadManifestOrNew() => LoadManifestOrNull() ?? new InstallManifest();

    private InstallManifest? LoadManifestOrNull()
    {
        try { return InstallManifest.Load(_folder.Manifest); }
        catch (JsonException) { return null; }
    }

    private static string? SafeGet(IniDocument ini, string section, string key)
    {
        try { return ini.Get(section, key); }
        catch (IniException) { return null; }
    }

    private static bool SamePath(string a, string b) =>
        string.Equals(a.Replace('/', '\\'), b.Replace('/', '\\'), StringComparison.OrdinalIgnoreCase);
}
