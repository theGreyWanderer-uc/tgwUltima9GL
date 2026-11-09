using System.Diagnostics;
using System.Text.Json;
using U9GL.Core.Telemetry;

namespace U9GL.WebDashboard.Telemetry;

public sealed class DashboardTelemetryStore
{
    public const int SessionLimit = 3, SamplesPerSession = 600, HistorySeconds = 120, HistoryByteLimit = 2 * 1024 * 1024;
    public const int LongSamplesPerSession = 360, LongHistorySeconds = 1800, LongIntervalSeconds = 5;
    public static readonly string[] HistorySeries = ["memory.privateBytes", "memory.workingSet", "frames.drawCallsLast",
        "resources.textureBytes", "resources.texturesAlive",
        "pacing.fpsMilli", "memoryTrend.floorSlopeMilli", "textureCache.cacheBytes", "textureCache.evictionsPerSec",
        "area.meshUsers", "area.worstCell", "area.objects", "area.nonfixedUsed", "area.fixedUsed",
        "addressSpace.largestFreeBytes", "vram.localUsageBytes", "gpu.frameUs", "gpu.waterUs", "gpu.aoUs", "gpu.sunShadowsUs", "gpu.poolShadowsUs",
        "player.health", "player.mana"];
    private static readonly (string Key, TelemetryGroupSchema Group, int Field)[] Series = HistorySeries.Select(key =>
    {
        int dot = key.IndexOf('.');
        var group = TelemetrySchema.Find(key[..dot]) ?? throw new InvalidOperationException("Unknown history group: " + key);
        int field = group.IndexOf(key[(dot + 1)..]);
        return field >= 0 ? (key, group, field) : throw new InvalidOperationException("Unknown history field: " + key);
    }).ToArray();
    private static readonly TelemetryGroupSchema[] SeriesGroups = Series.Select(s => s.Group).Distinct().ToArray();
    public static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private static readonly DashboardRetentionDto Limits = new(SessionLimit, SamplesPerSession, HistorySeconds, HistoryByteLimit,
        LongSamplesPerSession, LongHistorySeconds, LongIntervalSeconds);
    private readonly object _gate = new();
    private readonly List<Session> _sessions = [];
    private readonly Func<long> _clock;
    private TelemetrySnapshot? _current;
    private string _state = "waitingForGame", _detail = "Waiting for game";
    private int _historyBytes;
    private bool _gapPending;
    private DashboardGameDto? _game;

    public DashboardTelemetryStore(Func<long>? clock = null) => _clock = clock ?? Stopwatch.GetTimestamp;

    public event Action<string, string>? StateChanged;

    public Func<DashboardLogSummaryDto>? LogSummary { get; set; }

    public void Apply(TelemetrySourceUpdate update)
    {
        string before, beforeDetail, after, afterDetail;
        lock (_gate)
        {
            (before, beforeDetail) = (_state, _detail);
            ApplyLocked(update);
            (after, afterDetail) = (_state, _detail);
        }
        if (after != before || afterDetail != beforeDetail) StateChanged?.Invoke(after, afterDetail);
    }

