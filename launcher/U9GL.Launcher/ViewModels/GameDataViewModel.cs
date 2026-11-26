using System.Windows.Input;
using U9GL.Core.Data;
using U9GL.Core.Saves;

namespace U9GL.Launcher.ViewModels;

public sealed record GameDataFileRow(string Name, string State, string Detail, Tone Tone);

public abstract class StatusRowViewModel : ObservableObject
{
    private string _short = "Not checked";
    private Tone _tone = Tone.Neutral;
    private string _note = "";

    public string ShortStatus { get => _short; protected set => SetProperty(ref _short, value); }

    public Tone Tone
    {
        get => _tone;
        protected set { if (SetProperty(ref _tone, value)) OnPropertyChanged(nameof(ShowNote)); }
    }

    public virtual bool ShowNote => Tone is Tone.Warning or Tone.Bad;
    public string Note { get => _note; protected set => SetProperty(ref _note, value); }
    public abstract string Title { get; }
}

public sealed class StartupSaveViewModel : StatusRowViewModel
{
    public override string Title => "Startup savegame";

    public void SetChecking()
    {
        ShortStatus = "Checking";
        Tone = Tone.Neutral;
        Note = "Reading the save the game loads at start...";
    }

    public void SetUnavailable(string why)
    {
        ShortStatus = "None";
        Tone = Tone.Neutral;
        Note = why;
    }

    public void SetResult(ReportViewModel report)
    {
        var integrity = report.Report;
        if (integrity.NoSaveSelected)
        {
            SetUnavailable("No save is selected to load at start.");
            return;
        }
        (ShortStatus, Tone) = integrity.Overall switch
        {
            Severity.Fatal => ("UNSAFE", Tone.Bad),
            Severity.Error => ("ERRORS", Tone.Bad),
            Severity.Warn => ("REVIEW", Tone.Warning),
            _ => ("OK", Tone.Good),
        };
        string slot = integrity.Slot is int n ? $"Slot {n}" : "start.dat";
        string patch = report.MissesFanPatch ? " " + report.FanPatchAdvice : "";
        if (integrity.Overall == Severity.Pass)
        {
            Note = $"{slot} loads at start; nothing wrong found.{patch}";
            return;
        }
        string summary = report.Summary().TrimEnd();
        if (!summary.EndsWith('.')) summary += ".";
        Note = $"{slot}: {summary}{patch} See Save data for the detail.";
    }

    public void SetError(string error)
    {
        ShortStatus = "UNREADABLE";
        Tone = Tone.Bad;
        Note = error;
    }
}

public sealed class SavesPatchViewModel : StatusRowViewModel
{
    private const int ShownSlots = 6;

    public override string Title => "Forgotten World saves";

    public void SetChecking()
    {
        ShortStatus = "Checking";
        Tone = Tone.Neutral;
        Note = "Reading each save's own copy of the main map...";
    }

    public void SetUnchecked(string? note = null)
    {
        ShortStatus = "Not checked";
        Tone = Tone.Neutral;
        Note = note ?? "Check game data to see which saves have the Forgotten World patch's fixes.";
    }

    public void SetResult(FanPatchSaves result)
    {
        string unreadable = result.Unreadable == 0 ? "" : $" {Plural(result.Unreadable, "save")} could not be read.";
        if (!result.PatchInstalled)
        {
            ShortStatus = "N/A";
            Tone = Tone.Neutral;
            Note = "No Forgotten World map changes are installed, so no save can miss them." + unreadable;
            return;
        }
        if (result.Saves.Count == 0)
        {
            ShortStatus = "NONE";
            Tone = Tone.Neutral;
            Note = "There are no saves yet." + unreadable;
            return;
        }
        var partial = result.Partial;
        if (partial.Count == 0)
        {
            ShortStatus = "OK";
            Tone = Tone.Good;
            Note = $"All {Plural(result.Saves.Count, "save")} have every Forgotten World map fix." + unreadable;
            return;
        }
        string slots = string.Join(", ", partial.Take(ShownSlots).Select(s => s.Slot))
                       + (partial.Count > ShownSlots ? $" and {partial.Count - ShownSlots} more" : "");
        ShortStatus = $"{partial.Count} PARTIAL";
        Tone = Tone.Warning;
        Note = $"{partial.Count} of {Plural(result.Saves.Count, "save")} started before the Forgotten World patch (slot{(partial.Count == 1 ? "" : "s")} {slots}): " +
               "they load normally but miss some of its fixes. Start a new game for every fix." + unreadable;
    }

