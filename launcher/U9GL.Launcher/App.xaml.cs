using System.ComponentModel;
using System.Windows;
using System.Windows.Threading;
using Microsoft.Win32;
using U9GL.Core.Install;
using U9GL.Launcher.Services;
using U9GL.Launcher.ViewModels;

namespace U9GL.Launcher;

public partial class App : Application
{
    private ActionLog? _log;
    private WebDashboardViewModel? _webDashboard;
    private bool _shutdownStarted;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        DispatcherUnhandledException += OnUnhandledException;

        var settings = LauncherSettings.Load();
        _animateAbout = settings.AnimateAbout;
        LightTheme = ReadLightTheme();
        SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;

        var folder = GameFolderResolver.Resolve(e.Args, settings);
        if (folder == null)
        {
            Shutdown();
            return;
        }

        _log = new ActionLog(LogPath(folder));
        _webDashboard = new WebDashboardViewModel(settings, _log, folder);
        var window = new MainWindow { DataContext = new MainViewModel(folder, settings, _log, _webDashboard) };
        window.Closing += OnMainWindowClosing;
        MainWindow = window;
        window.Show();
        _ = _webDashboard.InitializeAsync();
    }

    private async void OnMainWindowClosing(object? sender, CancelEventArgs e)
    {
        e.Cancel = true;
        await StopAndShutdownAsync(0);
    }

    private async Task StopAndShutdownAsync(int exitCode)
    {
        if (_shutdownStarted) return;
        _shutdownStarted = true;
        if (MainWindow != null) MainWindow.IsEnabled = false;
        try
        {
            if (_webDashboard != null) await _webDashboard.StopAsync();
        }
        finally
        {
            if (MainWindow != null) MainWindow.Closing -= OnMainWindowClosing;
            Shutdown(exitCode);
        }
    }

    public static bool LightTheme { get; private set; }

    public static event Action? ArtChanged;

    public static bool AnimateAbout
    {
        get => _animateAbout;
        set
        {
            _animateAbout = value;
            ArtChanged?.Invoke();
        }
    }

    private static bool _animateAbout = true;

    private static bool ReadLightTheme()
    {
        if (SystemParameters.HighContrast) return false;
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
        return key?.GetValue("AppsUseLightTheme") is not int light || light != 0;
    }

    private void OnUserPreferenceChanged(object sender, UserPreferenceChangedEventArgs e)
    {
        if (e.Category != UserPreferenceCategory.General) return;
        Dispatcher.BeginInvoke(() =>
        {
            bool light = ReadLightTheme();
            if (light == LightTheme) return;
            LightTheme = light;
            ArtChanged?.Invoke();
        });
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SystemEvents.UserPreferenceChanged -= OnUserPreferenceChanged;
        base.OnExit(e);
    }

    private static string LogPath(GameFolder folder) =>
        Directory.Exists(folder.Bundle) && folder.IsWritable()
            ? Path.Combine(folder.Bundle, "launcher.log")
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "U9GL", "launcher.log");

    private async void OnUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        e.Handled = true;
        _log?.Write("unexpected error: " + e.Exception);
        string where = _log != null ? $"\n\nThe details are in {_log.Path}." : "";
        MessageBox.Show($"U9GL hit an unexpected error and will close.\n\n{e.Exception.Message}{where}",
            "U9GL", MessageBoxButton.OK, MessageBoxImage.Error);
        await StopAndShutdownAsync(1);
    }
}
