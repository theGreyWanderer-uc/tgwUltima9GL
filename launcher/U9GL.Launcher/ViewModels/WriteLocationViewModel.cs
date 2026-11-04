using System.ComponentModel;
using System.Diagnostics;
using System.Windows.Input;

namespace U9GL.Launcher.ViewModels;

public enum LocationKind
{
    Folder,
    TextFile,
    FileInFolder,
}

public sealed class WriteLocationViewModel
{
    public WriteLocationViewModel(string title, string description, string path, LocationKind kind)
    {
        Title = title;
        Description = description;
        Path = path;
        Kind = kind;
        OpenCommand = new RelayCommand(Open, () => Exists);
    }

    public string Title { get; }
    public string Description { get; }
    public string Path { get; }
    public LocationKind Kind { get; }
    public ICommand OpenCommand { get; }

    public bool Exists => Kind == LocationKind.Folder ? Directory.Exists(Path) : File.Exists(Path);

    private void Open()
    {
        try
        {
            switch (Kind)
            {
                case LocationKind.Folder:
                    Process.Start(new ProcessStartInfo(Path) { UseShellExecute = true });
                    break;
                case LocationKind.TextFile:
                    Process.Start(new ProcessStartInfo("notepad.exe", $"\"{Path}\"") { UseShellExecute = true });
                    break;
                case LocationKind.FileInFolder:
                    Process.Start(new ProcessStartInfo("explorer.exe", $"/select,\"{Path}\"") { UseShellExecute = true });
                    break;
            }
        }
        catch (Win32Exception)
        {
        }
    }
}