    private static string Plural(int n, string noun) => n == 1 ? $"1 {noun}" : $"{n} {noun}s";
}

public sealed class PatchStatusViewModel(FanPatchFamily family) : StatusRowViewModel
{
    public FanPatchFamily Family { get; } = family;

    public override string Title => Family.Title;

    public string? Installed { get; private set; }

    public override bool ShowNote => Tone != Tone.Neutral;

    public void Update(IReadOnlyList<GameDataFolderViewModel> folders, IReadOnlyList<ContentDiff> contents)
    {
        Installed = null;
        if (folders.Any(f => f.PatchSets == null))
        {
            ShortStatus = "Not checked";
            Tone = Tone.Neutral;
            Note = $"Check game data to see whether {Family.Title} is installed.";
            return;
        }
        var verdict = GameDataPatch.ResolveFamily(folders.SelectMany(f => f.PatchSets!), Family.Id);

        var renamed = Family.Id == "DP" ? contents.FirstOrDefault(c => c.AvatarName != null) : null;
        if (renamed != null && verdict.Kind != GameDataPatchKind.Mixed)
            verdict = new GameDataPatchVerdict(GameDataPatchKind.One, renamed.Closest[^1], [renamed.Closest[^1]]);

        string changes = ContentCheck.Describe(
            contents.Where(c => c.Exact.Count > 0 || c.AvatarName != null)
                    .SelectMany(c => c.FromStock)
                    .Where(k => ContentCheck.FamilyOf(k.Key) == Family.Id)
                    .GroupBy(k => k.Key)
                    .Select(g => KeyValuePair.Create(g.Key, g.Sum(k => k.Value))));
        string detail = (changes.Length > 0 ? $" It changes {changes}." : "")
                        + (renamed != null ? $" The Avatar is called {renamed.AvatarName} (renamed with u9name.exe)." : "");

        switch (verdict.Kind)
        {
            case GameDataPatchKind.None:
                ShortStatus = "NONE";
                Tone = Tone.Neutral;
                Note = "Not installed.";
                break;
            case GameDataPatchKind.One when verdict.Fits.Count > 1:
                Installed = verdict.Build!;
                ShortStatus = string.Join(" or ", verdict.Fits.Select(GameDataPatch.VersionOf));
                Tone = Tone.Warning;
                Note = Family.Id == "DP"
                    ? "Its books and item names are installed, but not its text.flx as the patch ships it, so the "
                      + "version cannot be told. Renaming the Avatar with u9name.exe changes that file."
                    : "Only files these versions share are installed, so the version cannot be told. Installing the newest one again sets every file it carries.";
                break;
            case GameDataPatchKind.One:
                Installed = verdict.Build!;
                string version = GameDataPatch.VersionOf(Installed);
                ShortStatus = version.Length > 0 ? version : "INSTALLED";
                bool latest = Family.Latest == null || Installed == Family.Latest;
                Tone = latest ? Tone.Good : Tone.Warning;
                Note = Family.About + detail
                       + (latest ? "" : $" {GameDataPatch.VersionOf(Family.Latest!)} is newer: update available.")
                       + (Family.Id == "DP" ? " Choose subtitles only under Speech on the Game page: the recordings still say the original lines." : "")
                       + SavesSentence(out bool savesWarn);
                if (savesWarn) Tone = Tone.Warning;
                break;
            default:
                ShortStatus = "MIXED";
                Tone = Tone.Warning;
                Note = $"No one version ships all these files: {string.Join(", ", verdict.Names)}. Installing the newest one again sets every file it carries.";
                break;
        }
    }

    private const int ShownSlots = 6;

    public NpcPatchSaves? NpcSaves { get; set; }

