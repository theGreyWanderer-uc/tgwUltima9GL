using System.Text;
using System.Text.RegularExpressions;

namespace U9GL.Core.Ini;

public static partial class U9glIni
{
    public const string BlockMarker = "# ==== U9GL launcher settings";

    public static IReadOnlyDictionary<string, string> RenamedKeys { get; } = new Dictionary<string, string>
    {
        ["fix_tele_watch"] = "diag_tele_watch",
        ["fix_path_watch"] = "diag_path_watch",
        ["fix_path_walker"] = "diag_path_walker",
        ["fix_bag_watch"] = "diag_bag_watch",
        ["fix_npc_watch"] = "diag_npc_watch",
        ["fix_npc_watch_ms"] = "diag_npc_watch_ms",
        ["fix_travel_watch"] = "diag_travel_watch",
        ["fix_mesh_watch"] = "diag_mesh_watch",
        ["fix_mesh_watch_ms"] = "diag_mesh_watch_ms",
        ["fix_mesh_watch_hw"] = "diag_mesh_watch_hw",
        ["fix_mesh_watch_hw_hits"] = "diag_mesh_watch_hw_hits",
        ["fix_mesh_stat"] = "diag_mesh_stat",
        ["fix_mesh_stat_ms"] = "diag_mesh_stat_ms",
        ["fix_mesh_stat_streak"] = "diag_mesh_stat_streak",
        ["fix_mesh_stat_held_s"] = "diag_mesh_stat_held_s",
        ["fix_furniture_watch"] = "diag_furniture_watch",
        ["fix_furniture_watch_ms"] = "diag_furniture_watch_ms",
        ["fix_census"] = "diag_census",
        ["fix_dump_entity"] = "diag_dump_entity",
        ["fix_fastarea"] = "diag_fastarea",
        ["fix_probe_index"] = "diag_probe_index",
        ["fix_probe_deep"] = "diag_probe_deep",
    };
    public const string DefaultsVersionPrefix = "# u9gl.ini defaults ";

    public static IReadOnlyDictionary<string, string> Effective(string text)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var (key, value) in Entries(Lines(text))) result[key] = value;
        return result;
    }

    public static bool HasBlock(string text) => BlockLine(Lines(text)) >= 0;

    public static Version? BlockVersion(string text)
    {
        var lines = Lines(text);
        int start = BlockLine(lines);
        if (start < 0) return null;
        var match = BlockVersionPattern().Match(lines[start]);
        return match.Success && Version.TryParse(match.Groups[1].Value, out var v) ? v : null;
    }

    public static IReadOnlyList<KeyValuePair<string, string>> ReadBlock(string text)
    {
        var lines = Lines(text);
        int start = BlockLine(lines);
        return start < 0 ? [] : Merge(Entries(lines.Skip(start + 1)));
    }

    public static IReadOnlyList<KeyValuePair<string, string>> ReadProfile(string path) =>
        File.Exists(path) ? Merge(Entries(Lines(File.ReadAllText(path, Encoding.Latin1)))) : [];

    public static string Compose(string defaultsText, IEnumerable<KeyValuePair<string, string>> settings, string version)
    {
        var sb = new StringBuilder(WithoutBlock(defaultsText));
        if (sb.Length > 0 && sb[^1] != '\n') sb.Append('\n');
        sb.Append('\n');
        sb.Append(BlockMarker).Append(", written by U9GL ").Append(version).Append(" ====\n");
        sb.Append("# Everything above is u9gl.defaults.ini. These lines come last, so they win.\n");
        sb.Append("# U9GL.exe rewrites this block from its settings. Keys added below this line\n");
        sb.Append("# are kept; edits to the defaults above are lost on the next update.\n");
        foreach (var (key, value) in Merge(settings))
        {
            if (!KeyPattern().IsMatch(key))
                throw new ArgumentException($"'{key}' is not a u9gl.ini key.", nameof(settings));
            if (value.Contains('\r') || value.Contains('\n'))
                throw new ArgumentException($"The value for '{key}' contains a line break.", nameof(settings));
            sb.Append(key).Append(" = ").Append(value).Append('\n');
        }
        return sb.ToString();
    }

    public static string? DefaultsVersion(string defaultsText)
    {
        var match = DefaultsVersionPattern().Match(defaultsText);
        return match.Success ? match.Groups[1].Value : null;
    }

    public static string CommentOut(string text, IEnumerable<string> keys, string note, out List<string> commented)
    {
        var wanted = keys.Select(k => k.ToLowerInvariant()).ToHashSet(StringComparer.Ordinal);
        commented = [];
        var sb = new StringBuilder(text.Length + 64);
        string[] raws = text.Split('\n');
        for (int i = 0; i < raws.Length; i++)
        {
            string raw = raws[i];
            string body = raw.TrimEnd('\r');
            string line = body.Trim();
            int eq = line.IndexOf('=');
            if (line.Length > 0 && line[0] is not ('#' or ';') && eq > 0)
            {
                string key = line[..eq].Trim().ToLowerInvariant();
                key = RenamedKeys.GetValueOrDefault(key, key);
                if (wanted.Contains(key))
                {
                    commented.Add(key);
                    raw = "# " + body + "   (" + note + ")" + raw[body.Length..];
                }
            }
            sb.Append(raw);
            if (i < raws.Length - 1) sb.Append('\n');
        }
        return sb.ToString();
    }

    private static List<string> Lines(string text) =>
        text.Split('\n').Select(l => l.TrimEnd('\r')).ToList();

    private static int BlockLine(List<string> lines) =>
        lines.FindIndex(l => l.TrimStart().StartsWith(BlockMarker, StringComparison.Ordinal));

    private static string WithoutBlock(string text)
    {
        int position = 0;
        foreach (string raw in text.Split('\n'))
        {
            if (raw.TrimStart().StartsWith(BlockMarker, StringComparison.Ordinal))
                return text[..position].TrimEnd('\n', '\r') + "\n";
            position += raw.Length + 1;
        }
        return text;
    }

    private static IEnumerable<KeyValuePair<string, string>> Entries(IEnumerable<string> lines)
    {
        foreach (string raw in lines)
        {
            string line = raw.Trim();
            if (line.Length == 0 || line[0] is '#' or ';') continue;
            int eq = line.IndexOf('=');
            if (eq <= 0) continue;
            string key = line[..eq].Trim().ToLowerInvariant();
            if (key.Length == 0) continue;
            yield return new(RenamedKeys.GetValueOrDefault(key, key), line[(eq + 1)..].Trim());
        }
    }

    private static List<KeyValuePair<string, string>> Merge(IEnumerable<KeyValuePair<string, string>> entries)
    {
        var result = new List<KeyValuePair<string, string>>();
        foreach (var (key, value) in entries)
        {
            int index = result.FindIndex(p => p.Key == key);
            if (index >= 0) result[index] = new(key, value);
            else result.Add(new(key, value));
        }
        return result;
    }

    [GeneratedRegex("^[a-z0-9_]+$")]
    private static partial Regex KeyPattern();

    [GeneratedRegex(@"^# u9gl\.ini defaults (\d+\.\d+\.\d+)", RegexOptions.Multiline)]
    private static partial Regex DefaultsVersionPattern();

    [GeneratedRegex(@"written by U9GL (\d+\.\d+\.\d+)")]
    private static partial Regex BlockVersionPattern();
}
