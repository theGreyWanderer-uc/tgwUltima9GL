using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using U9GL.Core;
using U9GL.Core.Install;
using U9GL.Core.Settings;
using U9GL.Launcher.Services;

namespace U9GL.Launcher.ViewModels;

public sealed class ComponentRowViewModel(ComponentRow row, bool largeAddressOn = false)
{
    public string Name => row.Name;

    public string VersionText => row.Name == Deployer.LargeAddressRow ? (row.Version.Exists ? "u9.exe copy" : "") : row.Version.Display;

    public string Note => row.Name == Deployer.LargeAddressRow && row.Version.Exists && !largeAddressOn
        ? row.Note + ". Not used while Large address space is off; Uninstall removes it."
        : row.Note;

    public string StatusText => row.Status switch
    {
        ComponentStatus.Ok => "OK",
        ComponentStatus.UpdateAvailable => "Update available",
        ComponentStatus.VersionMismatch => "Version mismatch",
        ComponentStatus.Modified => "Modified",
        ComponentStatus.Foreign => "Not a U9GL file",
        _ => "Missing",
    };

    public Tone Tone => row.Status switch
    {
        ComponentStatus.Ok => Tone.Good,
        ComponentStatus.UpdateAvailable => Tone.Warning,
        ComponentStatus.Missing when row.Name is Deployer.LoaderRow or Deployer.LargeAddressRow => Tone.Neutral,
        _ => Tone.Bad,
    };
}

public sealed class MainViewModel : ObservableObject
{
    private readonly LauncherSettings _launcherSettings;
    private readonly ActionLog _log;
    private readonly Deployer _deployer;
    private InstallState _state;
    private PageViewModel _selectedPage;
    private string _errorText = "";
    private bool _busy;