    private string SavesSentence(out bool warn)
    {
        warn = false;
        if (Family.Saves != null) return " " + Family.Saves;
        if (Family.Id != "Monster") return "";
        if (NpcSaves == null) return " Monsters change only in a game started after it; Check game data reads which saves were.";
        if (NpcSaves.Saves.Count == 0) return " Monsters change only in a game started after it; there are no saves yet.";
        var before = NpcSaves.Before;
        if (before.Count == 0) return $" All {Plural(NpcSaves.Saves.Count, "save")} have its hit points.";
        warn = true;
        string slots = string.Join(", ", before.Take(ShownSlots).Select(s => s.Slot))
                       + (before.Count > ShownSlots ? $" and {before.Count - ShownSlots} more" : "");
        return before.Count == NpcSaves.Saves.Count
            ? $" Every save (slot{(before.Count == 1 ? "" : "s")} {slots}) was started before it: monsters there keep their old hit points. Start a new game for it."
            : $" {before.Count} of {Plural(NpcSaves.Saves.Count, "save")} were started before it (slot{(before.Count == 1 ? "" : "s")} {slots}): monsters there keep their old hit points.";
    }

    private static string Plural(int n, string noun) => n == 1 ? $"1 {noun}" : $"{n} {noun}s";
}

public sealed class UnknownEditViewModel(ContentDiff diff) : StatusRowViewModel
{
    public ContentDiff Diff { get; } = diff;

    public override string Title => Diff.Name;

    public void Show()
    {
        ShortStatus = "UNKNOWN EDIT";
        Tone = Tone.Warning;
        string closest = Diff.Closest.Count == 0 || Diff.Closest.Contains(GameDataManifest.BaseBuild)
            ? ""
            : $" Closest known version: {string.Join(" or ", Diff.Closest)}, {Count(Diff.FromClosest)} off.";
        string stock = ContentCheck.Describe(Diff.FromStock);
        Note = "Matches no version U9GL knows." + closest
               + (stock.Length > 0 ? $" Changed from the release: {stock}." : "");
    }

    private static string Count(int n) => n == 1 ? "1 entry" : $"{n:N0} entries";
}

public sealed class GameDataFolderViewModel : StatusRowViewModel
{
    private const int ShownFiles = 12;
    private const string NotChecked = "Not checked";

    private string _status = NotChecked;
    private string _checkedText = "";
    private IReadOnlyList<GameDataFileRow> _files = [];
    private IReadOnlyList<string> _notes = [];
    private IReadOnlyList<IReadOnlyList<string>>? _patchSets;

    public GameDataFolderViewModel(GameDataGroup group, long knownFiles)
    {
        Folder = group.Folder;
        Title = group.Title;
        Description = group.Description;
        KnownFiles = knownFiles;
    }

    public string Folder { get; }
    public override string Title { get; }
    public string Description { get; }
    public long KnownFiles { get; }

    public string StatusText { get => _status; private set => SetProperty(ref _status, value); }

    public string CheckedText { get => _checkedText; private set => SetProperty(ref _checkedText, value); }

    public IReadOnlyList<GameDataFileRow> Files { get => _files; private set { SetProperty(ref _files, value); OnPropertyChanged(nameof(HasFiles)); } }

    public bool HasFiles => _files.Count > 0;

    public IReadOnlyList<IReadOnlyList<string>>? PatchSets { get => _patchSets; private set => SetProperty(ref _patchSets, value); }

    public IReadOnlyList<string> Notes { get => _notes; private set => SetProperty(ref _notes, value); }

    public void SetChecking(string text)
    {
        StatusText = ShortStatus = "Checking";
        Tone = Tone.Neutral;
        Note = text;
        PatchSets = null;
    }

    public void SetUnchecked(string? reason = null)
    {
        StatusText = ShortStatus = NotChecked;
        Tone = Tone.Neutral;
        Note = reason ?? $"{KnownFiles} files to read.";
        CheckedText = "";
        Files = [];
        Notes = [];
        PatchSets = null;
    }

