using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Data;
using System.Windows.Input;
using U9GL.Core.Saves;

namespace U9GL.Launcher.ViewModels;

public sealed class DataPage : PageViewModel
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private bool _shown;
    private int _selectedTab;

    public DataPage(MainViewModel main) : base(main, "Data")
    {
        Current = new CurrentSaveViewModel(this);
        Saves = new SaveListViewModel(this);
    }

    public CurrentSaveViewModel Current { get; }
    public SaveListViewModel Saves { get; }

    public int SelectedTab
    {
        get => _selectedTab;
        set => SetProperty(ref _selectedTab, value);
    }

    public string SaveDirectory => Path.Combine(Main.Folder.Root, "savegame");
    public string StaticDirectory => Path.Combine(Main.Folder.Root, "static");

    public string ReferenceDirectory => StaticDirectory;

    public string BackupDirectory => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "U9GL", "Save backups");

    public string ReportDirectory => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "U9GL", "Save reports");

    public static string ActionLabel(string action) => SaveWording.Shared.Actions[action].Label;

    internal async Task<(string? Folder, string? Error)> BackupAsync(IntegrityReport report)
    {
        if (report.Archive is not { } archive) return (null, "There is no save file to back up.");
        try
        {
            string folder = await Task.Run(() => SaveBackup.Create(archive, SaveDirectory, BackupDirectory, report.Slot));
            Main.Log.Write($"Save backup of slot {report.Slot}: {folder}");
            return (folder, null);
        }
        catch (Exception e) when (e is SaveBackupException or IOException or UnauthorizedAccessException)
        {
            Main.Log.Write($"Save backup of slot {report.Slot} failed: {e.Message}");
            return (null, e.Message);
        }
    }

    internal (string? Folder, string? Error) SaveReport(ReportViewModel report)
    {
        try
        {
            Directory.CreateDirectory(ReportDirectory);
            string stem = report.Report.Slot is int slot ? $"save_report_slot{slot}" : "save_report";
            string folder = Path.Combine(ReportDirectory, stem);
            for (int i = 1; Directory.Exists(folder) || File.Exists(folder); i++) folder = Path.Combine(ReportDirectory, $"{stem}_{i}");
            Directory.CreateDirectory(folder);
            File.WriteAllText(Path.Combine(folder, "summary.txt"), report.PlainText + "\n");
            File.WriteAllText(Path.Combine(folder, "integrity.txt"), report.Text + "\n");
            File.WriteAllText(Path.Combine(folder, "integrity.json"), report.Report.ToJson() + "\n");
            OpenFolder(folder);
            return (folder, null);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return (null, $"The report could not be saved: {e.Message}");
        }
    }

    private SaveIntegrityChecker Checker { get; } = new();

    public void OnShown()
    {
        if (_shown) return;
        _shown = true;
        Saves.Rescan();
        if (Current.Report == null && !Current.IsBusy) _ = Current.CheckAsync();
    }

    internal async Task<(ReportViewModel? Report, string? Error, TimeSpan Took)> RunAsync(Func<SaveIntegrityChecker, IntegrityReport> check)
    {
        await _gate.WaitAsync();
        var clock = Stopwatch.StartNew();
        try
        {
            var report = await Task.Run(() => new ReportViewModel(check(Checker)));
            return (report, null, clock.Elapsed);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return (null, $"A file could not be read: {e.Message}", clock.Elapsed);
        }
        catch (Exception e)
        {
            Main.Log.Write($"Save check failed: {e}");
            return (null, $"The check failed: {e.Message}", clock.Elapsed);
        }
        finally
        {
            _gate.Release();
        }
    }

    internal IntegrityReport CheckCurrent(SaveIntegrityChecker checker) =>
        checker.CheckSave(Main.Folder.Root, staticDirectory: StaticDirectory, fixedReferenceDirectory: ReferenceDirectory);

    internal IntegrityReport CheckSlot(SaveIntegrityChecker checker, int slot) =>
        checker.CheckArchive(SaveDirectory, slot, StaticDirectory, ReferenceDirectory);

    internal IntegrityReport CheckListed(SaveIntegrityChecker checker, int slot)
    {
        int? selected;
        try { selected = U9StartDat.FromFile(Path.Combine(SaveDirectory, "start.dat")).Slot; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or U9SaveException) { selected = null; }
        return selected == slot
            ? checker.CheckSave(Main.Folder.Root, slot, StaticDirectory, ReferenceDirectory)
            : CheckSlot(checker, slot);
    }

    internal void CurrentChecked(ReportViewModel report, TimeSpan took)
    {
        Current.Show(report, took);
        Saves.ApplyCurrent(report);
    }

    internal void CurrentFailed(string error)
    {
        Current.ShowError(error);
        Saves.ClearCurrent();
    }

    internal static void OpenFolder(string path)
    {
        try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
        catch (Win32Exception) {  }
    }

    internal static void Copy(string text)
    {
        try { Clipboard.SetText(text); }
        catch (COMException) {  }
    }

    internal static string Size(long bytes) =>
        bytes >= 1 << 20 ? $"{bytes / (double)(1 << 20):0.0} MB" : bytes >= 1024 ? $"{bytes / 1024.0:0} KB" : $"{bytes} bytes";

    internal static string When(DateTime time) => time.ToString("yyyy-MM-dd HH:mm");
}

