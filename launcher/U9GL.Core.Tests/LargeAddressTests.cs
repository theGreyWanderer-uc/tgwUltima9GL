using U9GL.Core.Install;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class LargeAddressTests
{
    private string _root = "";
    private GameFolder _folder = null!;

    [TestInitialize]
    public void CreateFolder()
    {
        _root = Path.Combine(Path.GetTempPath(), "u9gl-core-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_root);
        _folder = new GameFolder(_root);
    }

    [TestCleanup]
    public void DeleteFolder()
    {
        try { Directory.Delete(_root, recursive: true); }
        catch (IOException) {  }
        catch (UnauthorizedAccessException) {  }
    }

    private static byte[] FakeExe()
    {
        var image = new byte[0x200];
        image[0] = (byte)'M';
        image[1] = (byte)'Z';
        BitConverter.GetBytes(0x80).CopyTo(image, 0x3C);
        image[0x80] = (byte)'P';
        image[0x81] = (byte)'E';
        BitConverter.GetBytes((ushort)0x010B).CopyTo(image, 0x80 + 4 + 18);
        for (int i = 0x100; i < image.Length; i++) image[i] = (byte)i;
        return image;
    }

    [TestMethod]
    public void TheCopyDiffersOnlyInTheFlag()
    {
        var original = FakeExe();
        var flagged = LargeAddress.Flagged(original);
        int at = 0x80 + 4 + 18;
        Assert.AreEqual((ushort)0x012B, BitConverter.ToUInt16(flagged, at));
        var differing = Enumerable.Range(0, original.Length).Where(i => original[i] != flagged[i]).ToList();
        CollectionAssert.AreEqual(new[] { at }, differing);
    }

    [TestMethod]
    public void CheckSaysWhetherTheCopyIsExactlyU9ExePlusTheBit()
    {
        File.WriteAllBytes(_folder.Exe, FakeExe());
        Assert.IsFalse(LargeAddress.Check(_folder).Exists);

        LargeAddress.EnsureCopy(_folder);
        var good = LargeAddress.Check(_folder);
        Assert.IsTrue(good.Exists);
        Assert.IsTrue(good.MatchesExe);
        Assert.AreEqual(64, good.Sha256!.Length);

        byte[] tampered = File.ReadAllBytes(LargeAddress.CopyPath(_folder));
        tampered[0x150] ^= 0xFF;
        File.WriteAllBytes(LargeAddress.CopyPath(_folder), tampered);
        Assert.IsFalse(LargeAddress.Check(_folder).MatchesExe, "one other byte changed");

        File.WriteAllBytes(LargeAddress.CopyPath(_folder), FakeExe());
        Assert.IsFalse(LargeAddress.Check(_folder).MatchesExe, "u9.exe itself, without the bit");
    }

    [TestMethod]
    public void EnsureCopyNeverTouchesU9Exe()
    {
        var original = FakeExe();
        File.WriteAllBytes(_folder.Exe, original);

        Assert.IsTrue(LargeAddress.EnsureCopy(_folder), "first call writes the copy");
        Assert.IsFalse(LargeAddress.EnsureCopy(_folder), "an up-to-date copy is not rewritten");
        CollectionAssert.AreEqual(original, File.ReadAllBytes(_folder.Exe));
        Assert.IsFalse(LargeAddress.IsSet(_folder.Exe));
        Assert.IsTrue(LargeAddress.IsSet(LargeAddress.CopyPath(_folder)));
    }

    [TestMethod]
    public void AChangedU9ExeRefreshesTheCopy()
    {
        var original = FakeExe();
        File.WriteAllBytes(_folder.Exe, original);
        LargeAddress.EnsureCopy(_folder);

        original[0x150] ^= 0xFF;
        File.WriteAllBytes(_folder.Exe, original);
        Assert.IsTrue(LargeAddress.EnsureCopy(_folder));
        CollectionAssert.AreEqual(LargeAddress.Flagged(original), File.ReadAllBytes(LargeAddress.CopyPath(_folder)));
    }

    [TestMethod]
    public void RemoveCopyLeavesU9Exe()
    {
        File.WriteAllBytes(_folder.Exe, FakeExe());
        LargeAddress.EnsureCopy(_folder);
        Assert.IsTrue(LargeAddress.RemoveCopy(_folder));
        Assert.IsFalse(LargeAddress.RemoveCopy(_folder));
        Assert.IsTrue(File.Exists(_folder.Exe));
    }

    [TestMethod]
    public void NotAPeImageIsRefused()
    {
        Assert.ThrowsExactly<InvalidDataException>(() => LargeAddress.Flagged(new byte[64]));
    }
}