    public void SetCached(GameDataCacheEntry entry)
    {
        (StatusText, ShortStatus, Tone) = Verdict(Counts.Of(entry));
        Note = Describe(Counts.Of(entry));
        PatchSets = entry.PatchSets;
        CheckedText = $"Checked {Ago(entry.CheckedUtc)}; the folder has not changed since.";
        Files = entry.Problems.OrderBy(f => Order(f.State)).ThenBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
            .Take(ShownFiles).Select(Row).ToList();
        int shown = entry.Patched + entry.Changed + entry.Missing + entry.Unreadable + entry.Extra;
        Notes = shown > Files.Count ? [$"and {shown - Files.Count} more files."] : [];
    }

    public void SetResult(GameDataFolderResult result)
    {
        if (!result.Exists)
        {
            StatusText = ShortStatus = "MISSING";
            Tone = Tone.Bad;
            Note = $"There is no {Folder} folder in the game folder.";
            CheckedText = "";
            Files = [];
            Notes = [];
            PatchSets = [];
            return;
        }
        (StatusText, ShortStatus, Tone) = Verdict(Counts.Of(result));
        Note = Describe(Counts.Of(result));
        PatchSets = result.PatchSets();
        CheckedText = $"Checked at {DateTime.Now:HH:mm:ss}: {result.BytesRead / (double)(1 << 20):0} MB read in {result.Took.TotalSeconds:0.0} s.";

        var rows = result.Files.Where(f => f.State != GameDataState.Ok)
            .OrderBy(f => Order(f.State)).ThenBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
            .Select(Row).ToList();
        Files = rows.Take(ShownFiles).ToList();

        var notes = new List<string>();
        if (rows.Count > ShownFiles) notes.Add($"and {rows.Count - ShownFiles} more files.");
        foreach (string sub in result.Subfolders)
            notes.Add($"{Folder}\\{sub}\\ is a folder inside this one. The game does not read it, so it is not checked.");
        Notes = notes;
    }

    private static GameDataFileRow Row(GameDataFile file) => file.State switch
    {
        GameDataState.Patched when file.Backup => new GameDataFileRow(file.Name, $"PATCH {file.Build}",
            $"The {file.Build} patch's backup of the original file, which it leaves beside its own.", Tone.Good),
        GameDataState.Patched when file.Builds.Count > 1 => new GameDataFileRow(file.Name, $"PATCH {file.Build}",
            $"The {file.Build} patch's version of this file; {string.Join(" and ", file.Builds.Skip(1))} ships the same file.", Tone.Good),
        GameDataState.Patched => new GameDataFileRow(file.Name, $"PATCH {file.Build}",
            $"The {file.Build} patch's version of this file.", Tone.Good),
        GameDataState.Changed => new GameDataFileRow(file.Name, "MODIFIED",
            file.ExpectedBytes == file.ActualBytes
                ? $"Same size ({Size(file.ActualBytes)}), different contents."
                : $"{Size(file.ExpectedBytes)} when shipped, {Size(file.ActualBytes)} here.", Tone.Warning),
        GameDataState.Missing => new GameDataFileRow(file.Name, "MISSING",
            $"The game ships this file ({Size(file.ExpectedBytes)}) and it is not here.", Tone.Bad),
        GameDataState.Extra => new GameDataFileRow(file.Name, "EXTRA",
            $"{Size(file.ActualBytes)}, not part of the release.", Tone.Neutral),
        GameDataState.Unreadable => new GameDataFileRow(file.Name, "UNREADABLE",
            file.Error ?? "The file could not be read.", Tone.Bad),
        _ => new GameDataFileRow(file.Name, "OK", "", Tone.Good),
    };

    private static int Order(GameDataState state) => state switch
    {
        GameDataState.Missing => 0,
        GameDataState.Unreadable => 1,
        GameDataState.Changed => 2,
        GameDataState.Patched => 3,
        _ => 4,
    };

