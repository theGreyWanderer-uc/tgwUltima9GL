using System.Windows;
using U9GL.Launcher.ViewModels;

namespace U9GL.Launcher;

public partial class MainWindow : Window
{
    public MainWindow() => InitializeComponent();

    private void OnActivated(object? sender, EventArgs e) => (DataContext as MainViewModel)?.Refresh();
}