    private void ApplyLocked(TelemetrySourceUpdate update)
    {
        {
            if (update.Sample is { } sample)
            {
                bool same = _current is { } old && SameSession(old, sample);
                if (same && (sample.SampleIndex < _current!.SampleIndex || sample.SampleQpc < _current.SampleQpc))
                {
                    _state = "error"; _detail = "Telemetry sample index or timestamp moved backwards."; _gapPending = true;
                    return;
                }
                if (!same || sample.SampleIndex != _current!.SampleIndex)
                {
                    bool gap = _gapPending || (same && (sample.SampleIndex - _current!.SampleIndex > 1 ||
                        DashboardTelemetryProjection.Interval(sample.SampleQpc, _current.SampleQpc, sample.QpcFrequency) >= 2000 ||
                        SeriesGroups.Any(g => _current.Group(g) != null && sample.Group(g) == null)));
                    if (!same)
                    {
                        _sessions.Add(new(DashboardTelemetryProjection.Identity(sample)));
                        while (_sessions.Count > SessionLimit) RemoveSession();
                    }
                    var point = HistoryPoint(sample, gap);
                    int bytes = JsonSerializer.SerializeToUtf8Bytes(point, Json).Length;
                    var session = _sessions[^1];
                    session.Points.Add(new(point, bytes)); _historyBytes += bytes;
                    while (session.Points.Count > SamplesPerSession ||
                        point.ElapsedMs - session.Points[0].Point.ElapsedMs > HistorySeconds * 1000) RemovePoint(session);
                    session.LongGapPending |= gap;
                    if (session.LongPoints.Count == 0 || point.ElapsedMs - session.LongPoints[^1].Point.ElapsedMs >= LongIntervalSeconds * 1000)
                    {
                        var coarse = point with { GapBefore = session.LongGapPending && session.LongPoints.Count > 0 };
                        session.LongGapPending = false;
                        session.LongPoints.Add(new(coarse, bytes)); _historyBytes += bytes;
                        while (session.LongPoints.Count > LongSamplesPerSession ||
                            coarse.ElapsedMs - session.LongPoints[0].Point.ElapsedMs > LongHistorySeconds * 1000) RemoveLongPoint(session);
                    }
                    while (_historyBytes > HistoryByteLimit)
                    {
                        if (_sessions.Count > 1) RemoveSession();
                        else if (session.LongPoints.Count > session.Points.Count / 10) RemoveLongPoint(session);
                        else RemovePoint(session);
                    }
                    _gapPending = false;
                }
                _current = sample;
            }
            if (update.Game != null) _game = update.Game;
            _state = update.State;
            if (_state == "live" && _current == null) _state = "waitingForTelemetry";
            _detail = update.Detail.Length <= 256 ? update.Detail : update.Detail[..256];
            if (_state is "error" or "incompatible" or "processExited") _gapPending = true;
        }
    }

    public DashboardTelemetryView GetView()
    {
        lock (_gate)
        {
            var current = _current is { } s ? DashboardTelemetryProjection.Sample(s, checked((ulong)_clock())) : null;
            string state = _state == "live" && current?.AgeMs >= TelemetryContract.StaleMilliseconds ? "stale" : _state;
            return new(2, state, state == "stale" ? "No current publication; retaining the last accepted sample." : _detail,
                current, _sessions.Select(s => new DashboardSessionDto(s.Identity, s.Points.Select(p => p.Point).ToArray(),
                    s.LongPoints.Select(p => p.Point).ToArray())).ToArray(), Limits,
                HistorySeries, DashboardTelemetryProjection.Schema, _game, EmbeddedDashboardAssets.FrontendBuild, LogSummary?.Invoke(),
                DashboardTelemetryProjection.Arrays);
        }
    }

    private static DashboardHistoryPoint HistoryPoint(TelemetrySnapshot sample, bool gap)
    {
        var collected = new Dictionary<string, double>();
        var values = new Dictionary<string, string>();
        foreach (var group in SeriesGroups)
            if (sample.Group(group) is { } reading)
                collected[group.Name] = DashboardTelemetryProjection.Interval(reading.CollectedQpc, sample.SessionStartQpc, sample.QpcFrequency);
        foreach (var (key, group, field) in Series)
            if (sample.Group(group) is { } reading) values[key] = DashboardTelemetryProjection.Text(group.Fields[field], reading.Values[field]);
        return new(DashboardTelemetryProjection.Exact(sample.SampleIndex),
            DashboardTelemetryProjection.Interval(sample.SampleQpc, sample.SessionStartQpc, sample.QpcFrequency), gap, collected, values);
    }

    private static bool SameSession(TelemetrySnapshot a, TelemetrySnapshot b) => a.ProducerPid == b.ProducerPid &&
        a.ProcessCreatedFiletime == b.ProcessCreatedFiletime && a.SessionId == b.SessionId;
    private void RemovePoint(Session session) { _historyBytes -= session.Points[0].Bytes; session.Points.RemoveAt(0); }
    private void RemoveLongPoint(Session session) { _historyBytes -= session.LongPoints[0].Bytes; session.LongPoints.RemoveAt(0); }
    private void RemoveSession()
    {
        _historyBytes -= _sessions[0].Points.Sum(p => p.Bytes) + _sessions[0].LongPoints.Sum(p => p.Bytes);
        _sessions.RemoveAt(0);
    }
    private sealed class Session(DashboardIdentityDto identity)
    {
        internal DashboardIdentityDto Identity { get; } = identity;
        internal List<StoredPoint> Points { get; } = [];
        internal List<StoredPoint> LongPoints { get; } = [];
        internal bool LongGapPending { get; set; }
    }
    private sealed record StoredPoint(DashboardHistoryPoint Point, int Bytes);
}
