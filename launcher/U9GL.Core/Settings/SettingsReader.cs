using System.Text;
using U9GL.Core.Ini;
using U9GL.Core.Install;

namespace U9GL.Core.Settings;

public sealed record CurrentSetting(
    SettingDefinition Definition,
    IReadOnlyDictionary<string, string?> Values,
    SettingChoice? Choice,
    IReadOnlyList<string>? DiagKeys = null)
{
    public IReadOnlyList<string> Overridden => DiagKeys ?? [];

    public bool IsSet => Values.Values.All(v => v != null);
}

public static class SettingsReader
{
    public static IReadOnlyList<CurrentSetting> Read(GameFolder folder)
    {
        var options = LoadOptions(folder.OptionsIni);
        var u9gl = LoadU9gl(folder);
        var diag = Effective(folder.DiagIni);
        var keyMap = KeyMapValues(folder);
        return SettingsCatalog.All
            .Where(d => d.Page != SettingPage.Managed)
            .Select(d => Current(d, options, u9gl, keyMap) with
            {
                DiagKeys = d.Keys.Where(k => k.File == SettingFile.U9glIni && diag.ContainsKey(k.Key.ToLowerInvariant()))
                                 .Select(k => k.Key).ToList(),
            })
            .ToList();
    }

    public static CurrentSetting Current(SettingDefinition definition, IniDocument? options, IReadOnlyDictionary<string, string> u9gl,
        IReadOnlyDictionary<string, string>? keyMap = null)
    {
        var values = new Dictionary<string, string?>();
        foreach (var key in definition.Keys)
        {
            values[key.Key] = key.File switch
            {
                SettingFile.OptionsIni => SafeGet(options, key.Section ?? "", key.Key),
                SettingFile.KeyMap => keyMap != null && keyMap.TryGetValue(key.Key, out var state) ? state : null,
                _ => u9gl.TryGetValue(key.Key.ToLowerInvariant(), out var value) ? value : null,
            };
        }
        var choice = definition.Choices.FirstOrDefault(c =>
            c.Values.All(p => values.TryGetValue(p.Key, out var current) && current == p.Value));
        return new CurrentSetting(definition, values, choice);
    }

    private static IniDocument? LoadOptions(string path)
    {
        try { return File.Exists(path) ? IniDocument.Load(path) : null; }
        catch (IOException) { return null; }
    }

    private static IReadOnlyDictionary<string, string> LoadU9gl(GameFolder folder)
    {
        bool installed = File.Exists(folder.U9glIni);
        var result = new Dictionary<string, string>(Effective(folder.DefaultsIni));
        if (installed)
            foreach (var (key, value) in Effective(folder.U9glIni)) result[key] = value;
        else
        {
            try { foreach (var (key, value) in U9glIni.ReadProfile(folder.ProfileIni)) result[key] = value; }
            catch (IOException) { }
        }
        foreach (var (key, value) in Effective(folder.DiagIni)) result[key] = value;
        return result;
    }

    private static IReadOnlyDictionary<string, string> KeyMapValues(GameFolder folder)
    {
        var result = new Dictionary<string, string>();
        try
        {
            if (KeyMap.PathFor(folder) is { } path && File.Exists(path) &&
                KeyMap.State(File.ReadAllText(path, Encoding.Latin1)) is { } state)
                result[SettingsCatalog.WasdMovementKey] = state;
        }
        catch (IOException) { }
        return result;
    }

    private static IReadOnlyDictionary<string, string> Effective(string path)
    {
        try
        {
            return File.Exists(path)
                ? U9glIni.Effective(File.ReadAllText(path, Encoding.Latin1))
                : new Dictionary<string, string>();
        }
        catch (IOException)
        {
            return new Dictionary<string, string>();
        }
    }

    private static string? SafeGet(IniDocument? ini, string section, string key)
    {
        if (ini == null) return null;
        try { return ini.Get(section, key); }
        catch (IniException) { return null; }
    }
}
