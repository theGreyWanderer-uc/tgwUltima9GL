using System.Text;
using System.Text.RegularExpressions;

namespace U9GL.WebDashboard.Telemetry;

public sealed record DashboardLogEvent(long Seq, string ReceivedUtc, uint RunPid, string Source, string Tag, string Level,
    string Text, string? Detail);
public sealed record DashboardLogPage(DashboardLogEvent[] Events, long Next, long Oldest, DashboardLogSummaryDto Summary);
public sealed record DashboardLogSummaryDto(uint RunPid, string? RunVersion, int Faults, int Warnings, long LastSeq,
    string LogFile, long LogBytes, double? LogGrewAgoMs, string? CrashReport);

public sealed partial class GameLogFollower
{
    public const int EventLimit = 3000, DetailLimit = 4096, TextLimit = 512, PageLimit = 500;
    private readonly object _gate = new();
    private readonly string _logPath;
    private readonly Func<DateTime> _clock;
    private readonly List<DashboardLogEvent> _events = [];
    private long _seq;
    private Tracked _log = new(), _crash = new();
    private string? _crashPath;
    private uint _runPid;
    private string? _runVersion;
    private int _faults, _warnings;
    private DateTime? _logGrewUtc;

    public GameLogFollower(string? logPath = null, Func<DateTime>? clock = null)
    {
        _logPath = logPath ?? Path.Combine(Path.GetTempPath(), "u9log.txt");
        _clock = clock ?? (() => DateTime.UtcNow);
    }

    public void Poll()
    {
        var lines = ReadNew(_logPath, ref _log, out bool restarted);
        if (restarted) NewRun();
        foreach (string line in lines) AddLogLine(line);
        if (_crashPath != null)
            foreach (string line in ReadNew(_crashPath, ref _crash, out _)) AddCrashLine(line);
    }

    public DashboardLogPage Page(long after, int limit = PageLimit)
    {
        lock (_gate)
        {
            limit = Math.Clamp(limit, 1, PageLimit);
            var events = _events.Where(e => e.Seq > after).Take(limit).ToArray();
            long next = events.Length > 0 ? events[^1].Seq : Math.Max(after, 0);
            return new(events, next, _events.Count > 0 ? _events[0].Seq : _seq + 1, SummaryLocked());
        }
    }

    public DashboardLogSummaryDto Summary() { lock (_gate) return SummaryLocked(); }

    private DashboardLogSummaryDto SummaryLocked() => new(_runPid, _runVersion, _faults, _warnings, _seq, _logPath, _log.Offset,
        _logGrewUtc is { } t ? Math.Max(0, (_clock() - t).TotalMilliseconds) : null, _crashPath);

    private List<string> ReadNew(string path, ref Tracked file, out bool restarted)
    {
        restarted = false;
        var lines = new List<string>();
        try
        {
            var info = new FileInfo(path);
            if (!info.Exists) return lines;
            if (info.CreationTimeUtc != file.Created || info.Length < file.Offset)
            {
                restarted = file.Created != default || file.Offset > 0 || path == _logPath;
                file = new Tracked { Created = info.CreationTimeUtc };
            }
            if (info.Length == file.Offset) return lines;
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            stream.Seek(file.Offset, SeekOrigin.Begin);
            var buffer = new byte[Math.Min(info.Length - file.Offset, 4 * 1024 * 1024)];
            int read = stream.Read(buffer, 0, buffer.Length);
            file.Offset += read;
            if (path == _logPath && read > 0) _logGrewUtc = _clock();
            string text = file.Partial + Encoding.UTF8.GetString(buffer, 0, read);
            int end = text.LastIndexOf('\n');
            file.Partial = end < 0 ? text : text[(end + 1)..];
            if (end >= 0) lines.AddRange(text[..end].Split('\n').Select(l => l.TrimEnd('\r')));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) {  }
        return lines;
    }

    private void NewRun()
    {
        lock (_gate) { _runPid = 0; _runVersion = null; _faults = 0; _warnings = 0; }
        _crashPath = null;
        _crash = new();
    }

    private void AddLogLine(string line)
    {
        if (line.Length == 0) return;
        if (Attached().Match(line) is { Success: true } run)
            lock (_gate) { _runPid = uint.Parse(run.Groups[1].Value); _runVersion = run.Groups[2].Value; }
        if (CrashPointer().Match(line) is { Success: true } crash) { _crashPath = crash.Groups[1].Value.Trim(); _crash = new(); }
        if (char.IsWhiteSpace(line[0])) { Continue(line); return; }
        int space = line.IndexOf(' ');
        string first = space > 0 ? line[..space] : line;
        string tag = TagWord().IsMatch(first) ? first : FrameDetail().IsMatch(line) ? "FRAME" : "-";
        Add("log", tag, Level(line), line, null);
    }

    private void AddCrashLine(string line)
    {
        if (line.StartsWith("========== FAULT:", StringComparison.Ordinal))
        {
            _crash.InFault = true;
            Add("crash", "FAULT", "fault", line.Trim('=', ' '), null);
        }
        else if (line.StartsWith("========== end of fault report", StringComparison.Ordinal)) _crash.InFault = false;
        else if (_crash.InFault) Continue(line);
    }

    private void Continue(string line)
    {
        lock (_gate)
        {
            if (_events.Count == 0) return;
            var last = _events[^1];
            string detail = last.Detail == null ? line : last.Detail + "\n" + line;
            _events[^1] = last with { Detail = detail.Length <= DetailLimit ? detail : detail[..DetailLimit] };
        }
    }

    private void Add(string source, string tag, string level, string text, string? detail)
    {
        lock (_gate)
        {
            if (level == "fault") _faults++;
            else if (level == "warning") _warnings++;
            _events.Add(new(++_seq, _clock().ToString("O"), _runPid, source, tag, level,
                text.Length <= TextLimit ? text : text[..TextLimit], detail));
            if (_events.Count > EventLimit) _events.RemoveRange(0, _events.Count - EventLimit);
        }
    }

    private static string Level(string line) =>
        FaultWords().IsMatch(line) ? "fault" : WarningWords().IsMatch(line) ? "warning" : "info";

    [GeneratedRegex(@"^u9log attached\s+pid=(\d+)\s+version\s+(\S+)")] private static partial Regex Attached();
    [GeneratedRegex(@"^CRASH diagnostics -> (.+)$")] private static partial Regex CrashPointer();
    [GeneratedRegex(@"^[A-Z][A-Z0-9_\-]{1,15}$")] private static partial Regex TagWord();
    [GeneratedRegex(@"^F \d{6} ")] private static partial Regex FrameDetail();
    [GeneratedRegex(@"\bFAULT\b|\bEXCEPTION\b|ACCESS_VIOLATION")] private static partial Regex FaultWords();
    [GeneratedRegex(@"\b(fail(ed|s|ure)?|error|refus(ed|es)|not started|unavailable|RISING|cap reached|warning)\b", RegexOptions.IgnoreCase)]
    private static partial Regex WarningWords();

    private struct Tracked
    {
        public DateTime Created;
        public long Offset;
        public string Partial;
        public bool InFault;
        public Tracked() { Created = default; Offset = 0; Partial = ""; InFault = false; }
    }
}
