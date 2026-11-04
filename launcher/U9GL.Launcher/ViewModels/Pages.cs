namespace U9GL.Launcher.ViewModels;

public abstract class PageViewModel(MainViewModel main, string title) : ObservableObject
{
    public MainViewModel Main { get; } = main;
    public string Title { get; } = title;
}

public sealed class DashboardPage(MainViewModel main) : PageViewModel(main, "Dashboard");

public sealed class WebDashboardPage(MainViewModel main) : PageViewModel(main, "Web Dashboard");

public sealed class SettingsPage(MainViewModel main, string title, string description, IReadOnlyList<SettingViewModel> settings,
                                 bool twoColumns = false)
    : PageViewModel(main, title)
{
    public string Description { get; } = description;
    public IReadOnlyList<SettingViewModel> Settings { get; } = settings;
    public bool TwoColumns { get; } = twoColumns;
}

public sealed class FixesPage(MainViewModel main, IReadOnlyList<SettingViewModel> settings) : PageViewModel(main, "Fixes")
{
    public IReadOnlyList<SettingViewModel> Settings { get; } = settings;
}

public sealed class DiagnosticsPage(MainViewModel main, IReadOnlyList<SettingViewModel> settings) : PageViewModel(main, "Diagnostics")
{
    public IReadOnlyList<SettingViewModel> Settings { get; } = settings;

    public bool DiagIniPresent => File.Exists(Main.Folder.DiagIni);

    public void Refresh() => OnPropertyChanged(nameof(DiagIniPresent));
}

public sealed class ExperimentalPage(MainViewModel main, IReadOnlyList<SettingViewModel> settings) : PageViewModel(main, "Experimental")
{
    public IReadOnlyList<SettingViewModel> Settings { get; } = settings;
}

public sealed class AboutPage(MainViewModel main) : PageViewModel(main, "About");
