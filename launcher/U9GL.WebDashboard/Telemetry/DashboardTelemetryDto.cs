using System.Globalization;
using U9GL.Core.Telemetry;

namespace U9GL.WebDashboard.Telemetry;

public sealed record DashboardTelemetryView(int Version, string State, string Detail,
    DashboardSampleDto? Current, DashboardSessionDto[] Sessions, DashboardRetentionDto Limits, string[] HistorySeries,
    IReadOnlyDictionary<string, DashboardFieldDto[]> Schema, DashboardGameDto? Game, string Build, DashboardLogSummaryDto? Log,
    IReadOnlyDictionary<string, DashboardArrayDto> Arrays);
public sealed record DashboardArrayDto(string Name, string CountField, int Capacity, DashboardPackedPartDto[] Parts);
public sealed record DashboardPackedPartDto(string Name, int Bits);
public sealed record DashboardGameDto(string Executable, bool LargeAddressAware, string AddressSpaceBytes);
public sealed record DashboardFieldDto(string Name, string Kind, int Scale, string Unit, string[]? Names);
public sealed record DashboardRetentionDto(int SessionLimit, int SamplesPerSession, int HistorySeconds, int HistoryByteLimit,
    int LongSamplesPerSession, int LongHistorySeconds, int LongIntervalSeconds);
public sealed record DashboardIdentityDto(uint Pid, string ProcessCreatedFiletime, string SessionId, string ProducerKind, string ProducerVersion);
public sealed record DashboardGroupDto(string CollectedQpc, double ElapsedMs, double AgeMs, string Quality, int StaleMs,
    IReadOnlyDictionary<string, string> Values, string[]? Array = null);
public sealed record DashboardSampleDto(DashboardIdentityDto Identity, string QpcFrequency, string SessionStartQpc, string SampleIndex, string SampleQpc,
    string? SampleUtcFiletime, double ElapsedMs, double AgeMs, int ContractMinor, string[] RequestedGroups, int SkippedGroups,
    IReadOnlyDictionary<string, DashboardGroupDto> Groups, IReadOnlyDictionary<string, string> Labels);
public sealed record DashboardHistoryPoint(string SampleIndex, double ElapsedMs, bool GapBefore,
    IReadOnlyDictionary<string, double> CollectedMs, IReadOnlyDictionary<string, string> Values);
public sealed record DashboardSessionDto(DashboardIdentityDto Identity, DashboardHistoryPoint[] History, DashboardHistoryPoint[] LongHistory);

internal static class DashboardTelemetryProjection
{
    internal static string Exact(ulong value) => value.ToString(CultureInfo.InvariantCulture);
    internal static string Text(TelemetryField field, ulong value) => field.Kind == TelemetryFieldKind.Signed
        ? unchecked((long)value).ToString(CultureInfo.InvariantCulture) : Exact(value);

    internal static readonly IReadOnlyDictionary<string, DashboardFieldDto[]> Schema = TelemetrySchema.Groups.ToDictionary(
        g => g.Name, g => g.Fields.Select(f => new DashboardFieldDto(f.Name, f.Kind.ToString().ToLowerInvariant(), f.Scale, f.Unit,
            f.Names?.ToArray())).ToArray());

    internal static readonly IReadOnlyDictionary<string, DashboardArrayDto> Arrays = TelemetrySchema.Groups
        .Where(g => g.Array != null).ToDictionary(g => g.Name, g => new DashboardArrayDto(g.Array!.Name, g.Array.CountField,
            g.Array.Capacity, g.Array.Parts.Select(p => new DashboardPackedPartDto(p.Name, p.Bits)).ToArray()));
    internal static double Interval(ulong later, ulong earlier, ulong frequency) =>
        Math.Min(9_007_199_254_740_991d, (later >= earlier ? later - earlier : 0) * (1000d / frequency));

    internal static DashboardIdentityDto Identity(TelemetrySnapshot s) => new(s.ProducerPid,
        Exact(s.ProcessCreatedFiletime), s.SessionId, s.ProducerKind.ToString(), s.ProducerVersion);

    internal static DashboardSampleDto Sample(TelemetrySnapshot s, ulong now)
    {
        var groups = new Dictionary<string, DashboardGroupDto>();
        foreach (var group in s.Groups)
        {
            double age = Interval(now, group.CollectedQpc, s.QpcFrequency);
            var values = new Dictionary<string, string>();
            for (int i = 0; i < group.Values.Count; i++) values[group.Schema.Fields[i].Name] = Text(group.Schema.Fields[i], group.Values[i]);
            groups[group.Schema.Name] = new(Exact(group.CollectedQpc), Interval(group.CollectedQpc, s.SessionStartQpc, s.QpcFrequency),
                age, age >= group.Schema.StaleMilliseconds ? "stale" : "ready", group.Schema.StaleMilliseconds, values,
                group.Schema.Array != null ? group.Array.Select(Exact).ToArray() : null);
        }
        return new(Identity(s), Exact(s.QpcFrequency), Exact(s.SessionStartQpc), Exact(s.SampleIndex), Exact(s.SampleQpc),
            s.SampleUtcFiletime is { } utc ? Exact(utc) : null,
            Interval(s.SampleQpc, s.SessionStartQpc, s.QpcFrequency), Interval(now, s.SampleQpc, s.QpcFrequency),
            s.Minor, TelemetrySchema.Groups.Where(g => (s.RequestedGroups & g.RequestBit) != 0).Select(g => g.Name).ToArray(),
            s.SkippedGroups, groups, Labels(s));
    }

    private static Dictionary<string, string> Labels(TelemetrySnapshot s)
    {
        var labels = new Dictionary<string, string>();
        if (s.Group(TelemetrySchema.World) is { } world && world["mapValid"] != 0 && world["map"] <= int.MaxValue)
            labels["world.map"] = U9GL.Core.Saves.SaveWording.Shared.Map((int)world["map"]).DisplayName;
        return labels;
    }
}
