using System.Collections.ObjectModel;
using U9GL.Core.Install;

namespace U9GL.Launcher.ViewModels;

public sealed class PlainStatusRow : StatusRowViewModel
{
    public PlainStatusRow(string title, string shortStatus, Tone tone, string note)
    {
        Title = title;
        ShortStatus = shortStatus;
        Tone = tone;
        Note = note;
    }

    public override string Title { get; }
}

public sealed class ExeModificationsViewModel
{
    private ExeChanges? _map;
    private (string Path, long Size, DateTime Written)? _compared;

    public ObservableCollection<StatusRowViewModel> Rows { get; } = [];

    public void Update(GameBuild? build, string exe)
    {
        if (build == null)
        {
            Show([new PlainStatusRow("u9.exe", "MISSING", Tone.Bad, "There is no u9.exe in the game folder.")]);
            _compared = null;
            return;
        }
        if (build.IsSupported)
        {
            Show([new PlainStatusRow("u9.exe", $"OK, {build.Display}", Tone.Good, build.Source == "Steam"
                ? "The 1.19F release as Steam ships it: the GOG file with EA's signature added. No part is modified."
                : "The 1.19F release, byte for byte. No part is modified.")]);
            _compared = null;
            return;
        }

        var info = new FileInfo(exe);
        var key = (info.FullName, info.Length, info.LastWriteTimeUtc);
        if (_compared == key) return;
        _compared = key;
        try
        {
            _map ??= ExeChanges.Load();
            Show(Describe(_map.Compare(exe)));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            Show([new PlainStatusRow("u9.exe", "NOT READ", Tone.Warning, $"u9.exe could not be compared: {e.Message}")]);
        }
    }

    private void Show(IEnumerable<StatusRowViewModel> rows)
    {
        Rows.Clear();
        foreach (var row in rows) Rows.Add(row);
    }

    private static IEnumerable<StatusRowViewModel> Describe(ExeComparison result)
    {
        int parts = result.Changes.Count + (result.Added > 0 ? 1 : 0);
        bool code = result.Changes.Any(c => (c.IsCode && !c.IsValuesInCode) || c.Places.Any(p => p.CheckedByU9gl));
        string what = parts == 1 ? "1 part differs" : $"{parts} parts differ";
        yield return new PlainStatusRow("u9.exe", "MODIFIED", code ? Tone.Bad : Tone.Warning,
            $"Not the 1.19F release: {what} from it. U9GL supports the release only, so Install and Play stay off. "
            + Summary(code, result.Changes.Any(c => c.IsValuesInCode)));

        foreach (var change in result.Changes)
            yield return change.IsValuesInCode ? ValuesInCodeRow(change) : ChangeRow(change);
        if (result.Added > 0)
            yield return new PlainStatusRow("Added at the end", "ADDED", Tone.Warning,
                $"{result.Added:N0} bytes after where the 1.19F file ends: a signature, or a section a tool added.");
        if (result.Size < result.StockSize)
            yield return new PlainStatusRow("Shorter", "CUT", Tone.Bad,
                $"The file is {result.StockSize - result.Size:N0} bytes shorter than 1.19F.");
    }

    private static PlainStatusRow ChangeRow(ExeChange change)
    {
        bool checkedByU9gl = change.Places.Any(p => p.CheckedByU9gl);
        string where = change.VaStart is uint start && change.VaEnd is uint end
            ? $"0x{start:X8}-0x{end - 1:X8}"
            : $"file offset 0x{change.FileStart:X}-0x{change.FileEnd - 1:X}";
        var tables = change.Places.Where(p => !p.CheckedByU9gl).Select(p => p.Name).ToList();
        var checks = change.Places.Where(p => p.CheckedByU9gl).Select(p => p.Name).ToList();
        string note = $"{where} ({Size(change.FileEnd - change.FileStart)}).";
        if (tables.Count > 0) note += $" In the same blocks: {string.Join(", ", tables)}.";
        if (checks.Count > 0) note += $" U9GL checks bytes here before it runs ({string.Join(", ", checks)}).";
        if (change.Part == "header") note += " For example the large address flag, or the checksum.";
        return new PlainStatusRow(PartTitle(change.Part), "CHANGED",
            change.IsCode || checkedByU9gl ? Tone.Bad : Tone.Warning, note);
    }

    private static string Summary(bool code, bool valuesInCode)
    {
        if (code) return "The game's code is changed too, so U9GL's addresses may not hold.";
        if (valuesInCode) return "Only game values are changed (in data, and values the game keeps in its code), not the code U9GL hooks.";
        return "Only data is changed, not the code U9GL hooks.";
    }

    private static PlainStatusRow ValuesInCodeRow(ExeChange change)
    {
        string at = change.VaStart is uint s && change.VaEnd is uint e ? $"0x{s:X8}-0x{e - 1:X8} " : "";
        string names = string.Join(", ", change.Places.Select(p => p.Name));
        return new PlainStatusRow("Code (.text), game values", "CHANGED", Tone.Warning,
            $"{at}({Size(change.FileEnd - change.FileStart)}). Only blocks holding {names}: values the game "
            + "builds by code when it starts, which a stat mod edits there. Most likely a stat edit, not a change "
            + "to code U9GL hooks; the first and last blocks also hold a little code around them.");
    }

    private static string PartTitle(string part) => part switch
    {
        ".text" => "Code (.text)",
        ".rdata" => "Constants (.rdata)",
        ".data" => "Data (.data)",
        ".rsrc" => "Resources (.rsrc)",
        "header" => "PE header",
        "after the sections" => "After the sections",
        var other => other,
    };

    private static string Size(long bytes) => bytes >= 1024 ? $"{bytes / 1024.0:0.#} KB" : $"{bytes} bytes";
}
