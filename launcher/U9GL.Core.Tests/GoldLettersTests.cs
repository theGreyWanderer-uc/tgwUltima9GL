using U9GL.Core.Install;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class GoldLettersTests
{
    private static string TempFolder()
    {
        string dir = Path.Combine(Path.GetTempPath(), "u9gl-gold-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(dir, GameFolder.BundleFolderName));
        return dir;
    }

    [TestMethod]
    public void MissingGameDataIsReportedNotThrown()
    {
        string root = TempFolder();
        var actions = new List<string>();
        GoldLetters.EnsureFiles(new GameFolder(root), actions);
        Assert.IsFalse(File.Exists(Path.Combine(root, GameFolder.BundleFolderName, GoldLetters.KFile)));
        Assert.IsTrue(actions.Single().StartsWith("could not make the gold letters"), actions.Single());
    }

    [TestMethod]
    public void ExistingLettersAreKept()
    {
        string root = TempFolder(), bundle = Path.Combine(root, GameFolder.BundleFolderName);
        File.WriteAllText(Path.Combine(bundle, GoldLetters.KFile), "mine");
        File.WriteAllText(Path.Combine(bundle, GoldLetters.MFile), "mine");
        var actions = new List<string>();
        GoldLetters.EnsureFiles(new GameFolder(root), actions);
        Assert.AreEqual("mine", File.ReadAllText(Path.Combine(bundle, GoldLetters.KFile)));
        StringAssert.StartsWith(actions.Single(), "kept the gold letters");
    }

    [TestMethod]
    public void MakesBothLettersFromARealInstall()
    {
        string game = Environment.GetEnvironmentVariable("U9_GAME_DIR") ?? @"C:\Ultima\Ultima9";
        string flx = Path.Combine(game, "static", "bitmap16.flx");
        if (!File.Exists(flx)) Assert.Inconclusive("No game install on this machine.");

        var (k, m) = GoldLetters.Make(flx);
        foreach (var (bmp, name) in new[] { (k, "K"), (m, "M") })
        {
            Assert.AreEqual((byte)'B', bmp[0]);
            Assert.AreEqual(14, BitConverter.ToInt32(bmp, 22), $"{name} height");
            Assert.AreEqual(24, BitConverter.ToInt16(bmp, 28), $"{name} depth");
            int width = BitConverter.ToInt32(bmp, 18);
            Assert.IsTrue(width is >= 8 and <= 20, $"{name} width {width}");
        }
        string outDir = Path.Combine(Path.GetTempPath(), "u9gl-gold-letters");
        Directory.CreateDirectory(outDir);
        File.WriteAllBytes(Path.Combine(outDir, GoldLetters.KFile), k);
        File.WriteAllBytes(Path.Combine(outDir, GoldLetters.MFile), m);
    }
}