public sealed record AxisRow(string Name, string Verdict, Tone Tone, string Meaning);

public sealed record CardRow(string Title, string Meanings, string WhatToDo, string LoadText, string Badge, Tone Tone, bool IsNote,
                             string Key, string Technical, string Sources, IReadOnlyList<string> Evidence);

public sealed class ReportViewModel
{
    public ReportViewModel(IntegrityReport report)
    {
        Report = report;
        Plain = SaveWording.Shared.Describe(report);
        AssessmentTitle = Plain.Summary.Title;
        AssessmentText = Plain.Summary.Meaning;
        LoadText = Plain.Summary.LoadText;
        WhatToDo = Plain.Summary.WhatToDo ?? "";
        AssessmentTone = report.NoSaveSelected ? Tone.Neutral : ToneOf(report.Overall);
        Notes = Plain.Notes;
        Text = IntegrityText.Render(report);
        PlainText = SaveWording.Render(Plain);
        if (report.NoSaveSelected)
        {
            Axes = [];
            Cards = [];
            Evidence = [];
            return;
        }
        Axes =
        [
            Axis(report, Core.Saves.Axis.Custody, "Working files",
                report.CustodyApplicable
                    ? "The loose files in savegame came from this save."
                    : "Only checked for the save start.dat selects: the loose files in savegame belong to that one."),
            Axis(report, Core.Saves.Axis.Structure, "File structure", "The save's files are intact inside."),
            Axis(report, Core.Saves.Axis.Compatibility, "References", "What the save points at exists in the map files."),
        ];
        if (!report.CustodyApplicable)
        {
            int rank = report.Findings.Where(f => f.Axis == Core.Saves.Axis.Custody && f.AffectsVerdict)
                             .Select(f => Core.Saves.Severity.Rank(f.Severity)).DefaultIfEmpty(0).Max();
            if (rank > 0)
            {
                string verdict = Core.Saves.Severity.FromRank(rank);
                Axes = [new AxisRow("Save file", VerdictText(verdict), ToneOf(verdict), "The save file reads whole, with only shipped map numbers."),
                        .. Axes];
            }
        }
        Cards = Plain.Cards.Select(c => Card(c, report.Target)).ToList();
        Evidence = report.Artifacts.Select(a => $"{a.Key}: {Summary(a.Value)}").ToList();
    }

    public IntegrityReport Report { get; }
    public SavePresentation Plain { get; }
    public string AssessmentTitle { get; }
    public string AssessmentText { get; }
    public string LoadText { get; }
    public string WhatToDo { get; }
    public Tone AssessmentTone { get; }
    public IReadOnlyList<string> Notes { get; }
    public IReadOnlyList<AxisRow> Axes { get; }
    public IReadOnlyList<CardRow> Cards { get; }
    public IReadOnlyList<string> Evidence { get; }

    public string Text { get; }

    public string PlainText { get; }

    public bool HasCards => Cards.Count > 0;
    public string CardsTitle => Cards.Count == 0 ? "Nothing to report" : Cards.Count == 1 ? "1 finding" : $"{Cards.Count} findings";
    public string EvidenceTitle => $"Files read ({Evidence.Count})";

    public bool Offers(string action) => Plain.Actions.Contains(action);

    public string Summary() =>
        Plain.Cards.FirstOrDefault(c => c.AffectsVerdict && Core.Saves.Severity.Rank(c.Severity) > 0)?.Title ?? Plain.Summary.Title;

