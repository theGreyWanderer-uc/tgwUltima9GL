using System.Globalization;
using System.Text;

namespace U9GL.Core.Install;

public sealed class ActionLog(string path, Func<DateTime>? clock = null, long maxBytes = 256 * 1024)
{
    private static readonly Encoding Utf8NoBom = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
    private readonly Func<DateTime> _clock = clock ?? (() => DateTime.Now);
    private readonly object _gate = new();

    public string Path { get; } = path;

    public void Write(string message)
    {
        lock (_gate)
        try
        {
            string? directory = System.IO.Path.GetDirectoryName(Path);
            if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);

            var existing = new FileInfo(Path);
            if (existing.Exists && existing.Length >= maxBytes)
                File.Move(Path, Path + ".1", overwrite: true);

            string stamp = _clock().ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture);
            var text = new StringBuilder();
            foreach (string line in message.Replace("\r\n", "\n").Split('\n'))
                text.Append(stamp).Append("  ").Append(line).Append("\r\n");
            File.AppendAllText(Path, text.ToString(), Utf8NoBom);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}