    private readonly record struct Counts(int Total, int Ok, int Patched, int Changed, int Missing,
                                          int Extra, int Unreadable, IReadOnlyList<string> PatchNames)
    {
        public static Counts Of(GameDataFolderResult r) =>
            new(r.Total, r.Ok, r.Patched, r.Changed, r.Missing, r.Extra, r.Unreadable, Names(r.PatchSets()));

        public static Counts Of(GameDataCacheEntry e) =>
            new(e.Total, e.Ok, e.Patched, e.Changed, e.Missing, e.Extra, e.Unreadable, Names(e.PatchSets));

        private static IReadOnlyList<string> Names(IEnumerable<IReadOnlyList<string>> sets) =>
            GameDataPatch.ResolveAll(sets).Select(p => p.Verdict.Kind == GameDataPatchKind.One
                                                      ? string.Join(" or ", p.Verdict.Fits)
                                                      : string.Join(" and ", p.Verdict.Names)).ToList();
    }

    private static (string Detail, string Short, Tone Tone) Verdict(Counts c)
    {
        if (c.Missing > 0) return ($"{Count(c.Missing, "file")} missing", "MISSING", Tone.Bad);
        if (c.Unreadable > 0) return ($"{Count(c.Unreadable, "file")} unreadable", "UNREADABLE", Tone.Bad);
        if (c.Changed > 0) return ($"{Count(c.Changed, "file")} changed", "MODIFIED", Tone.Warning);
        if (c.Patched > 0) return ("OK, patched", "OK, PATCHED", Tone.Good);
        return ("OK", "OK", Tone.Good);
    }

    private static string Describe(Counts c)
    {
        var parts = new List<string>();
        if (c.Ok == c.Total && c.Total > 0) parts.Add($"all {c.Total} files are what the game shipped");
        else
        {
            parts.Add($"{c.Ok} of {c.Total} files match");
            if (c.Patched > 0)
                parts.Add(c.PatchNames.Count > 0
                    ? $"{c.Patched} from {string.Join(", ", c.PatchNames)}"
                    : $"{c.Patched} left by a patch");
            if (c.Changed > 0) parts.Add($"{c.Changed} changed");
            if (c.Missing > 0) parts.Add($"{c.Missing} missing");
            if (c.Unreadable > 0) parts.Add($"{c.Unreadable} unreadable");
        }
        if (c.Extra > 0) parts.Add(Count(c.Extra, "extra file"));
        string text = string.Join(", ", parts);
        return char.ToUpperInvariant(text[0]) + text[1..] + ".";
    }

    private static string Count(int n, string noun) => n == 1 ? $"1 {noun}" : $"{n} {noun}s";

    private static string Size(long bytes) => DataPage.Size(bytes);

    private static string Ago(DateTime utc)
    {
        var span = DateTime.UtcNow - utc;
        if (span < TimeSpan.FromMinutes(1)) return "just now";
        if (span < TimeSpan.FromHours(1)) return $"{(int)span.TotalMinutes} min ago";
        if (span < TimeSpan.FromDays(1)) return $"{(int)span.TotalHours} h ago";
        return utc.ToLocalTime().ToString("yyyy-MM-dd HH:mm");
    }
}

public sealed class GameDataViewModel : ObservableObject
{
    private readonly MainViewModel _main;
    private readonly GameDataChecker _checker;
    private readonly GameDataCache _cache;
    private CancellationTokenSource? _cancel;
    private bool _busy;
    private double _progress;
    private string _progressText = "";
    private string _error = "";

    public GameDataViewModel(MainViewModel main, GameDataCache? cache = null, GameDataChecker? checker = null)
    {
        _main = main;
        _checker = checker ?? new GameDataChecker();
        _cache = cache ?? GameDataCache.Load();
        Folders = GameDataManifest.Groups.Select(g => new GameDataFolderViewModel(g, _checker.Manifest.FileCount(g.Folder))).ToList();
        FanPatches = GameDataPatch.Families.Select(f => new PatchStatusViewModel(f)).ToList();
        DashboardRows = [StartupSave, .. Folders];
        CheckCommand = new RelayCommand(() => _ = CheckAsync(), () => !_busy);
        CancelCommand = new RelayCommand(() => _cancel?.Cancel(), () => _busy);
        ManifestNotes = _checker.Manifest.Notes;
        Reload();
    }

    public IReadOnlyList<GameDataFolderViewModel> Folders { get; }

    public StartupSaveViewModel StartupSave { get; } = new();

    public IReadOnlyList<PatchStatusViewModel> FanPatches { get; }

