using System.Text;
using U9GL.Core.Ini;

namespace U9GL.Core.Install;

public sealed class KeyMapException(string message) : Exception(message);

public static class KeyMap
{
    public const string DefaultFileName = "default.kmp";
    public const string Marker = "; U9GL WASD movement";

    public const string On = "1";
    public const string Off = "0";

    private static readonly (string Key, string Command)[] Movement =
    [
        ("w", "forward"), ("a", "sidestep_left"), ("s", "backward"), ("d", "sidestep_right"),
    ];

    private static readonly (string Command, string StockKey, string WasdKey, string Section)[] Moved =
    [
        ("open_spellbook", "s", "e", "INTERACTION COMMANDS"),
    ];

    private const string DeadKey = "a";
    private const string DeadCommand = "abort_targeting";
    private const string DeadLegacyKey = "r";
    private const string CommentPrefix = "; ";

    private const string MovementSection = "MOVEMENT COMMANDS";

    public static string? PathFor(GameFolder folder)
    {
        string name = DefaultFileName;
        try
        {
            if (File.Exists(folder.OptionsIni) &&
                IniDocument.Load(folder.OptionsIni).Get("Interface", "Current_Keymap") is { Length: > 0 } set)
                name = set.Trim();
        }
        catch (Exception e) when (e is IniException or IOException)
        {
            name = DefaultFileName;
        }
        if (name.Length == 0 || name != Path.GetFileName(name) || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
            return null;
        try
        {
            if (Directory.Exists(folder.Root) &&
                Directory.EnumerateFiles(folder.Root, name).FirstOrDefault() is { } onDisk)
                return onDisk;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
        return Path.Combine(folder.Root, name);
    }

    public static string? State(string text)
    {
        var lines = Parse(text).Lines;
        int found = Movement.Count(m => lines.Any(l => l.Binds(m.Key, m.Command)));
        if (found == Movement.Length) return On;
        return found == 0 ? Off : null;
    }

    public static string Apply(string text, bool on)
    {
        var map = Parse(text);
        if (on) SwitchOn(map); else SwitchOff(map);
        return map.Compose();
    }

    private static void SwitchOn(Doc map)
    {
        var lines = map.Lines;
        SwitchOff(map);
        var conflicts = new List<string>();
        foreach (var (key, command) in Movement)
        {
            string? moved = Moved.FirstOrDefault(m => m.StockKey == key).Command;
            string? dead = key == DeadKey ? DeadCommand : null;
            foreach (var l in lines.Where(l => l.IsKey(key) && !Same(l.Command, command) && !Same(l.Command, moved) && !Same(l.Command, dead)))
                conflicts.Add($"{key} is bound to {l.Command}");
        }
        foreach (var (command, _, wasdKey, _) in Moved)
            foreach (var l in lines.Where(l => l.IsKey(wasdKey) && !Same(l.Command, command)))
                conflicts.Add($"{wasdKey} is bound to {l.Command}");
        if (conflicts.Count > 0)
            throw new KeyMapException(
                $"WASD movement needs the keys W, A, S, D and E, and your key map uses some of them already: " +
                $"{string.Join("; ", conflicts)}. Change those lines in the key map first; nothing was changed.");

        for (int i = 0; i < lines.Count; i++)
            if (lines[i].Binds(DeadKey, DeadCommand))
                lines[i] = Line.Of($"{CommentPrefix}{lines[i].Text.TrimEnd()}\t{Marker}, not a command in u9.exe");

        foreach (var (command, stockKey, wasdKey, _) in Moved)
        {
            bool already = lines.Any(l => l.Binds(wasdKey, command));
            for (int i = lines.Count - 1; i >= 0; i--)
            {
                if (!lines[i].Binds(stockKey, command)) continue;
                if (already) lines.RemoveAt(i);
                else { lines[i] = lines[i].Rekeyed(wasdKey).WithMarker($"was {stockKey}"); already = true; }
            }
            if (!already && !lines.Any(l => Same(l.Command, command)))
                map.AddToSection(Moved.First(m => m.Command == command).Section, Line.New(wasdKey, command));
        }

        foreach (var (key, command) in Movement)
            if (!lines.Any(l => l.Binds(key, command)))
                map.AddToSection(MovementSection, Line.New(key, command));
    }

    private static void SwitchOff(Doc map)
    {
        var lines = map.Lines;
        lines.RemoveAll(l => Movement.Any(m => l.Binds(m.Key, m.Command)));
        foreach (var (command, stockKey, wasdKey, section) in Moved)
        {
            for (int i = 0; i < lines.Count; i++)
                if (lines[i].Binds(wasdKey, command) && lines[i].HasMarker)
                    lines[i] = lines[i].Rekeyed(stockKey).WithoutMarker();
            if (!lines.Any(l => Same(l.Command, command)) && !lines.Any(l => l.IsKey(stockKey)))
                map.AddToSection(section, Line.Of($"{stockKey}\t= {command}"));
        }

        for (int i = 0; i < lines.Count; i++)
            if (RestoredDeadLine(lines[i]) is { } restored) lines[i] = restored;
    }

    private static Line? RestoredDeadLine(Line line)
    {
        if (!line.HasMarker) return null;
        if (line.Binds(DeadLegacyKey, DeadCommand)) return line.Rekeyed(DeadKey).WithoutMarker();
        if (line.Key != null || !line.Text.StartsWith(CommentPrefix, StringComparison.Ordinal)) return null;
        var restored = Line.Of(line.Text[CommentPrefix.Length..]).WithoutMarker();
        return restored.Binds(DeadKey, DeadCommand) ? restored : null;
    }

    private sealed record Line(string Text, string? Key, string? Command)
    {
        public bool IsKey(string key) => string.Equals(Key, key, StringComparison.OrdinalIgnoreCase);
        public bool Binds(string key, string command) =>
            IsKey(key) && string.Equals(Command, command, StringComparison.OrdinalIgnoreCase);
        public bool HasMarker => Text.Contains(Marker, StringComparison.Ordinal);

        public static Line New(string key, string command) => Of($"{key}\t= {command}\t{Marker}");

        public Line Rekeyed(string key)
        {
            int eq = Text.IndexOf('=');
            string before = Text[..eq];
            int start = before.Length - before.TrimStart().Length;
            int length = before.Trim().Length;
            return Of(before[..start] + key + before[(start + length)..] + Text[eq..]);
        }

        public Line WithMarker(string note) => Of($"{Text.TrimEnd()}\t{Marker}, {note}");

        public Line WithoutMarker()
        {
            int at = Text.IndexOf(Marker, StringComparison.Ordinal);
            return at < 0 ? this : Of(Text[..at].TrimEnd());
        }

        public static Line Of(string text)
        {
            string trimmed = text.TrimStart();
            if (trimmed.Length == 0 || trimmed[0] == '[') return new(text, null, null);
            if (trimmed[0] == ';' && !trimmed[1..].TrimStart().StartsWith('=')) return new(text, null, null);
            int eq = text.IndexOf('=', trimmed[0] == ';' ? text.IndexOf(';') + 1 : 0);
            if (eq < 0) return new(text, null, null);
            string key = text[..eq].Trim();
            string rest = text[(eq + 1)..];
            int comment = rest.IndexOf(';');
            string command = (comment < 0 ? rest : rest[..comment]).Trim();
            return key.Length == 0 || command.Length == 0 ? new(text, null, null) : new(text, key, command);
        }
    }

    private sealed class Doc(List<Line> lines, string newline, bool finalNewline)
    {
        public List<Line> Lines { get; } = lines;

        public void AddToSection(string section, Line line)
        {
            int header = Lines.FindIndex(l => l.Text.Trim().Equals($"[{section}]", StringComparison.OrdinalIgnoreCase));
            if (header < 0)
            {
                if (Lines.Count > 0 && Lines[^1].Text.Trim().Length > 0) Lines.Add(Line.Of(""));
                Lines.Add(Line.Of($"[{section}]"));
                Lines.Add(line);
                return;
            }
            int at = header + 1;
            for (int i = header + 1; i < Lines.Count && !Lines[i].Text.TrimStart().StartsWith('['); i++)
                if (Lines[i].Key != null) at = i + 1;
            Lines.Insert(at, line);
        }

        public string Compose()
        {
            var sb = new StringBuilder();
            for (int i = 0; i < Lines.Count; i++)
            {
                sb.Append(Lines[i].Text);
                if (i < Lines.Count - 1 || finalNewline) sb.Append(newline);
            }
            return sb.ToString();
        }
    }

    private static bool Same(string? a, string? b) =>
        a != null && b != null && string.Equals(a, b, StringComparison.OrdinalIgnoreCase);

    private static Doc Parse(string text)
    {
        string newline = text.Contains("\r\n") ? "\r\n" : "\n";
        if (text.Length == 0) return new Doc([], newline, finalNewline: true);
        bool finalNewline = text.EndsWith('\n');
        string body = finalNewline ? text[..^newline.Length] : text;
        return new Doc(body.Split(newline).Select(Line.Of).ToList(), newline, finalNewline);
    }
}
