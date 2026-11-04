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

    public Tone Tone { get => _tone; protected set => SetProperty(ref _tone, value); }
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
        if (integrity.Overall == Severity.Pass)
        {
            Note = $"{slot} loads at start; nothing wrong found.";
            return;
        }
        string summary = report.Summary().TrimEnd();
        if (!summary.EndsWith('.')) summary += ".";
        Note = $"{slot}: {summary} See Data for the detail.";
    }

    public void SetError(string error)
    {
        ShortStatus = "UNREADABLE";
        Tone = Tone.Bad;
        Note = error;
    }
}

public sealed class GameDataFolderViewModel : StatusRowViewModel
{
    private const int ShownFiles = 12;
    private const string NotChecked = "Not checked";

    private string _status = NotChecked;
    private string _checkedText = "";
    private IReadOnlyList<GameDataFileRow> _files = [];
    private IReadOnlyList<string> _notes = [];

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

    public IReadOnlyList<string> Notes { get => _notes; private set => SetProperty(ref _notes, value); }

    public void SetChecking(string text)
    {
        StatusText = ShortStatus = "Checking";
        Tone = Tone.Neutral;
        Note = text;
    }

    public void SetUnchecked(string? reason = null)
    {
        StatusText = ShortStatus = NotChecked;
        Tone = Tone.Neutral;
        Note = reason ?? $"{KnownFiles} files to read.";
        CheckedText = "";
        Files = [];
        Notes = [];
    }

    public void SetCached(GameDataCacheEntry entry)
    {
        (StatusText, ShortStatus, Tone) = Verdict(Counts.Of(entry));
        Note = Describe(Counts.Of(entry));
        CheckedText = $"Checked {Ago(entry.CheckedUtc)}; the folder has not changed since.";
        Files = entry.Problems.OrderBy(f => Order(f.State)).ThenBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
            .Take(ShownFiles).Select(Row).ToList();
        int problems = entry.Changed + entry.Missing + entry.Unreadable;
        Notes = problems > Files.Count ? [$"and {problems - Files.Count} more files."] : [];
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
            return;
        }
        (StatusText, ShortStatus, Tone) = Verdict(Counts.Of(result));
        Note = Describe(Counts.Of(result));
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
                                          int Extra, int Unreadable)
    {
        public static Counts Of(GameDataFolderResult r) =>
            new(r.Total, r.Ok, r.Patched, r.Changed, r.Missing, r.Extra, r.Unreadable);

        public static Counts Of(GameDataCacheEntry e) =>
            new(e.Total, e.Ok, e.Patched, e.Changed, e.Missing, e.Extra, e.Unreadable);
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
            if (c.Patched > 0) parts.Add($"{c.Patched} from the 1.19H patch");
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
        DashboardRows = [StartupSave, .. Folders];
        CheckCommand = new RelayCommand(() => _ = CheckAsync(), () => !_busy);
        CancelCommand = new RelayCommand(() => _cancel?.Cancel(), () => _busy);
        ManifestNotes = _checker.Manifest.Notes;
        Reload();
    }

    public IReadOnlyList<GameDataFolderViewModel> Folders { get; }

    public StartupSaveViewModel StartupSave { get; } = new();

    public IReadOnlyList<StatusRowViewModel> DashboardRows { get; }

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

    public string SummaryText
    {
        get
        {
            var worst = Folders.Select(f => f.Tone).DefaultIfEmpty(Tone.Neutral).Max();
            bool anyChecked = Folders.Any(f => f.StatusText != "Not checked");
            if (!anyChecked) return "The game's data files have not been checked yet.";
            return worst switch
            {
                Tone.Bad => "Some of the game's data files are missing or cannot be read.",
                Tone.Warning => "Some of the game's data files are not what the release shipped. That is expected if you have modded the game.",
                _ => "Every data file checked is what the release shipped.",
            };
        }
    }

    public void Reload()
    {
        foreach (var folder in Folders)
        {
            var entry = _cache.Get(_main.Folder.Root, folder.Folder);
            if (entry != null && GameDataCache.IsCurrent(entry, _main.Folder.Root, _checker.Manifest.Id)) folder.SetCached(entry);
            else folder.SetUnchecked(entry == null ? null : "Files have changed since the last check.");
        }
        OnPropertyChanged(nameof(SummaryText));
    }

    public async Task CheckAsync()
    {
        if (IsBusy) return;
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
                folder.SetChecking($"Reading {folder.KnownFiles} files...");
                try
                {
                    var result = await Task.Run(() => _checker.Check(_main.Folder.Root, folder.Folder, progress, _cancel.Token),
                                                _cancel.Token);
                    folder.SetResult(result);
                    _cache.Put(_main.Folder.Root, result);
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
                OnPropertyChanged(nameof(SummaryText));
            }
            ProgressText = _cancel.IsCancellationRequested ? "Stopped. The folders already read keep their results." : "";
            if (!_cancel.IsCancellationRequested) await _main.Data.Current.CheckAsync();
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
