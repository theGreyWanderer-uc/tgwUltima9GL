using System.Windows.Controls;

namespace U9GL.Launcher.Views;

public partial class DashboardView : UserControl
{
    public DashboardView() => InitializeComponent();
}

public partial class WebDashboardPageView : UserControl
{
    public WebDashboardPageView() => InitializeComponent();
}

public partial class SettingsPageView : UserControl
{
    public SettingsPageView() => InitializeComponent();
}

public partial class FixesPageView : UserControl
{
    public FixesPageView() => InitializeComponent();
}

public partial class DiagnosticsPageView : UserControl
{
    public DiagnosticsPageView() => InitializeComponent();
}

public partial class ExperimentalPageView : UserControl
{
    public ExperimentalPageView() => InitializeComponent();
}

public partial class DataPageView : UserControl
{
    public DataPageView() => InitializeComponent();

    private void ShowSelectedSave(object sender, System.Windows.RoutedEventArgs e) => ScrollToSelected();

    private void ShowSelectedSave(object sender, System.Windows.DependencyPropertyChangedEventArgs e) => ScrollToSelected();

    private void ScrollToSelected()
    {
        if (SavesGrid.SelectedItem is { } row && SavesGrid.IsVisible)
            Dispatcher.BeginInvoke(() => SavesGrid.ScrollIntoView(row), System.Windows.Threading.DispatcherPriority.Background);
    }
}

public partial class AboutView : UserControl
{
    public AboutView() => InitializeComponent();
}