    public static string VerdictText(string verdict) => verdict switch
    {
        Severity.Pass => "Pass",
        Severity.Warn => "Review",
        Severity.Error => "Errors",
        Severity.Fatal => "Unsafe",
        _ => "Not applicable",
    };

    public static Tone ToneOf(string verdict) => verdict switch
    {
        Severity.Pass => Tone.Good,
        Severity.Warn => Tone.Warning,
        Severity.Error or Severity.Fatal => Tone.Bad,
        _ => Tone.Neutral,
    };

    private static AxisRow Axis(IntegrityReport report, string axis, string name, string meaning)
    {
        string verdict = report.Verdict(axis);
        return new AxisRow(name, VerdictText(verdict), ToneOf(verdict), meaning);
    }

    private const int ShownLines = 6;

    private static CardRow Card(PlainCard card, string root)
    {
        string prefix = Path.TrimEndingDirectorySeparator(root) + Path.DirectorySeparatorChar;
        string Short(string path) => path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ? path[prefix.Length..] : path;
        var records = IntegrityText.TargetRecords(card.Findings).ToList();
        var evidence = records.Take(8).Select(IntegrityText.EvidenceLine).ToList();
        if (records.Count > 8) evidence.Add($"and {records.Count - 8} more target records (Copy report has them all)");
        (string badge, Tone tone) = card.IsNote ? ("Note", Tone.Neutral)
            : card.Severity == Severity.Info ? ("Info", Tone.Neutral)
            : (VerdictText(card.Severity), ToneOf(card.Severity));
        string technical = $"{card.Key} {card.Findings[0].Axis}\n" + Lines(card.Findings.Select(f => f.Message).Distinct().ToList());
        return new CardRow(card.Title, Lines(card.Meanings.ToList()), card.WhatToDo, card.LoadText, badge, tone, card.IsNote, card.Key,
            technical, Lines(card.Findings.Select(f => f.Path).OfType<string>().Select(Short).Distinct().ToList()), evidence);
    }

    private static string Lines(List<string> lines) =>
        lines.Count <= ShownLines ? string.Join("\n", lines)
            : string.Join("\n", lines.Take(ShownLines - 1)) + $"\nand {lines.Count - (ShownLines - 1)} more";

    private static string Summary(Dictionary<string, object?> artifact)
    {
        var parts = new List<string>();
        if (artifact.GetValueOrDefault("bytes") is int or long) parts.Add(DataPage.Size(Convert.ToInt64(artifact["bytes"])));
        foreach (var (key, label) in new[] { ("chunks", "chunks"), ("pages", "pages"), ("objects", "objects"),
                     ("live_handles", "live handles"), ("incomplete_chunks", "incomplete chunks"), ("invalid_free_lists", "bad free lists") })
            if (artifact.TryGetValue(key, out var value)) parts.Add($"{value} {label}");
        if (artifact.GetValueOrDefault("sha256") is string sha) parts.Add($"SHA-256 {sha[..12]}");
        return string.Join(", ", parts);
    }
}

public sealed class CurrentSaveViewModel : ObservableObject
{
    private readonly DataPage _page;
    private ReportViewModel? _report;
    private bool _busy;
    private string _statusText = "Not checked yet.";
    private Tone _statusTone = Tone.Neutral;
    private string _title = "";
    private string _details = "";
    private string _actionText = "";
    private Tone _actionTone = Tone.Neutral;

    public CurrentSaveViewModel(DataPage page)
    {
        _page = page;
        CheckCommand = new RelayCommand(() => _ = CheckAsync(), () => !_busy);
        CopyCommand = new RelayCommand(() => DataPage.Copy(_report!.Text), () => _report != null);
        OpenFolderCommand = new RelayCommand(() => DataPage.OpenFolder(_page.SaveDirectory));
        BackupCommand = new RelayCommand(() => _ = BackupAsync(), () => !_busy && _report?.Offers("backup") == true);
        BackUpAndPlayCommand = new RelayCommand(() => _ = BackUpAndPlayAsync(), () => !_busy && CanTryLoad);
        OlderSaveCommand = new RelayCommand(() => _page.SelectedTab = 1,
            () => _report?.Offers("load_older_save") == true || _report?.Offers("choose_save") == true);
        SaveReportCommand = new RelayCommand(SaveReport, () => _report?.Offers("send_report") == true);
        OpenBackupsCommand = new RelayCommand(() => DataPage.OpenFolder(_page.BackupDirectory), () => Directory.Exists(_page.BackupDirectory));
    }