    public SavesPatchViewModel Saves { get; } = new();

    public ExeModificationsViewModel Exe { get; } = new();

    public IReadOnlyList<StatusRowViewModel> DashboardRows { get; }

    public System.Collections.ObjectModel.ObservableCollection<StatusRowViewModel> FanPatchRows { get; } = [];

    public string FanPatchSummary { get => _fanPatchSummary; private set => SetProperty(ref _fanPatchSummary, value); }

    private string _fanPatchSummary = "";
    private IReadOnlyList<ContentDiff> _contents = [];

    private void UpdatePatches()
    {
        foreach (var patch in FanPatches) patch.Update(Folders, _contents);
        var unknown = _contents.Where(c => c.Exact.Count == 0 && c.AvatarName == null)
                               .Select(c => { var row = new UnknownEditViewModel(c); row.Show(); return row; }).ToList();
        bool unchecked_ = Folders.Any(f => f.PatchSets == null);
        var installed = FanPatches.Where(p => p.Installed != null || p.ShortStatus == "MIXED").ToList();

        FanPatchRows.Clear();
        foreach (var row in installed)
        {
            FanPatchRows.Add(row);
            if (row.Family.Id == "FW") FanPatchRows.Add(Saves);
        }
        foreach (var row in unknown) FanPatchRows.Add(row);

        FanPatchSummary = unchecked_ ? "Check game data to see which fan patches are installed."
                        : installed.Count == 0 && unknown.Count == 0 ? "No fan patch is installed; the files are the release's."
                        : installed.Count == 0 ? "No known fan patch is installed."
                        : "";
    }

    private async Task RefreshContentsAsync()
    {
        string root = _main.Folder.Root;
        if (_main.GameRunning) { UpdatePatches(); return; }
        try
        {
            _contents = await Task.Run(() => ContentCheck.Load().CheckAll(root));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            _main.Log.Write($"Fan patch content check failed: {e.Message}");
            _contents = [];
        }
        UpdatePatches();
    }

    public string? NotStockReason
    {
        get
        {
            if (Folders.Any(f => f.PatchSets == null)) return "The game's data files have not all been checked.";
            var installed = FanPatches.Where(p => p.Installed != null || p.ShortStatus == "MIXED").Select(p => p.Title).ToList();
            if (installed.Count > 0) return $"Installed: {string.Join(", ", installed)}.";
            if (_contents.Any(c => c.Exact.Count == 0 && c.AvatarName == null)) return "Some game files carry edits no known fan patch explains.";
            if (Folders.Any(f => f.Tone is Tone.Warning or Tone.Bad)) return "Some of the game's data files are not what the release shipped.";
            return null;
        }
    }

    public IReadOnlyList<string> ManifestNotes { get; }
    public ICommand CheckCommand { get; }
    public ICommand CancelCommand { get; }

    public bool IsBusy
    {
        get => _busy;
        private set { SetProperty(ref _busy, value); CommandManager.InvalidateRequerySuggested(); }
    }

    public double Progress { get => _progress; private set => SetProperty(ref _progress, value); }
    public string ProgressText { get => _progressText; private set => SetProperty(ref _progressText, value); }
    public string ErrorText { get => _error; private set => SetProperty(ref _error, value); }

    public const string GameRunningText =
        "Ultima IX is running and keeps its data files open: quit the game to check them. The results shown are from the last check.";

    public string SummaryText
    {
        get
        {
            if (_main.GameRunning) return GameRunningText;
            var worst = Folders.Select(f => f.Tone).DefaultIfEmpty(Tone.Neutral).Max();
            bool anyChecked = Folders.Any(f => f.StatusText != "Not checked");
            if (!anyChecked) return "The game's data files have not been checked yet.";
            return worst switch
            {
                Tone.Bad => "Some of the game's data files are missing or cannot be read.",
                Tone.Warning => "Some of the game's data files are not what the release shipped. That is expected if you have modded the game.",
                _ => Folders.Any(f => f.PatchSets is { Count: > 0 })
                    ? "Every data file checked is known: what the release shipped, or a known fan patch's version."
                    : "Every data file checked is what the release shipped.",
            };
        }
    }

