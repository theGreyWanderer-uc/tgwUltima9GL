using System.Text.Json;
using System.Windows;
using Microsoft.Win32;
using U9GL.Core.Install;

namespace U9GL.Launcher.Services;

public sealed class LauncherSettings
{
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    public string? GameFolder { get; set; }

    public bool CloseOnPlay { get; set; } = true;

    public bool WebDashboardEnabled { get; set; }

    public bool WebDashboardNetwork { get; set; }

    public string? WebDashboardToken { get; set; }

    public bool AnimateAbout { get; set; } = true;

    private static string FilePath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "U9GL", "launcher.json");

    public static LauncherSettings Load()
    {
        try
        {
            return File.Exists(FilePath)
                ? JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllText(FilePath), Json) ?? new LauncherSettings()
                : new LauncherSettings();
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        {
            return new LauncherSettings();
        }
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, Json));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}

public static class GameFolderResolver
{
    public static GameFolder? Resolve(string[] args, LauncherSettings settings)
    {
        int index = Array.FindIndex(args, a => string.Equals(a, "--game", StringComparison.OrdinalIgnoreCase));
        if (index >= 0 && index + 1 < args.Length && HasGame(args[index + 1]))
            return Remember(settings, args[index + 1]);

        var beside = GameFolder.ForLauncherDirectory(AppContext.BaseDirectory);
        if (File.Exists(beside.Exe)) return beside;

        if (settings.GameFolder != null && HasGame(settings.GameFolder))
            return new GameFolder(settings.GameFolder);

        while (true)
        {
            var dialog = new OpenFolderDialog { Title = "Choose the Ultima IX folder, the one with u9.exe in it" };
            if (dialog.ShowDialog() != true) return null;
            if (HasGame(dialog.FolderName)) return Remember(settings, dialog.FolderName);
            MessageBox.Show($"There is no u9.exe in {dialog.FolderName}.", "U9GL", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private static bool HasGame(string folder) => File.Exists(Path.Combine(folder, "u9.exe"));

    private static GameFolder Remember(LauncherSettings settings, string folder)
    {
        settings.GameFolder = Path.GetFullPath(folder);
        settings.Save();
        return new GameFolder(folder);
    }
}

public static class Dialogs
{
    public static bool Confirm(string text, MessageBoxImage image = MessageBoxImage.Question) =>
        Show(text, MessageBoxButton.YesNo, image) == MessageBoxResult.Yes;

    public static MessageBoxResult AskYesNoCancel(string text) =>
        Show(text, MessageBoxButton.YesNoCancel, MessageBoxImage.Question);

    private static MessageBoxResult Show(string text, MessageBoxButton buttons, MessageBoxImage image)
    {
        var owner = Application.Current?.MainWindow;
        return owner != null
            ? MessageBox.Show(owner, text, "U9GL", buttons, image)
            : MessageBox.Show(text, "U9GL", buttons, image);
    }
}