    public ICommand CheckCommand { get; }
    public ICommand CopyCommand { get; }
    public ICommand OpenFolderCommand { get; }
    public ICommand BackupCommand { get; }
    public ICommand BackUpAndPlayCommand { get; }
    public ICommand OlderSaveCommand { get; }
    public ICommand SaveReportCommand { get; }
    public ICommand OpenBackupsCommand { get; }

    public string BackupLabel => DataPage.ActionLabel("backup");
    public string OlderSaveLabel => DataPage.ActionLabel("load_older_save");
    public string SaveReportLabel => DataPage.ActionLabel("send_report");
    public string BackupTip => $"Copies this save and the loose files in savegame to a new folder in {_page.BackupDirectory}. Nothing in the game folder changes.";

    public string ActionText { get => _actionText; private set => SetProperty(ref _actionText, value); }
    public Tone ActionTone { get => _actionTone; private set => SetProperty(ref _actionTone, value); }

    public ReportViewModel? Report
    {
        get => _report;
        private set
        {
            SetProperty(ref _report, value);
            OnPropertyChanged(nameof(HasReport));
            OnPropertyChanged(nameof(OffersPlay));
            CommandManager.InvalidateRequerySuggested();
        }
    }

    public bool OffersPlay => _report is { Report: { CustodyApplicable: true, NoSaveSelected: false } } r && r.Offers(SaveWording.TryLoad);

    private bool CanTryLoad => OffersPlay && _page.Main.PlayCommand.CanExecute(null);

    private async Task<bool> BackupAsync()
    {
        if (_report == null || IsBusy) return false;
        IsBusy = true;
        try
        {
            ActionText = "Backing up...";
            ActionTone = Tone.Neutral;
            var (folder, error) = await _page.BackupAsync(_report.Report);
            ActionText = folder != null ? $"Backed up to {folder}" : $"No backup was made: {error}";
            ActionTone = folder != null ? Tone.Good : Tone.Bad;
            return folder != null;
        }
        finally
        {
            IsBusy = false;
        }
    }

    private async Task BackUpAndPlayAsync()
    {
        int? slot = _report?.Report.Slot;
        if (!await BackupAsync()) return;
        string backedUp = ActionText;
        await CheckAsync();
        if (_report?.Report.Slot == slot && CanTryLoad)
        {
            _page.Main.Log.Write($"Data: backed up slot {slot}, checked again ({_report!.Report.Overall}), starting the game");
            ActionText = backedUp + ". Starting the game.";
            _page.Main.PlayCommand.Execute(null);
        }
        else
        {
            ActionText = backedUp + ". The game was not started: after the backup the save no longer checks as one to try. See the result below.";
            ActionTone = Tone.Warning;
        }
    }

    private void SaveReport()
    {
        var (folder, error) = _page.SaveReport(_report!);
        ActionText = folder != null ? $"Report saved to {folder}" : error!;
        ActionTone = folder != null ? Tone.Good : Tone.Bad;
    }

    public bool HasReport => _report != null;

    public bool IsBusy
    {
        get => _busy;
        private set { SetProperty(ref _busy, value); CommandManager.InvalidateRequerySuggested(); }
    }

    public string StatusText { get => _statusText; private set => SetProperty(ref _statusText, value); }
    public Tone StatusTone { get => _statusTone; private set => SetProperty(ref _statusTone, value); }
    public string Title { get => _title; private set => SetProperty(ref _title, value); }
    public string Details { get => _details; private set => SetProperty(ref _details, value); }

    public string RunningNote => string.IsNullOrEmpty(_page.Main.RunningText) ? ""
        : "Ultima IX is running. It rewrites the working files as it plays, so they can differ from the save until it quits.";

    public async Task CheckAsync()
    {
        if (IsBusy) return;
        IsBusy = true;
        try
        {
            StatusText = "Checking the save the game loads at start...";
            StatusTone = Tone.Neutral;
            _page.Main.GameData.StartupSave.SetChecking();
            OnPropertyChanged(nameof(RunningNote));
            var (report, error, took) = await _page.RunAsync(_page.CheckCurrent);
            if (report == null) _page.CurrentFailed(error ?? "The check did not finish.");
            else _page.CurrentChecked(report, took);
        }
        finally
        {
            IsBusy = false;
        }
    }

