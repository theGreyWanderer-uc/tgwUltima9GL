using System.Text;
using U9GL.Core.Ini;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class IniDocumentTests
{
    private const string Fixture =
        "; Options for a test\r\n" +
        "[Sound]\r\n" +
        "SkipFrames =2\r\n" +
        "MusicVolume=64\n" +
        "[Lighting]\n" +
        "SkipFrames=4\n" +
        "[Game]\r\n" +
        "Game Rasterizer=2\r\n" +
        "MoviePath=.\\Movies\r\n" +
        "Name=café\r\n" +
        "GameScreenWidth = 640\r\n" +
        "GameScreenHeight=480";

    private static byte[] Bytes(string text) => Encoding.Latin1.GetBytes(text);

    [TestMethod]
    public void RoundTripsEveryByte()
    {
        CollectionAssert.AreEqual(Bytes(Fixture), IniDocument.Parse(Bytes(Fixture)).ToBytes());
    }

    [TestMethod]
    public void GetIsSectionAwareAndCaseInsensitive()
    {
        var ini = IniDocument.Parse(Bytes(Fixture));
        Assert.AreEqual("2", ini.Get("Sound", "SkipFrames"));
        Assert.AreEqual("4", ini.Get("Lighting", "SkipFrames"));
        Assert.AreEqual("2", ini.Get("game", "game rasterizer"));
        Assert.AreEqual("café", ini.Get("Game", "Name"));
        Assert.IsNull(ini.Get("Game", "SkipFrames"));
        Assert.IsNull(ini.Get("Video", "GammaRed"));
    }

    [TestMethod]
    public void SetChangesOnlyThatValue()
    {
        var ini = IniDocument.Parse(Bytes(Fixture));
        ini.Set("Game", "Game Rasterizer", "3");
        CollectionAssert.AreEqual(Bytes(Fixture.Replace("Game Rasterizer=2", "Game Rasterizer=3")), ini.ToBytes());
    }

    [TestMethod]
    public void SetKeepsTheSpacingAfterTheEqualsSign()
    {
        var ini = IniDocument.Parse(Bytes(Fixture));
        ini.Set("Game", "GameScreenWidth", "1920");
        StringAssert.Contains(Encoding.Latin1.GetString(ini.ToBytes()), "GameScreenWidth = 1920\r\n");
    }

    [TestMethod]
    public void SetOnTheLastLineAddsNoNewline()
    {
        var ini = IniDocument.Parse(Bytes(Fixture));
        ini.Set("Game", "GameScreenHeight", "1080");
        StringAssert.EndsWith(Encoding.Latin1.GetString(ini.ToBytes()), "\r\nGameScreenHeight=1080");
    }

    [TestMethod]
    public void SetInOneSectionLeavesTheSameKeyElsewhere()
    {
        var ini = IniDocument.Parse(Bytes(Fixture));
        ini.Set("Lighting", "SkipFrames", "9");
        Assert.AreEqual("2", ini.Get("Sound", "SkipFrames"));
        Assert.AreEqual("9", ini.Get("Lighting", "SkipFrames"));
    }

    [TestMethod]
    public void AKeyTwiceInOneSectionIsRefused()
    {
        var ini = IniDocument.Parse(Bytes("[Game]\nA=1\nA=2\n"));
        Assert.ThrowsExactly<IniException>(() => ini.Get("Game", "A"));
        Assert.ThrowsExactly<IniException>(() => ini.Set("Game", "A", "3"));
    }

    [TestMethod]
    public void AMissingKeyIsNotAdded()
    {
        var ini = IniDocument.Parse(Bytes(Fixture));
        Assert.ThrowsExactly<IniException>(() => ini.Set("Game", "NoSuchKey", "1"));
        CollectionAssert.AreEqual(Bytes(Fixture), ini.ToBytes());
    }

    [TestMethod]
    public void ValuesWithLineBreaksOrNonLatin1AreRejected()
    {
        var ini = IniDocument.Parse(Bytes(Fixture));
        Assert.ThrowsExactly<ArgumentException>(() => ini.Set("Game", "Name", "a\nb"));
        Assert.ThrowsExactly<ArgumentException>(() => ini.Set("Game", "Name", "日本"));
    }
}
