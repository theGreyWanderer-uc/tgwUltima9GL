using System.ComponentModel;
using System.Diagnostics;

namespace U9GL.Core.Install;

public interface IGameProcessProbe
{
    bool IsRunning(GameFolder folder);
}

public sealed class GameProcessProbe : IGameProcessProbe
{
    public bool IsRunning(GameFolder folder)
    {
        string copy = LargeAddress.CopyPath(folder);
        var processes = Process.GetProcessesByName("u9")
            .Concat(Process.GetProcessesByName(Path.GetFileNameWithoutExtension(LargeAddress.CopyName)));
        foreach (var process in processes)
        {
            using (process)
            {
                try
                {
                    string? file = process.MainModule?.FileName;
                    if (file == null ||
                        string.Equals(Path.GetFullPath(file), folder.Exe, StringComparison.OrdinalIgnoreCase) ||
                        string.Equals(Path.GetFullPath(file), copy, StringComparison.OrdinalIgnoreCase))
                        return true;
                }
                catch (Exception e) when (e is Win32Exception or InvalidOperationException)
                {
                    return true;
                }
            }
        }
        return false;
    }
}

public static class GameLauncher
{
    public static Process? Start(GameFolder folder, bool largeAddress = false)
    {
        string exe = folder.Exe;
        if (largeAddress)
        {
            LargeAddress.EnsureCopy(folder);
            exe = LargeAddress.CopyPath(folder);
        }
        return Process.Start(new ProcessStartInfo(exe)
        {
            WorkingDirectory = folder.Root,
            UseShellExecute = true,
        });
    }
}