    internal void Show(ReportViewModel report, TimeSpan took)
    {
        Report = report;
        _page.Main.GameData.StartupSave.SetResult(report);
        var integrity = report.Report;
        Title = integrity.NoSaveSelected ? "No save selected"
            : integrity.Slot == null ? "start.dat could not be read"
            : integrity.Header == null ? $"Slot {integrity.Slot}"
            : $"Slot {integrity.Slot}: {Describe(integrity.Header)}";
        Details = DetailsOf(integrity);
        StatusText = $"Checked at {DateTime.Now:HH:mm:ss}, in {took.TotalSeconds:0.0} s.";
        StatusTone = Tone.Neutral;
    }

    internal void ShowError(string error)
    {
        Report = null;
        Title = "";
        Details = "";
        StatusText = error;
        StatusTone = Tone.Bad;
        _page.Main.GameData.StartupSave.SetError(error);
    }

    internal static string Describe(U9SaveHeader header) =>
        header.Description.Length > 0 ? header.Description : header.Label.Length > 0 ? header.Label : "(no description)";

    private static string DetailsOf(IntegrityReport report)
    {
        var parts = new List<string> { "start.dat selects this slot" };
        if (report.Header is { } h) parts.Add($"saved in {SaveWording.Shared.Map(h.HeaderMap).DisplayName}, game time {h.Time}");
        if (report.Archive is { } path && File.Exists(path))
        {
            var info = new FileInfo(path);
            parts.Add($"{info.Name}, {DataPage.Size(info.Length)}, {DataPage.When(info.LastWriteTime)}");
        }
        return string.Join(" · ", parts);
    }
}

public sealed class SaveRowViewModel(SaveSlotInfo info, bool isCurrent, bool checkable, bool duplicate) : ObservableObject
{
    private ReportViewModel? _report;
    private string _state = checkable ? "Not checked" : "Skipped";
    private Tone _tone = Tone.Neutral;
    private int _rank = -1;
    private string _summary = info.HeaderError != null ? $"The header could not be read: {info.HeaderError}"
        : checkable ? ""
        : duplicate ? $"Another file also claims slot {info.Slot}; the game looks for u9game{info.Slot}.sav."
        : $"The game looks for u9game{info.Slot}.sav, so it does not load this file.";

    public SaveSlotInfo Info { get; } = info;
    public int Slot => Info.Slot;
    public bool IsCurrent { get; } = isCurrent;
    public bool Checkable { get; } = checkable;
    public string CurrentMark => IsCurrent ? "current" : "";
    public string Description => Info.Header is { } h ? CurrentSaveViewModel.Describe(h) : Info.FileName;
    public int? Map => Info.Header?.HeaderMap;
    public DateTime Modified => Info.Modified;
    public string ModifiedText => DataPage.When(Info.Modified);
    public long Bytes => Info.Bytes;
    public string SizeText => DataPage.Size(Info.Bytes);

    public string ResultText { get => _state; private set => SetProperty(ref _state, value); }
    public Tone ResultTone { get => _tone; private set => SetProperty(ref _tone, value); }

    public int ResultRank { get => _rank; private set => SetProperty(ref _rank, value); }

    public string Summary { get => _summary; private set => SetProperty(ref _summary, value); }

    public ReportViewModel? Report
    {
        get => _report;
        private set => SetProperty(ref _report, value);
    }

    public bool HasProblems => _rank > 0;

    public void SetChecking()
    {
        ResultText = "Checking...";
        ResultTone = Tone.Neutral;
    }

    public void SetResult(ReportViewModel report)
    {
        Report = report;
        string overall = report.Report.Overall;
        ResultText = ReportViewModel.VerdictText(overall);
        ResultTone = ReportViewModel.ToneOf(overall);
        ResultRank = Severity.Rank(overall);
        Summary = report.Summary();
        OnPropertyChanged(nameof(HasProblems));
    }

    public void SetUnchecked(string note)
    {
        Report = null;
        ResultText = "Not checked";
        ResultTone = Tone.Neutral;
        ResultRank = -1;
        Summary = note;
        OnPropertyChanged(nameof(HasProblems));
    }

    public void SetError(string error)
    {
        ResultText = "Unreadable";
        ResultTone = Tone.Bad;
        ResultRank = SaveListViewModel.UnreadableRank;
        Summary = error;
        OnPropertyChanged(nameof(HasProblems));
    }
}

