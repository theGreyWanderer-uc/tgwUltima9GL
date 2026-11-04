using System.Diagnostics;
using System.Runtime.InteropServices;

namespace U9GL.Core.Install;

public static class GameWindow
{
    private const int GwlExStyle = -20;
    private const int WsExNoActivate = 0x08000000;
    private const uint GwOwner = 4;

    public static bool AllowForeground(int processId) => AllowSetForegroundWindow(processId);

    public static IntPtr Find(int processId)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, _) =>
        {
            GetWindowThreadProcessId(window, out uint owner);
            if (owner != (uint)processId || !IsWindowVisible(window) || GetWindow(window, GwOwner) != IntPtr.Zero)
                return true;
            if ((GetWindowLongW(window, GwlExStyle) & WsExNoActivate) != 0)
                return true;
            found = window;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static bool IsInFront(int processId)
    {
        IntPtr window = GetForegroundWindow();
        if (window == IntPtr.Zero) return false;
        GetWindowThreadProcessId(window, out uint owner);
        return owner == (uint)processId;
    }

    public static bool BringToFront(IntPtr window) => SetForegroundWindow(window);

    public static string ForegroundOwner()
    {
        IntPtr window = GetForegroundWindow();
        if (window == IntPtr.Zero) return "no window";
        GetWindowThreadProcessId(window, out uint owner);
        try
        {
            using var process = Process.GetProcessById((int)owner);
            return $"{process.ProcessName}.exe";
        }
        catch (ArgumentException)
        {
            return $"process {owner}";
        }
    }

    private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr window, uint command);

    [DllImport("user32.dll")]
    private static extern int GetWindowLongW(IntPtr window, int index);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AllowSetForegroundWindow(int processId);
}
