using System.Windows;
using System.Windows.Controls;

namespace U9GL.Launcher.Views;

public partial class AboutArt : UserControl
{
    public static readonly string VideoPath = Path.Combine(AppContext.BaseDirectory, "media", "about.mp4");

    private bool _failed;

    private bool _opened;

    public AboutArt() => InitializeComponent();

    private bool ShouldPlay =>
        IsVisible && !_failed && App.LightTheme && App.AnimateAbout && File.Exists(VideoPath);

    private void Update()
    {
        if (ShouldPlay)
        {
            Video.Source ??= new Uri(VideoPath);
            Video.Play();
            if (_opened) ShowVideo();
        }
        else
        {
            Video.Pause();
            ShowStill();
        }
    }

    private void ShowVideo()
    {
        Video.Visibility = Visibility.Visible;
        Still.Visibility = Visibility.Hidden;
    }

    private void ShowStill()
    {
        Video.Visibility = Visibility.Hidden;
        Still.Visibility = Visibility.Visible;
    }

    private void OnIsVisibleChanged(object sender, DependencyPropertyChangedEventArgs e) => Update();

    private void OnMediaOpened(object sender, RoutedEventArgs e)
    {
        _opened = true;
        if (ShouldPlay) ShowVideo();
    }

    private void OnMediaEnded(object sender, RoutedEventArgs e)
    {
        Video.Position = TimeSpan.Zero;
        Video.Play();
    }

    private void OnMediaFailed(object? sender, ExceptionRoutedEventArgs e)
    {
        _failed = true;
        ShowStill();
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        App.ArtChanged += Update;
        Update();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        App.ArtChanged -= Update;
        Video.Close();
        _opened = false;
        Video.Source = null;
        ShowStill();
    }
}