public sealed record FolderFileRow(string Name, string SizeText, string ModifiedText, string Note, Tone Tone);

public sealed class SaveListViewModel : ObservableObject
{
    internal const int UnreadableRank = 4;

    private readonly DataPage _page;
    private SaveFolderScan? _scan;
    private ReportViewModel? _currentReport;
    private SaveRowViewModel? _selected;
    private string _filter = "";
    private bool _problemsOnly;
    private bool _checking;
    private CancellationTokenSource? _cancel;
    private double _progress;
    private string _progressText = "";
    private IReadOnlyList<FolderFileRow> _otherFiles = [];
    private IReadOnlyList<string> _folderNotes = [];
    private string _actionText = "";
    private Tone _actionTone = Tone.Neutral;
    private bool _backingUp;

    public SaveListViewModel(DataPage page)
    {
        _page = page;
        View = CollectionViewSource.GetDefaultView(Rows);
        View.Filter = Matches;
        View.SortDescriptions.Add(new SortDescription(nameof(SaveRowViewModel.Slot), ListSortDirection.Ascending));
        CheckAllCommand = new RelayCommand(() => _ = CheckAllAsync(), () => !_checking && Rows.Any(r => r.Checkable));
        CheckSelectedCommand = new RelayCommand(() => _ = CheckRowsAsync([_selected!]), () => !_checking && _selected is { Checkable: true });
        CancelCommand = new RelayCommand(() => _cancel?.Cancel(), () => _checking);
        RescanCommand = new RelayCommand(Rescan, () => !_checking);
        OpenFolderCommand = new RelayCommand(() => DataPage.OpenFolder(_page.SaveDirectory));
        CopyCommand = new RelayCommand(() => DataPage.Copy(_selected!.Report!.Text), () => _selected?.Report != null);
        BackupCommand = new RelayCommand(() => _ = BackupAsync(), () => !_checking && !_backingUp && _selected?.Report?.Offers("backup") == true);
        SaveReportCommand = new RelayCommand(SaveReport, () => _selected?.Report?.Offers("send_report") == true);
    }

    public ObservableCollection<SaveRowViewModel> Rows { get; } = [];
    public ICollectionView View { get; }

    public ICommand CheckAllCommand { get; }
    public ICommand CheckSelectedCommand { get; }
    public ICommand CancelCommand { get; }
    public ICommand RescanCommand { get; }
    public ICommand OpenFolderCommand { get; }
    public ICommand CopyCommand { get; }
    public ICommand BackupCommand { get; }
    public ICommand SaveReportCommand { get; }

    public string BackupLabel => DataPage.ActionLabel("backup");
    public string SaveReportLabel => DataPage.ActionLabel("send_report");

    public string ActionText { get => _actionText; private set => SetProperty(ref _actionText, value); }
    public Tone ActionTone { get => _actionTone; private set => SetProperty(ref _actionTone, value); }

    private async Task BackupAsync()
    {
        var row = _selected!;
        _backingUp = true;
        CommandManager.InvalidateRequerySuggested();
        try
        {
            ActionText = $"Backing up slot {row.Slot}...";
            ActionTone = Tone.Neutral;
            var (folder, error) = await _page.BackupAsync(row.Report!.Report);
            ActionText = folder != null ? $"Slot {row.Slot} backed up to {folder}" : $"No backup was made: {error}";
            ActionTone = folder != null ? Tone.Good : Tone.Bad;
        }
        finally
        {
            _backingUp = false;
            CommandManager.InvalidateRequerySuggested();
        }
    }

    private void SaveReport()
    {
        var (folder, error) = _page.SaveReport(_selected!.Report!);
        ActionText = folder != null ? $"Report saved to {folder}" : error!;
        ActionTone = folder != null ? Tone.Good : Tone.Bad;
    }

    public SaveRowViewModel? Selected
    {
        get => _selected;
        set { SetProperty(ref _selected, value); CommandManager.InvalidateRequerySuggested(); }
    }

    public string FilterText
    {
        get => _filter;
        set { if (SetProperty(ref _filter, value)) View.Refresh(); }
    }

    public bool ProblemsOnly
    {
        get => _problemsOnly;
        set { if (SetProperty(ref _problemsOnly, value)) View.Refresh(); }
    }

    public bool IsChecking
    {
        get => _checking;
        private set { SetProperty(ref _checking, value); CommandManager.InvalidateRequerySuggested(); }
    }

