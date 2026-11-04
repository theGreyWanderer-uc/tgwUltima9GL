using U9GL.Core.Ini;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class U9glIniTests
{
    private const string DefaultsV1 = "# u9gl.ini defaults 0.1.0\n# a documented default\ngl_window_size = 0\nmetrics_overlay = 1\n";
    private const string DefaultsV2 = "# u9gl.ini defaults 0.2.0\ngl_window_size = 0\nnew_key = 5\n";

    private static KeyValuePair<string, string> Kv(string key, string value) => new(key, value);

    [TestMethod]
    public void TheLastLineForAKeyWins()
    {
        var effective = U9glIni.Effective("a = 1\r\n# comment\n; also a comment\nA=2\nb=\n");
        Assert.AreEqual("2", effective["a"]);
        Assert.AreEqual("", effective["b"]);
    }

    [TestMethod]
    public void CommentOutTurnsOnlyThoseKeysActiveLinesIntoComments()
    {
        const string text = "# header\r\ndiag_bird_watch = 2\r\n# diag_door_watch = 1\nfix_tele_watch=1\nstream_actors = 300";
        string edited = U9glIni.CommentOut(text, ["diag_bird_watch", "diag_door_watch", "diag_tele_watch"], "note", out var commented);

        Assert.AreEqual("# header\r\n# diag_bird_watch = 2   (note)\r\n# diag_door_watch = 1\n# fix_tele_watch=1   (note)\nstream_actors = 300", edited);
        CollectionAssert.AreEqual(new[] { "diag_bird_watch", "diag_tele_watch" }, commented);
        Assert.IsFalse(U9glIni.Effective(edited).ContainsKey("diag_bird_watch"));
        Assert.AreEqual("300", U9glIni.Effective(edited)["stream_actors"]);
    }

    [TestMethod]
    public void RenamedDiagnosticsAreReadAndWrittenUnderTheirNewNames()
    {
        string old = U9glIni.Compose(DefaultsV1, [Kv("gl_window_size", "1")], "0.1.0") + "fix_tele_watch = 1\nfix_census = 0\n";

        Assert.AreEqual("1", U9glIni.Effective(old)["diag_tele_watch"]);
        Assert.IsFalse(U9glIni.Effective(old).ContainsKey("fix_tele_watch"));

        string rewritten = U9glIni.Compose(DefaultsV1, [.. U9glIni.ReadBlock(old), Kv("diag_census", "1")], "0.1.0");
        CollectionAssert.AreEqual(new[] { Kv("gl_window_size", "1"), Kv("diag_tele_watch", "1"), Kv("diag_census", "1") },
            U9glIni.ReadBlock(rewritten).ToArray(), "the old names are gone, and a later new-name write wins");
        Assert.IsFalse(rewritten.Contains("fix_census", StringComparison.Ordinal));
    }

    [TestMethod]
    public void ComposePutsTheBlockAfterTheDefaultsAndItWins()
    {
        string text = U9glIni.Compose(DefaultsV1,
            [Kv("gl_window_size", "1"), Kv("rasterizer5", "1"), Kv("gl_window_size", "2")], "0.1.0");

        StringAssert.StartsWith(text, DefaultsV1);
        CollectionAssert.AreEqual(new[] { Kv("gl_window_size", "2"), Kv("rasterizer5", "1") }, U9glIni.ReadBlock(text).ToArray());
        Assert.AreEqual("2", U9glIni.Effective(text)["gl_window_size"]);
        Assert.AreEqual("1", U9glIni.Effective(text)["metrics_overlay"]);
    }

    [TestMethod]
    public void RegeneratingFromNewDefaultsKeepsTheBlock()
    {
        string v1 = U9glIni.Compose(DefaultsV1, [Kv("gl_window_size", "2"), Kv("player_added", "7")], "0.1.0");
        string v2 = U9glIni.Compose(DefaultsV2, U9glIni.ReadBlock(v1), "0.2.0");

        StringAssert.StartsWith(v2, DefaultsV2);
        Assert.AreEqual(1, v2.Split('\n').Count(l => l.StartsWith(U9glIni.BlockMarker, StringComparison.Ordinal)));
        Assert.AreEqual("2", U9glIni.Effective(v2)["gl_window_size"]);
        Assert.AreEqual("7", U9glIni.Effective(v2)["player_added"]);
        Assert.AreEqual("5", U9glIni.Effective(v2)["new_key"]);
    }

    [TestMethod]
    public void AFileWithABlockPassedAsDefaultsDoesNotGetTwoBlocks()
    {
        string once = U9glIni.Compose(DefaultsV1, [Kv("a", "1")], "0.1.0");
        string twice = U9glIni.Compose(once, [Kv("b", "2")], "0.1.0");
        Assert.AreEqual(1, twice.Split('\n').Count(l => l.StartsWith(U9glIni.BlockMarker, StringComparison.Ordinal)));
        Assert.IsFalse(U9glIni.Effective(twice).ContainsKey("a"));
    }

    [TestMethod]
    public void DefaultsWithoutAFinalNewlineStillEndBeforeTheBlock()
    {
        string text = U9glIni.Compose("x = 1", [Kv("y", "2")], "0.1.0");
        Assert.AreEqual("1", U9glIni.Effective(text)["x"]);
        Assert.AreEqual("2", U9glIni.Effective(text)["y"]);
    }

    [TestMethod]
    public void TheDefaultsVersionLineIsRead()
    {
        Assert.AreEqual("0.1.0", U9glIni.DefaultsVersion(DefaultsV1));
        Assert.IsNull(U9glIni.DefaultsVersion("gl_window_size = 0\n"));
    }

    [TestMethod]
    public void BadKeysAndValuesAreRejected()
    {
        Assert.ThrowsExactly<ArgumentException>(() => U9glIni.Compose(DefaultsV1, [Kv("Bad Key", "1")], "0.1.0"));
        Assert.ThrowsExactly<ArgumentException>(() => U9glIni.Compose(DefaultsV1, [Kv("good_key", "1\n2")], "0.1.0"));
    }
}
