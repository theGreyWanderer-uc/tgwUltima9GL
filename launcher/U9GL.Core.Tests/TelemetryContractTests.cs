using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.Json;
using U9GL.Core.Telemetry;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class TelemetryContractTests
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private static TelemetryFixtureSuite Fixtures => JsonSerializer.Deserialize<TelemetryFixtureSuite>(
        File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Data", "telemetry-samples.json")), Json)!;

    public static IEnumerable<object[]> Samples => Fixtures.Cases.Select(c => new object[] { c.Name, c });

    private static string Camel(string snake)
    {
        var parts = snake.Split('_');
        return parts[0] + string.Concat(parts.Skip(1).Select(p => char.ToUpperInvariant(p[0]) + p[1..]));
    }

    [TestMethod]
    [DynamicData(nameof(Samples))]
    public void DecodesSharedSyntheticSamples(string name, TelemetryFixture fixture)
    {
        var result = Decode(fixture);
        Assert.AreEqual(Enum.Parse<TelemetryReadStatus>(fixture.Status), result.Status, name + ": " + result.Reason);
        if (result.Status != TelemetryReadStatus.Accepted)
        {
            Assert.IsNull(result.Snapshot, name);
            Assert.IsFalse(string.IsNullOrWhiteSpace(result.Reason), name);
            return;
        }

        var snapshot = result.Snapshot!;
        var fields = fixture.Fields!;
        ulong N(string key) => ulong.Parse(fields[key], CultureInfo.InvariantCulture);
        Assert.AreEqual((uint)N("header.producer_pid"), snapshot.ProducerPid);
        Assert.AreEqual(N("header.process_created_filetime"), snapshot.ProcessCreatedFiletime);
        Assert.AreEqual(fixture.SessionId, snapshot.SessionId);
        Assert.AreEqual(N("header.qpc_frequency"), snapshot.QpcFrequency);
        Assert.AreEqual(N("header.session_start_qpc"), snapshot.SessionStartQpc);
        Assert.AreEqual(N("header.sample_qpc"), snapshot.SampleQpc);
        Assert.AreEqual(N("header.sample_index"), snapshot.SampleIndex);
        Assert.AreEqual(N("header.sample_utc_filetime") == 0 ? (ulong?)null : N("header.sample_utc_filetime"), snapshot.SampleUtcFiletime);
        Assert.AreEqual((TelemetryProducerKind)N("header.producer_kind"), snapshot.ProducerKind);
        Assert.AreEqual((ushort)N("header.minor"), snapshot.Minor);
        Assert.AreEqual((uint)N("header.requested_groups"), snapshot.RequestedGroups);
        Assert.AreEqual(fixture.ProducerVersion, snapshot.ProducerVersion);
        Assert.AreEqual(fixture.Skipped, snapshot.SkippedGroups, name);

        var expectedGroups = Fixtures.Groups.Where(g => fields.ContainsKey(g.Name + ".collected_qpc")).ToList();
        CollectionAssert.AreEqual(expectedGroups.Select(g => g.Name).ToList(), snapshot.Groups.Select(g => g.Schema.Name).ToList(), name);
        foreach (var group in expectedGroups)
        {
            var reading = snapshot.Group(group.Name)!;
            Assert.AreEqual(N(group.Name + ".collected_qpc"), reading.CollectedQpc, name);
            Assert.AreEqual(group.Id, reading.Schema.Id);
            Assert.AreEqual(group.Version, reading.Schema.Version);
            CollectionAssert.AreEqual(group.Fields.Select(Camel).ToList(), reading.Schema.Fields.Select(f => f.Name).ToList(), group.Name);
            CollectionAssert.AreEqual(group.Fields.Select(f => N(group.Name + "." + f)).ToList(), reading.Values.ToList(), name + " " + group.Name);
        }
    }

    [TestMethod]
    public void ManagedLayoutMatchesEveryGoldenNumericOffset()
    {
        Assert.AreEqual(Fixtures.HeaderBytes, Marshal.SizeOf<TelemetryWireHeader>());
        Assert.AreEqual(Fixtures.EntryBytes, Marshal.SizeOf<TelemetryWireEntry>());
        Assert.AreEqual(TelemetryContract.MappingBytes, Fixtures.MappingBytes);
        foreach (var field in Fixtures.HeaderLayout)
        {
            string managed = string.Concat(field.Name.Split('_').Select(p => char.ToUpperInvariant(p[0]) + p[1..]));
            Assert.AreEqual(field.Offset, Marshal.OffsetOf<TelemetryWireHeader>(managed).ToInt32(), field.Name);
            Assert.AreEqual(field.Bytes, Marshal.SizeOf(typeof(TelemetryWireHeader).GetField(managed)!.FieldType), field.Name);
        }
        Assert.AreEqual(32, Marshal.OffsetOf<TelemetryWireHeader>(nameof(TelemetryWireHeader.SessionBytes0)).ToInt32());
        Assert.AreEqual(96, Marshal.OffsetOf<TelemetryWireHeader>(nameof(TelemetryWireHeader.VersionBytes0)).ToInt32());
        Assert.AreEqual(136, Marshal.OffsetOf<TelemetryWireHeader>(nameof(TelemetryWireHeader.Reserved0)).ToInt32());
        Assert.AreEqual(TelemetryContract.SequenceOffset, Marshal.OffsetOf<TelemetryWireHeader>(nameof(TelemetryWireHeader.Sequence)).ToInt32());
        Assert.AreEqual(TelemetryContract.SizeOffset, Marshal.OffsetOf<TelemetryWireHeader>(nameof(TelemetryWireHeader.SnapshotBytes)).ToInt32());
    }

    [TestMethod]
    public void ManagedSchemaMatchesTheFixtureGroups()
    {
        foreach (var group in Fixtures.Groups)
        {
            var schema = TelemetrySchema.Find(group.Id, group.Version)!;
            Assert.AreEqual(group.Name, schema.Name);
            CollectionAssert.AreEqual(group.Fields.Select(Camel).ToList(), schema.Fields.Select(f => f.Name).ToList());
            Assert.AreSame(schema, TelemetrySchema.Find(group.Name));
        }
        Assert.HasCount(Fixtures.Groups.Count, TelemetrySchema.Groups);
        Assert.AreEqual(0x7FFFEu, TelemetrySchema.AllRequestBits);
        Assert.AreEqual(1000, TelemetrySchema.Pacing.Fields[0].Scale);
        Assert.AreEqual(TelemetryFieldKind.Signed, TelemetrySchema.World.Fields[TelemetrySchema.World.IndexOf("avatarX")].Kind);
        Assert.HasCount(3, TelemetrySchema.Renderer.Fields[0].Names!);
        Assert.IsNull(TelemetrySchema.Find(1, 2));
    }

    [TestMethod]
    public void DiscoveryNamesAreIndependentOfCultureAndDifferentAcrossPidReuse()
    {
        var previous = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("ar-SA");
            Assert.AreEqual(@"Local\U9GL.Telemetry.v2.4242.0000000000000001", TelemetryContract.MappingName(4242, 1));
            Assert.AreEqual(@"Local\U9GL.TelemetryRequest.v2.4242.0000000000000001", TelemetryContract.RequestMappingName(4242, 1));
            Assert.AreEqual(@"Local\U9GL.Telemetry.v1.4242.0000000000000001", TelemetryContract.LegacyMappingName(4242, 1));
            Assert.AreNotEqual(TelemetryContract.MappingName(4242, 1), TelemetryContract.MappingName(4242, 2));
            Assert.Throws<ArgumentOutOfRangeException>(() => TelemetryContract.MappingName(0, 1));
            Assert.Throws<ArgumentOutOfRangeException>(() => TelemetryContract.RequestMappingName(1, 0));
        }
        finally { CultureInfo.CurrentCulture = previous; }
    }

    [TestMethod]
    public void ReplayExamplesIncludeGapsRestartAndIndependentCollectionAges()
    {
        var samples = Fixtures.Cases.ToDictionary(c => c.Name);
        var normal = Decode(samples["normal"]).Snapshot!;
        var next = Decode(samples["next"]).Snapshot!;
        var gap = Decode(samples["gap"]).Snapshot!;
        var restart = Decode(samples["restart"]).Snapshot!;
        Assert.AreEqual(2UL, gap.SampleIndex - next.SampleIndex);
        Assert.AreEqual(normal.ProducerPid, restart.ProducerPid);
        Assert.AreNotEqual(normal.ProcessCreatedFiletime, restart.ProcessCreatedFiletime);
        Assert.AreNotEqual(normal.SessionId, restart.SessionId);
        Assert.IsTrue(restart.SessionStartQpc > gap.SampleQpc);
        Assert.IsTrue(normal.Group(TelemetrySchema.Memory)!.CollectedQpc < normal.Group(TelemetrySchema.Frames)!.CollectedQpc);
        Assert.AreEqual(600UL, normal.Group(TelemetrySchema.Frames)!["frameNo"]);
        Assert.Throws<KeyNotFoundException>(() => normal.Group(TelemetrySchema.Frames)!["missing"]);
    }

    [TestMethod]
    public void PackedCellsAreReadUpToTheirCountAndUnpackAsDocumented()
    {
        var cells = Decode(Fixtures.Cases.Single(c => c.Name == "area-cells")).Snapshot!.Group(TelemetrySchema.AreaCells)!;
        Assert.HasCount(2, cells.Array);
        ulong w = cells.Array[0];
        Assert.AreEqual((63UL, 41UL, 212UL, 180UL, 3UL, 7UL),
            (w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFFFF, (w >> 32) & 0xFFF, (w >> 44) & 0xFFF, w >> 56));
        Assert.AreEqual(2045UL, (cells.Array[1] >> 16) & 0xFFFF);
        var parts = TelemetrySchema.AreaCells.Array!.Parts;
        Assert.AreEqual(64, parts.Sum(p => p.Bits));
    }

    private static TelemetryDecodeResult Decode(TelemetryFixture fixture) => TelemetryContract.Decode(
        Convert.FromHexString(fixture.Hex), uint.Parse(fixture.SequenceBefore, CultureInfo.InvariantCulture),
        uint.Parse(fixture.SequenceAfter, CultureInfo.InvariantCulture));
}

public sealed record TelemetryFixtureSuite(int HeaderBytes, int EntryBytes, int MappingBytes,
    List<TelemetryFixtureField> HeaderLayout, List<TelemetryFixtureGroup> Groups, List<TelemetryFixture> Cases);
public sealed record TelemetryFixtureField(string Name, int Offset, int Bytes);
public sealed record TelemetryFixtureGroup(ushort Id, string Name, ushort Version, List<string> Fields);
public sealed record TelemetryFixture(string Name, string Status, string SequenceBefore, string SequenceAfter,
    string Hex, Dictionary<string, string>? Fields, int Skipped, string SessionId, string ProducerVersion);