    public double Progress { get => _progress; private set => SetProperty(ref _progress, value); }
    public string ProgressText { get => _progressText; private set => SetProperty(ref _progressText, value); }
    public IReadOnlyList<FolderFileRow> OtherFiles { get => _otherFiles; private set => SetProperty(ref _otherFiles, value); }
    public IReadOnlyList<string> FolderNotes { get => _folderNotes; private set => SetProperty(ref _folderNotes, value); }
    public string OtherFilesTitle => $"Other files in the save folder ({_otherFiles.Count})";

    public string SummaryText
    {
        get
        {
            if (_scan == null) return "";
            if (!_scan.Exists) return $"There is no save folder at {_page.SaveDirectory}.";
            if (Rows.Count == 0) return "No saves in the save folder.";
            string current = _scan.SelectedSlot is int s ? $"the game loads slot {s} at start" : "start.dat selects no save";
            var checkedRows = Rows.Where(r => r.Report != null || r.ResultRank == UnreadableRank).ToList();
            if (checkedRows.Count == 0) return $"{Plural(Rows.Count, "save")}, {current}. None checked yet: Check all reads every save.";
            int pass = checkedRows.Count(r => r.ResultRank == 0), review = checkedRows.Count(r => r.ResultRank == 1),
                errors = checkedRows.Count(r => r.ResultRank == 2), unsafeCount = checkedRows.Count(r => r.ResultRank == 3),
                unreadable = checkedRows.Count(r => r.ResultRank == UnreadableRank);
            return $"{Plural(Rows.Count, "save")}, {current}. Checked {checkedRows.Count}: {pass} pass, {review} to review, " +
                   $"{errors} with errors, {unsafeCount} unsafe" + (unreadable > 0 ? $", {unreadable} unreadable." : ".");
        }
    }

    public void Rescan()
    {
        _scan = SaveFolderScan.Read(_page.SaveDirectory);
        Rows.Clear();
        foreach (var slot in _scan.Slots)
        {
            bool canonical = string.Equals(slot.FileName, $"u9game{slot.Slot}.sav", StringComparison.OrdinalIgnoreCase);
            var row = new SaveRowViewModel(slot, canonical && slot.Slot == _scan.SelectedSlot, canonical && slot.HeaderError == null,
                                           _scan.DuplicateSlots.Contains(slot.Slot));
            if (slot.HeaderError != null) row.SetError($"The header could not be read: {slot.HeaderError}");
            Rows.Add(row);
        }
        if (_currentReport != null) ApplyCurrent(_currentReport);
        Selected = Rows.FirstOrDefault(r => r.IsCurrent) ?? Rows.FirstOrDefault();
        DescribeFolder();
        OnPropertyChanged(nameof(SummaryText));
    }

    public void ApplyCurrent(ReportViewModel report)
    {
        _currentReport = report;
        Rows.FirstOrDefault(r => r.Checkable && r.Slot == report.Report.Slot)?.SetResult(report);
        DescribeFolder();
        OnPropertyChanged(nameof(SummaryText));
        View.Refresh();
    }

    public void ClearCurrent()
    {
        if (_currentReport?.Report.Slot is int slot)
            Rows.FirstOrDefault(r => r.Checkable && r.Slot == slot && r.Report == _currentReport)
                ?.SetUnchecked("The last check of this save failed; see the Current save tab.");
        _currentReport = null;
        DescribeFolder();
        OnPropertyChanged(nameof(SummaryText));
        View.Refresh();
    }

    private async Task CheckAllAsync() => await CheckRowsAsync(Rows.Where(r => r.Checkable).OrderBy(r => r.Slot).ToList());

    private async Task CheckRowsAsync(IReadOnlyList<SaveRowViewModel> rows)
    {
        IsChecking = true;
        _cancel = new CancellationTokenSource();
        try
        {
            for (int i = 0; i < rows.Count && !_cancel.IsCancellationRequested; i++)
            {
                var row = rows[i];
                ProgressText = $"Checking slot {row.Slot} ({i + 1} of {rows.Count})...";
                Progress = (double)i / rows.Count;
                row.SetChecking();
                var (report, error, took) = await _page.RunAsync(c => _page.CheckListed(c, row.Slot));
                if (report == null) row.SetError(error ?? "The check did not finish.");
                else if (report.Report.CustodyApplicable) _page.CurrentChecked(report, took);
                else row.SetResult(report);
                OnPropertyChanged(nameof(SummaryText));
            }
            ProgressText = _cancel.IsCancellationRequested ? "Stopped. The saves already checked keep their results." : "";
        }
        finally
        {
            Progress = 0;
            IsChecking = false;
            _cancel.Dispose();
            _cancel = null;
            DescribeFolder();
            View.Refresh();
        }
    }

