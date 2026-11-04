using System.Text;

namespace U9GL.Core.Ini;

public sealed class IniDocument
{
    private readonly List<Line> _lines;

    private IniDocument(List<Line> lines) => _lines = lines;

    public static IniDocument Load(string path) => Parse(File.ReadAllBytes(path));

    public static IniDocument Parse(byte[] bytes)
    {
        string text = Encoding.Latin1.GetString(bytes);
        var lines = new List<Line>();
        string? section = null;
        int start = 0;
        while (start < text.Length)
        {
            int newline = text.IndexOf('\n', start);
            string body, ending;
            if (newline < 0)
            {
                body = text[start..];
                ending = "";
                start = text.Length;
            }
            else
            {
                int end = newline > start && text[newline - 1] == '\r' ? newline - 1 : newline;
                body = text[start..end];
                ending = text[end..(newline + 1)];
                start = newline + 1;
            }
            string? header = SectionName(body);
            if (header != null) section = header;
            lines.Add(new Line(body, ending, section, header != null));
        }
        return new IniDocument(lines);
    }

    public byte[] ToBytes()
    {
        var sb = new StringBuilder();
        foreach (var line in _lines) sb.Append(line.Text).Append(line.Ending);
        return Encoding.Latin1.GetBytes(sb.ToString());
    }

    public string? Get(string section, string key)
    {
        int index = Find(section, key);
        if (index < 0) return null;
        string text = _lines[index].Text;
        return text[(text.IndexOf('=') + 1)..].Trim();
    }

    public void Set(string section, string key, string value)
    {
        if (value.Contains('\r') || value.Contains('\n'))
            throw new ArgumentException("A value cannot contain a line break.", nameof(value));
        if (value.Any(c => c > 'ÿ'))
            throw new ArgumentException("A value must be Latin-1 text.", nameof(value));
        int index = Find(section, key);
        if (index < 0)
            throw new IniException($"[{section}] has no '{key}' line, and keys are not added.");
        var line = _lines[index];
        int valueStart = line.Text.IndexOf('=') + 1;
        while (valueStart < line.Text.Length && line.Text[valueStart] is ' ' or '\t')
            valueStart++;
        _lines[index] = line with { Text = line.Text[..valueStart] + value };
    }

    private int Find(string section, string key)
    {
        string wantedSection = section.Trim();
        string wantedKey = key.Trim();
        int found = -1;
        for (int i = 0; i < _lines.Count; i++)
        {
            var line = _lines[i];
            if (line.IsHeader || line.Section == null) continue;
            if (!string.Equals(line.Section, wantedSection, StringComparison.OrdinalIgnoreCase)) continue;
            if (!string.Equals(KeyOf(line.Text), wantedKey, StringComparison.OrdinalIgnoreCase)) continue;
            if (found >= 0)
                throw new IniException($"[{section}] has '{key}' more than once; refusing to guess which.");
            found = i;
        }
        return found;
    }

    private static string? SectionName(string text)
    {
        string t = text.Trim();
        if (t.Length < 3 || t[0] != '[') return null;
        int close = t.IndexOf(']');
        return close > 1 ? t[1..close].Trim() : null;
    }

    private static string? KeyOf(string text)
    {
        string t = text.TrimStart();
        if (t.Length == 0 || t[0] is ';' or '#' or '[') return null;
        int eq = text.IndexOf('=');
        if (eq <= 0) return null;
        string key = text[..eq].Trim();
        return key.Length > 0 ? key : null;
    }

    private sealed record Line(string Text, string Ending, string? Section, bool IsHeader);
}

public sealed class IniException(string message) : Exception(message);