    public MainViewModel(GameFolder folder, LauncherSettings launcherSettings, ActionLog log, WebDashboardViewModel webDashboard)
    {
        Folder = folder;
        _launcherSettings = launcherSettings;
        _log = log;
        WebDashboard = webDashboard;
        WebDashboard.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName != nameof(WebDashboardViewModel.Enabled)) return;
            OnPropertyChanged(nameof(CanCloseOnPlay));
            if (WebDashboard.Enabled && !TelemetryFeed.IsOverridden) TelemetryFeed.IsSwitchOn = true;
        };
        LauncherVersion = Assembly.GetExecutingAssembly().GetName().Version ?? new Version(0, 0, 0);
        _deployer = new Deployer(folder, LauncherVersion);

        Settings = SettingsCatalog.All
            .Where(d => d.Page != SettingPage.Managed)
            .Select(d => new SettingViewModel(d, OnSettingChanged))
            .ToList();

        var experimental = Settings.Where(s => s.Definition.HasExperimental).ToList();
        Pages =
        [
            new DashboardPage(this),
            new WebDashboardPage(this),
            new SettingsPage(this, "Display", "The game's resolution and its window. Changes apply the next time the game starts.", For(SettingPage.Display)),
            new SettingsPage(this, "Graphics", "What U9GL adds to the picture: lamp light that stays inside rooms, lamp and sun shadows, the night look, anti-aliasing and replacement textures. Changes apply the next time the game starts.", For(SettingPage.Graphics), twoColumns: true),
            new SettingsPage(this, "Game", "Game options U9GL has verified.", For(SettingPage.Game), twoColumns: true),
            new FixesPage(this, For(SettingPage.Fixes)),
            new DiagnosticsPage(this, For(SettingPage.Diagnostics)),
            .. (experimental.Count > 0 ? new PageViewModel[] { new ExperimentalPage(this, experimental) } : []),
            new DataPage(this),
            new AboutPage(this),
        ];
        _selectedPage = Pages[0];
        _state = _deployer.Inspect();
        GameData = new GameDataViewModel(this);

        string temp = Path.GetTempPath();
        string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        WriteLocations =
        [
            new("dsound.dll, beside u9.exe", "The loader. Install places it and Uninstall removes it.", Folder.Loader, LocationKind.FileInFolder),
            new("u9-laa.exe, beside u9.exe", "Made at Play when Large address space is on: u9.exe with one header bit set, checked against u9.exe on the dashboard. u9.exe itself is never changed. Uninstall removes it.",
                LargeAddress.CopyPath(Folder), LocationKind.FileInFolder),
            new("Options.ini", "The game's own settings. U9GL changes only the values you save, and Game Rasterizer at Install; Uninstall puts them back.", Folder.OptionsIni, LocationKind.TextFile),
            new(@"u9gl\u9gl.ini", "U9GL's settings: the documented defaults, then the launcher's block.", Folder.U9glIni, LocationKind.TextFile),
            new(@"u9gl\install.json", "What Install placed, edited and backed up.", Folder.Manifest, LocationKind.TextFile),
            new(@"u9gl\backup\", "Copies of anything U9GL replaced or edited, one folder per session.", Folder.BackupRoot, LocationKind.Folder),
            new("launcher.log", "What this launcher did, and any errors.", _log.Path, LocationKind.TextFile),
            new(@"%TEMP%\u9log.txt", "The game's log while U9GL runs, rewritten each time the game starts.", Path.Combine(temp, "u9log.txt"), LocationKind.TextFile),
            new(@"%TEMP%\u9gl_diag\", "Crash reports, one file per game run.", Path.Combine(temp, "u9gl_diag"), LocationKind.Folder),
            new(@"%LOCALAPPDATA%\U9GL\launcher.json", "This launcher's own preferences: the game folder, closing on Play, web dashboard, and About animation.", Path.Combine(localAppData, "U9GL", "launcher.json"), LocationKind.TextFile),
        ];

        RefreshCommand = new RelayCommand(Refresh);
        InstallCommand = new RelayCommand(Install, () => CanChangeFiles);
        UninstallCommand = new RelayCommand(Uninstall, () => CanChangeFiles && (_state.LoaderInstalled || _state.Manifest != null));
        SaveCommand = new RelayCommand(() => Save(), () => IsDirty && CanChangeFiles);
        RevertCommand = new RelayCommand(Revert, () => IsDirty);
        TurnOnTelemetryFeedCommand = new RelayCommand(() => TelemetryFeed.IsSwitchOn = true, () => CanTurnOnTelemetryFeed);
        PlayCommand = new RelayCommand(Play, () => CanPlay);
        OpenLogFolderCommand = new RelayCommand(() => OpenFolder(temp));
        OpenBackupFolderCommand = new RelayCommand(() => OpenFolder(Folder.BackupRoot), () => Directory.Exists(Folder.BackupRoot));
        OpenGameFolderCommand = new RelayCommand(() => OpenFolder(Folder.Root));
        CopyDetailsCommand = new RelayCommand(() => Clipboard.SetText(string.Join(Environment.NewLine, AboutDetails)));

        LoadSettings();
        _log.Write($"U9GL {LauncherVersion.ToString(3)} started for {Folder.Root}");
        LogState();

        _ = Data.Current.CheckAsync();
    }

    public GameFolder Folder { get; }
    public WebDashboardViewModel WebDashboard { get; }
    public bool CanCloseOnPlay => !WebDashboard.Enabled;
    private SettingViewModel TelemetryFeed => Settings.First(s => s.Definition.Id == SettingsCatalog.TelemetryFeedKey);
    public ICommand TurnOnTelemetryFeedCommand { get; }
    private bool CanTurnOnTelemetryFeed => !TelemetryFeed.IsOverridden && !TelemetryFeed.IsSwitchOn;

    public string TelemetryFeedText
    {
        get
        {
            var feed = TelemetryFeed;
            if (feed.IsOverridden)
                return feed.IsSwitchOn
                    ? "Game feed: on, set in u9gl\\u9gl.diag.ini."
                    : "Game feed: off. u9gl\\u9gl.diag.ini sets diag_telemetry = 0 and wins over the launcher; change it there.";
            if (feed.IsChanged)
                return feed.IsSwitchOn
                    ? "Game feed: on once you save. It takes effect the next time the game starts."
                    : "Game feed: off once you save.";
            return feed.IsSwitchOn
                ? "Game feed: on (Web dashboard feed, Diagnostics page)."
                : "Game feed: off. The page waits for telemetry until it is on.";
        }
    }
    public Version LauncherVersion { get; }
    public string VersionText => $"version {LauncherVersion.ToString(3)}";
    public string WindowTitle => $"U9GL {LauncherVersion.ToString(3)} - Ultima IX";
    public IReadOnlyList<PageViewModel> Pages { get; }
    public IReadOnlyList<SettingViewModel> Settings { get; }
    public IReadOnlyList<WriteLocationViewModel> WriteLocations { get; }
    public string LogPath => _log.Path;
    internal ActionLog Log => _log;

    public PageViewModel SelectedPage
    {
        get => _selectedPage;
        set
        {
            if (value == null) return;
            SetProperty(ref _selectedPage, value);
            if (value is DataPage data) data.OnShown();
        }
    }

    public ICommand RefreshCommand { get; }
    public ICommand InstallCommand { get; }
    public ICommand UninstallCommand { get; }
    public ICommand SaveCommand { get; }
    public ICommand RevertCommand { get; }
    public ICommand PlayCommand { get; }
    public ICommand OpenLogFolderCommand { get; }
    public ICommand OpenBackupFolderCommand { get; }
    public ICommand OpenGameFolderCommand { get; }
    public ICommand CopyDetailsCommand { get; }

    public IReadOnlyList<string> AboutDetails =>
    [
        LauncherDetail,
        $"WPF {InformationalVersion(typeof(Window).Assembly)} (Fluent theme, follows Windows)",
        $"{RuntimeInformation.FrameworkDescription} ({RuntimeInformation.ProcessArchitecture})",
        RuntimeInformation.OSDescription,
        _state.Build == null ? "u9.exe not found" : $"Ultima IX {_state.Build.Display}",
        RendererDetail,
    ];

    private string LauncherDetail
    {
        get
        {
            string? stamped = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
            string commit = stamped?.Split('+') is [_, var hash, ..] ? "+" + hash[..Math.Min(7, hash.Length)] : "";
            string built = Environment.ProcessPath is { } exe ? $", built {File.GetLastWriteTime(exe):yyyy-MM-dd HH:mm}" : "";
            return $"U9GL {LauncherVersion.ToString(3)}{commit}{built}";
        }
    }

    private string RendererDetail
    {
        get
        {
            var dll = _state.Component(Deployer.OurDllRow).Version;
            if (!dll.Exists) return "u9log.dll not found";
            return $"u9log.dll {dll.ProductVersion ?? dll.Display}, built {File.GetLastWriteTime(Folder.OurDll):yyyy-MM-dd HH:mm}";
        }
    }

    private static string InformationalVersion(Assembly assembly)
    {
        string? version = assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
        return version?.Split('+', '-')[0] ?? assembly.GetName().Version?.ToString(3) ?? "?";
    }

    public ObservableCollection<string> LastActions { get; } = [];

    public string ErrorText
    {
        get => _errorText;
        private set => SetProperty(ref _errorText, value);
    }

    public bool CloseOnPlay
    {
        get => _launcherSettings.CloseOnPlay;
        set
        {
            _launcherSettings.CloseOnPlay = value;
            _launcherSettings.Save();
            OnPropertyChanged();
        }
    }

    public bool AnimateAbout
    {
        get => _launcherSettings.AnimateAbout;
        set
        {
            _launcherSettings.AnimateAbout = value;
            _launcherSettings.Save();
            App.AnimateAbout = value;
            OnPropertyChanged();
        }
    }

    public string GameFolderPath => Folder.Root;

    public string BuildText =>
        _state.Build == null ? "u9.exe was not found in this folder." :
        _state.Build.IsSupported ? $"Ultima IX {_state.Build.Display}" :
        $"This u9.exe is not one U9GL supports (SHA-256 {_state.Build.Sha256[..16]}...). U9GL supports the GOG release of Ultima IX, 1.19F.";

    public Tone BuildTone => _state.Build?.IsSupported == true ? Tone.Good : Tone.Bad;

    public string RasterizerText => _state.GameRasterizer switch
    {
        "3" => "OK",
        null => "Options.ini has no readable Game Rasterizer line, so U9GL cannot tell which renderer the game will use.",
        var other => $"Game Rasterizer={other}: the game would start on {RendererName(other)} instead of U9GL's renderer. " +
                     $"Install or Repair sets it to 3, and Uninstall puts {other} back.",
    };

    public Tone RasterizerTone => _state.GameRasterizer == "3" ? Tone.Good : Tone.Bad;

    public string WritableText => _state.Writable
        ? "OK"
        : "U9GL cannot write to this folder without administrator rights. Move the game out of Program Files, or run U9GL.exe as administrator to install.";

    public Tone WritableTone => _state.Writable ? Tone.Good : Tone.Bad;

    public string VirtualStoreText => _state.VirtualStoreCopy == null ? "" :
        $"Windows keeps a redirected copy of Options.ini at {_state.VirtualStoreCopy}. The game may read that copy instead of the one U9GL edits.";

    public string RunningText => _state.GameRunning ? "Ultima IX is running. Quit the game to install, uninstall or save settings." : "";

    public IReadOnlyList<ComponentRowViewModel> Components
    {
        get
        {
            bool largeAddress = LargeAddressOn();
            return _state.Components.Select(c => new ComponentRowViewModel(c, largeAddress)).ToList();
        }
    }

    private bool LargeAddressOn() => SettingsReader.Read(Folder)
        .Any(s => s.Definition.Id == "large-address" && s.Values.GetValueOrDefault("large_address") == "1");

    public GameDataViewModel GameData { get; }

    public DataPage Data => Pages.OfType<DataPage>().First();

    public string InstallButtonText =>
        !_state.LoaderInstalled ? "Install" :
        _state.Component(Deployer.LoaderRow).Status == ComponentStatus.UpdateAvailable ? "Update" : "Repair";

    private bool CanChangeFiles => !_busy && _state.Build?.IsSupported == true && !_state.GameRunning && _state.Writable;

    private bool CanPlay => !_busy && _state.Build?.IsSupported == true && !_state.GameRunning && _state.LoaderInstalled;

    private static string RendererName(string id) => id switch
    {
        "2" => "Direct3D 7",
        "4" => "Direct3D 6",
        "5" => "U9GL's renderer id directly, which crashes without U9GL",
        _ => "an unknown renderer",
    };

    public bool IsDirty => Settings.Any(s => s.IsChanged);

    public string StatusText
    {
        get
        {
            if (_state.Build == null) return $"u9.exe was not found in {Folder.Root}.";
            if (!_state.Build.IsSupported) return "This u9.exe is not one U9GL supports.";
            if (_state.GameRunning) return "Ultima IX is running.";
            if (!_state.LoaderInstalled) return "U9GL is not installed. Install it on the Dashboard.";
            if (_state.GameRasterizer != "3") return "Options.ini selects another renderer. Repair it on the Dashboard.";
            return _state.Component(Deployer.LoaderRow).Status switch
            {
                ComponentStatus.Ok => $"Ultima IX {_state.Build.Display} with U9GL {_state.Component(Deployer.LoaderRow).Version.Display}.",
                ComponentStatus.UpdateAvailable => "An update is ready. Update it on the Dashboard.",
                _ => "U9GL needs attention. See the Dashboard.",
            };
        }
    }

    public Tone StatusTone =>
        _state.Build?.IsSupported != true ? Tone.Bad :
        _state.GameRunning || !_state.LoaderInstalled || _state.GameRasterizer != "3" ? Tone.Warning :
        _state.Component(Deployer.LoaderRow).Status == ComponentStatus.Ok ? Tone.Neutral : Tone.Warning;

    public string StatusDetail => IsDirty ? $"{Folder.Root}  -  unsaved changes" : Folder.Root;

    public void Refresh()
    {
        if (_busy) return;
        _state = _deployer.Inspect();
        GameData.Reload();
        if (!IsDirty) LoadSettings();
        else RaiseAll();
    }

    private void Install()
    {
        if (Run("Install", () => _deployer.Install(), out var problem) || problem != DeployProblem.ForeignLoader) return;
        bool replace = Dialogs.Confirm(ErrorText + "\n\nBack up that dsound.dll and replace it?", MessageBoxImage.Warning);
        _log.Write(replace ? "  the player chose to back up and replace the other dsound.dll" : "  the player kept the other dsound.dll");
        if (replace) Run("Install, replacing the other dsound.dll", () => _deployer.Install(new InstallOptions(ReplaceForeignLoader: true)), out _);
    }

    private void Uninstall()
    {
        bool sure = Dialogs.Confirm(
            "Uninstall removes dsound.dll from beside u9.exe and puts back what U9GL changed, " +
            "including values in Options.ini such as the resolution: the game without U9GL does not accept all of them.\n\n" +
            $"Without U9GL's fixes the game can hit the {Faults.F3.InSentence} again, and that includes the save it loads when it starts.\n\n" +
            "Uninstall U9GL?",
            MessageBoxImage.Warning);
        _log.Write(sure ? "Uninstall: confirmed by the player" : "Uninstall: cancelled by the player");
        if (sure) Run("Uninstall", () => _deployer.Uninstall(), out _);
    }

    private bool Save()
    {
        var writes = Settings.Where(s => s.IsChanged).SelectMany(s => s.Writes()).ToList();
        if (writes.Count == 0) return true;
        bool saved = Run("Save settings", () => _deployer.SaveSettings(writes), out _);
        if (saved) LoadSettings();
        return saved;
    }

    private void Revert() => LoadSettings();

    private async void Play()
    {
        if (IsDirty)
        {
            var answer = Dialogs.AskYesNoCancel("Save your changes before playing?");
            if (answer == MessageBoxResult.Cancel) return;
            if (answer == MessageBoxResult.Yes && !Save()) return;
            if (answer == MessageBoxResult.No) Revert();
        }

        Refresh();
        if (!CanPlay)
        {
            _log.Write("Play: not possible now");
            LogState();
            return;
        }

        if (_state.GameRasterizer != "3")
        {
            string id = _state.GameRasterizer ?? "?";
            bool anyway = Dialogs.Confirm(
                $"Options.ini has Game Rasterizer={id}, so the game will start on {RendererName(id)}, not on U9GL's renderer. " +
                "U9GL has not been tested with that renderer.\n\nRepair on the Dashboard sets it to 3.\n\nPlay anyway?",
                MessageBoxImage.Warning);
            _log.Write($"Play: Game Rasterizer={id}; the player chose to {(anyway ? "play anyway" : "not play")}");
            if (!anyway)
            {
                SelectedPage = Pages[0];
                return;
            }
        }

        bool largeAddress = LargeAddressOn();

        Process? game;
        try
        {
            game = GameLauncher.Start(Folder, largeAddress);
            string started = largeAddress ? LargeAddress.CopyPath(Folder) : Folder.Exe;
            _log.Write(largeAddress
                ? $"Play: started {started}, the large address copy (flag set: {LargeAddress.IsSet(started)})"
                : $"Play: started {started}");
        }
        catch (Exception e) when (e is Win32Exception or IOException or UnauthorizedAccessException or InvalidDataException)
        {
            ErrorText = $"The game could not be started: {e.Message}";
            _log.Write($"Play failed: {e.Message}");
            SelectedPage = Pages[0];
            return;
        }
        await HandOverToGameAsync(game);
    }

    private async Task HandOverToGameAsync(Process? game)
    {
        if (game == null)
        {
            _log.Write("Play: Windows returned no process to watch, so the launcher stays open");
            Refresh();
            return;
        }

        GameWindow.AllowForeground(game.Id);
        _busy = true;
        CommandManager.InvalidateRequerySuggested();
        bool inFront;
        try
        {
            inFront = await BringGameToFrontAsync(game);
        }
        finally
        {
            _busy = false;
            game.Dispose();
        }

        if (inFront && WebDashboard.Enabled)
        {
            Application.Current.MainWindow.WindowState = WindowState.Minimized;
            _log.Write("Play: launcher minimised; web dashboard keeps it open");
        }
        else if (inFront && CloseOnPlay)
        {
            Application.Current.MainWindow.Close();
            return;
        }
        if (!inFront)
        {
            ErrorText = "Ultima IX started, but Windows kept another window in front of it. " +
                        "Press Alt+Tab and choose Ultima IX, or click it on the taskbar.";
            SelectedPage = Pages[0];
        }
        Refresh();
    }

    private static readonly TimeSpan GameWindowTimeout = TimeSpan.FromSeconds(60);

    private async Task<bool> BringGameToFrontAsync(Process game)
    {
        var clock = Stopwatch.StartNew();
        IntPtr window = IntPtr.Zero;
        while (window == IntPtr.Zero)
        {
            if (game.HasExited)
            {
                _log.Write($"Play: the game exited {clock.Elapsed.TotalSeconds:0.0} s after starting, before its window appeared");
                return false;
            }
            if (clock.Elapsed > GameWindowTimeout)
            {
                _log.Write($"Play: no game window after {GameWindowTimeout.TotalSeconds:0} s, so the focus was left alone");
                return false;
            }
            await Task.Delay(250);
            window = GameWindow.Find(game.Id);
        }

        if (!GameWindow.IsInFront(game.Id)) GameWindow.BringToFront(window);
        var settle = Stopwatch.StartNew();
        while (!GameWindow.IsInFront(game.Id) && settle.Elapsed < TimeSpan.FromSeconds(2))
            await Task.Delay(100);

        bool inFront = GameWindow.IsInFront(game.Id);
        _log.Write(inFront
            ? $"Play: the game's window is in front, {clock.Elapsed.TotalSeconds:0.0} s after starting"
            : $"Play: the game's window could not be brought to the front; {GameWindow.ForegroundOwner()} has the foreground");
        return inFront;
    }

    private bool Run(string name, Func<IReadOnlyList<string>> action, out DeployProblem? problem)
    {
        problem = null;
        _busy = true;
        ErrorText = "";
        LastActions.Clear();
        _log.Write(name);
        bool ok = false;
        try
        {
            foreach (string line in action())
            {
                LastActions.Add(line);
                _log.Write("  " + line);
            }
            ok = true;
        }
        catch (DeployException e)
        {
            problem = e.Problem;
            ErrorText = e.Message;
            _log.Write($"  refused ({e.Problem}): {e.Message}");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            ErrorText = $"A file could not be written: {e.Message}";
            _log.Write($"  failed: {e}");
        }
        finally
        {
            _busy = false;
        }

        if (!ok) SelectedPage = Pages[0];
        _state = _deployer.Inspect();
        LogState();
        if (!IsDirty) LoadSettings();
        else RaiseAll();
        return ok;
    }

    private void LogState()
    {
        string build = _state.Build == null ? "u9.exe missing"
            : _state.Build.IsSupported ? $"build {_state.Build.Display}"
            : $"unsupported build {_state.Build.Sha256}";
        _log.Write($"  state: {build}; Game Rasterizer={_state.GameRasterizer ?? "?"}; running={_state.GameRunning}; writable={_state.Writable}");
        _log.Write("  files: " + string.Join("; ", _state.Components.Select(c => $"{c.Name} {c.Version.Display} {c.Status}")));
    }

    private void LoadSettings()
    {
        var current = SettingsReader.Read(Folder);
        foreach (var setting in Settings)
            setting.Load(current.First(c => c.Definition == setting.Definition));
        foreach (var page in Pages.OfType<DiagnosticsPage>()) page.Refresh();
        RaiseAll();
    }

    private void OnSettingChanged()
    {
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(StatusDetail));
        OnPropertyChanged(nameof(TelemetryFeedText));
        CommandManager.InvalidateRequerySuggested();
    }

    private void RaiseAll()
    {
        OnPropertyChanged(string.Empty);
        CommandManager.InvalidateRequerySuggested();
    }

    private IReadOnlyList<SettingViewModel> For(SettingPage page) =>
        Settings.Where(s => s.Definition.Page == page).ToList();

    private static void OpenFolder(string path)
    {
        try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
        catch (Win32Exception) {  }
    }
}