    private bool Matches(object item)
    {
        if (item is not SaveRowViewModel row) return false;
        if (_problemsOnly && !row.HasProblems) return false;
        if (_filter.Length == 0) return true;
        return row.Slot.ToString() == _filter.Trim()
            || row.Description.Contains(_filter, StringComparison.OrdinalIgnoreCase)
            || row.Summary.Contains(_filter, StringComparison.OrdinalIgnoreCase);
    }

    private void DescribeFolder()
    {
        if (_scan == null) return;
        var notes = new List<string>();
        if (_scan.SelectorError != null) notes.Add(_scan.SelectorError);
        else if (_scan.SelectedSlot is int s && _scan.Selected == null)
            notes.Add($"start.dat selects slot {s}, but u9game{s}.sav is not in the folder, so the game has nothing to load at start.");
        foreach (int slot in _scan.DuplicateSlots)
            notes.Add($"More than one file claims slot {slot}. The game reads u9game{slot}.sav; the other copy is not checked.");
        int unfinished = _scan.OtherFiles.Count(f => f.Kind == SaveFileKind.UnfinishedSave);
        if (unfinished > 0)
            notes.Add($"{Plural(unfinished, "save")} did not finish: see the .tmp files below.");
        FolderNotes = notes;

        var report = _currentReport?.Report;
        bool compared = report is { Header: not null, CustodyApplicable: true };
        var archivedMaps = compared ? report!.Artifacts.GetValueOrDefault("archive")?.GetValueOrDefault("maps") as List<int> : null;
        var differs = report?.Findings.Where(f => f.CheckId is "CUS01" or "CUS02" or "CUS03").Select(f => Path.GetFileName(f.Path ?? ""))
                          .ToHashSet(StringComparer.OrdinalIgnoreCase) ?? [];
        string from = report?.Slot is int cur ? $"slot {cur}" : "the current save";
        OtherFiles = _scan.OtherFiles.Select(f => Describe(f, compared, archivedMaps, differs, from)).ToList();
        OnPropertyChanged(nameof(OtherFilesTitle));
    }

    private FolderFileRow Describe(SaveFolderFile f, bool compared, List<int>? archivedMaps, HashSet<string> differs, string from)
    {
        (string note, Tone tone) = f.Kind switch
        {
            SaveFileKind.Selector => _scan!.SelectorError != null ? (_scan.SelectorError, Tone.Bad)
                : ($"Makes the game load slot {_scan.SelectedSlot} when it starts.", Tone.Neutral),
            SaveFileKind.Processes when differs.Contains(f.Name) => ($"Differs from {from}'s copy. The game would load a mixture.", Tone.Bad),
            SaveFileKind.Processes => (compared ? $"Working copy of the loaded save's state; matches {from}." : "Working copy of the loaded save's state.",
                compared ? Tone.Good : Tone.Neutral),
            SaveFileKind.Diary => ("Working copy of the journal.", Tone.Neutral),
            SaveFileKind.Nonfixed when differs.Contains(f.Name) => ($"Differs from {from}'s copy of map {f.Number}.", Tone.Bad),
            SaveFileKind.Nonfixed when archivedMaps?.Contains(f.Number ?? -1) == true =>
                ($"Working copy of map {f.Number}; matches {from}.", Tone.Good),
            SaveFileKind.Nonfixed when archivedMaps != null =>
                ($"Map {f.Number} is not in {from}: left from another save, or made after loading. The next save will include it.", Tone.Warning),
            SaveFileKind.Nonfixed => ($"Working copy of map {f.Number}.", Tone.Neutral),
            SaveFileKind.UnfinishedSave =>
                ($"A save of slot {f.Number} did not finish (the game writes .tmp, then renames it). u9game{f.Number}.sav, if present, is the older save.", Tone.Warning),
            _ => ("Not a file the game uses.", Tone.Neutral),
        };
        return new FolderFileRow(f.Name, DataPage.Size(f.Bytes), DataPage.When(f.Modified), note, tone);
    }

    private static string Plural(int count, string noun) => count == 1 ? $"1 {noun}" : $"{count} {noun}s";
}