    private async Task CheckSavesAsync(CancellationToken cancel)
    {
        string root = _main.Folder.Root;
        Saves.SetChecking();
        ProgressText = "Reading the saves...";
        try
        {
            var result = await Task.Run(() => FanPatch.CheckSaves(Path.Combine(root, "savegame"), Path.Combine(root, "runtime"), cancel), cancel);
            Saves.SetResult(result);
            var npc = await Task.Run(() => NpcPatchSavesCheck.Check(Path.Combine(root, "savegame"), Path.Combine(root, "runtime"), cancel), cancel);
            foreach (var patch in FanPatches) patch.NpcSaves = npc;
            UpdatePatches();
        }
        catch (OperationCanceledException)
        {
            Saves.SetUnchecked("Stopped before the saves were read.");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Saves.SetUnchecked($"The saves could not be read: {e.Message}");
        }
        finally
        {
            ProgressText = "";
        }
    }

    public void Reload()
    {
        Exe.Update(_main.Build, _main.Folder.Exe);
        foreach (var folder in Folders)
        {
            var entry = _cache.Get(_main.Folder.Root, folder.Folder);
            if (entry != null && GameDataCache.IsCurrent(entry, _main.Folder.Root, _checker.Manifest.Id)) folder.SetCached(entry);
            else folder.SetUnchecked(entry == null ? null : "Files have changed since the last check.");
        }
        UpdatePatches();
        _ = RefreshContentsAsync();
        if (!_main.GameRunning && ErrorText == GameRunningText) ErrorText = "";
        OnPropertyChanged(nameof(SummaryText));
    }

    public Task CheckOnStartAsync() => CheckAsync(onlyChanged: true);

    public async Task CheckAsync(bool onlyChanged = false)
    {
        if (IsBusy) return;
        if (_main.GameRunning)
        {
            ErrorText = GameRunningText;
            return;
        }
        IsBusy = true;
        ErrorText = "";
        _cancel = new CancellationTokenSource();
        var progress = new Progress<GameDataProgress>(p =>
        {
            Progress = p.BytesTotal == 0 ? 0 : (double)p.BytesDone / p.BytesTotal;
            ProgressText = $"Reading {p.Folder}: {p.FilesDone} of {p.FilesTotal} files";
        });
        try
        {
            foreach (var folder in Folders)
            {
                if (_cancel.IsCancellationRequested) break;
                if (onlyChanged && _cache.Get(_main.Folder.Root, folder.Folder) is { } remembered
                    && remembered.Unreadable == 0 && GameDataCache.IsCurrent(remembered, _main.Folder.Root, _checker.Manifest.Id)) continue;
                folder.SetChecking($"Reading {folder.KnownFiles} files...");
                try
                {
                    var result = await Task.Run(() => _checker.Check(_main.Folder.Root, folder.Folder, progress, _cancel.Token),
                                                _cancel.Token);
                    folder.SetResult(result);
                    if (result.Unreadable == 0) _cache.Put(_main.Folder.Root, result);
                }
                catch (OperationCanceledException)
                {
                    folder.SetUnchecked("Stopped before this folder was read.");
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException)
                {
                    folder.SetUnchecked($"The folder could not be read: {e.Message}");
                    ErrorText = $"{folder.Title}: {e.Message}";
                }
                catch (Exception e)
                {
                    _main.Log.Write($"Game data check of {folder.Folder} failed: {e}");
                    folder.SetUnchecked($"The check failed: {e.Message}");
                    ErrorText = $"{folder.Title}: {e.Message}";
                }
                UpdatePatches();
                OnPropertyChanged(nameof(SummaryText));
            }
            await RefreshContentsAsync();
            ProgressText = _cancel.IsCancellationRequested ? "Stopped. The folders already read keep their results." : "";
            if (!_cancel.IsCancellationRequested)
            {
                await CheckSavesAsync(_cancel.Token);
                if (!onlyChanged) await _main.Data.Current.CheckAsync();
            }
        }
        finally
        {
            Progress = 0;
            IsBusy = false;
            _cancel.Dispose();
            _cancel = null;
        }
    }
}
