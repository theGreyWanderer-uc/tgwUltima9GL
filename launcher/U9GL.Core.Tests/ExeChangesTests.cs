using System.Text;
using U9GL.Core.Install;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class ExeChangesTests
{
    private const string StockExe = @"C:\Ultima\Ultima9\u9.exe";

    private static (ExeChanges Map, byte[] Stock) Synthetic()
    {
        var stock = new byte[6 * ExeChanges.BlockSize];
        for (int i = 0; i < stock.Length; i++) stock[i] = (byte)(i * 7);
        var text = new StringBuilder("# test\n#size\t3072\n#block\t512\n");
        text.Append("#section\t.text\t0x00401000\t0x400\t0x200\t0x400\n");
        text.Append("#section\t.data\t0x0077F000\t0x600\t0x600\t0x600\n");
        for (int at = 0; at < stock.Length; at += ExeChanges.BlockSize)
            text.Append(ExeChanges.BlockHash(stock.AsSpan(at, ExeChanges.BlockSize))).Append('\n');
        return (ExeChanges.Parse(text.ToString()), stock);
    }

    [TestMethod]
    public void TheStockFileIsIdentical()
    {
        var (map, stock) = Synthetic();
        Assert.IsTrue(map.Compare(stock).Identical);
    }

    [TestMethod]
    public void AdjacentBlocksInOneSectionAreOneChange()
    {
        var (map, stock) = Synthetic();
        var data = (byte[])stock.Clone();
        data[0x600 + 10] ^= 1;
        data[0x800 + 10] ^= 1;
        data[0x200] ^= 1;
        var result = map.Compare(data);
        Assert.AreEqual(2, result.Changes.Count);
        Assert.AreEqual(".text", result.Changes[0].Part);
        Assert.IsTrue(result.Changes[0].IsCode);
        Assert.AreEqual(".data", result.Changes[1].Part);
        Assert.AreEqual(0x600L, result.Changes[1].FileStart);
        Assert.AreEqual(0xA00L, result.Changes[1].FileEnd);
        Assert.AreEqual(0x0077F000u, result.Changes[1].VaStart);
        Assert.AreEqual(0x0077F400u, result.Changes[1].VaEnd);
    }

    [TestMethod]
    public void HeaderAndAddedBytes()
    {
        var (map, stock) = Synthetic();
        var data = stock.Concat(new byte[100]).ToArray();
        data[0x3C] ^= 1;
        var result = map.Compare(data);
        Assert.AreEqual("header", result.Changes.Single().Part);
        Assert.AreEqual(100L, result.Added);
        Assert.IsFalse(result.Identical);
    }

    [TestMethod]
    public void TheInstalledExe()
    {
        if (!File.Exists(StockExe) || BuildIdentity.Identify(StockExe).Source != "GOG") Assert.Inconclusive("no GOG 1.19F here");
        var map = ExeChanges.Load();
        var stock = File.ReadAllBytes(StockExe);
        Assert.IsTrue(map.Compare(stock).Identical);

        var data = (byte[])stock.Clone();
        var section = map.Sections.Single(s => s.Name == ".data");
        long armour = section.RawOffset + (0x00780C80 - section.Va);
        data[armour] ^= 0x01;
        var change = map.Compare(data).Changes.Single();
        Assert.AreEqual(".data", change.Part);
        Assert.IsFalse(change.IsCode);
        CollectionAssert.Contains(change.Places.Select(p => p.Name).ToList(), "the armour table");
    }

    [TestMethod]
    public void EachTableIsNamed()
    {
        if (!File.Exists(StockExe) || BuildIdentity.Identify(StockExe).Source != "GOG") Assert.Inconclusive("no GOG 1.19F here");
        var map = ExeChanges.Load();
        var stock = File.ReadAllBytes(StockExe);
        foreach (var place in ExeChanges.Places.Where(p => !p.CheckedByU9gl))
        {
            var section = map.Sections.Single(s => place.Va >= s.Va && place.Va < s.Va + s.VirtualSize);
            var data = (byte[])stock.Clone();
            uint mid = place.Va + (place.End - place.Va) / 2;
            data[section.RawOffset + (mid - section.Va)] ^= 0x01;
            var change = map.Compare(data).Changes.Single();
            CollectionAssert.Contains(change.Places.Select(p => p.Name).ToList(), place.Name, place.Name);
            Assert.AreEqual(place.ValuesInCode, change.IsCode, place.Name);
            Assert.AreEqual(place.ValuesInCode, change.IsValuesInCode, place.Name);
        }
    }

    [TestMethod]
    public void AResistanceEditIsValuesNotCode()
    {
        if (!File.Exists(StockExe) || BuildIdentity.Identify(StockExe).Source != "GOG") Assert.Inconclusive("no GOG 1.19F here");
        var map = ExeChanges.Load();
        var data = File.ReadAllBytes(StockExe);
        var text = map.Sections.Single(s => s.Name == ".text");
        long imm = text.RawOffset + (0x0044A040 + 6 - text.Va);
        CollectionAssert.AreEqual(new byte[] { 0x00, 0x00, 0x80, 0x3F }, data.AsSpan((int)imm, 4).ToArray());
        data[imm + 3] = 0x3F; data[imm + 2] = 0x00;
        var change = map.Compare(data).Changes.Single();
        Assert.IsTrue(change.IsCode);
        Assert.IsTrue(change.IsValuesInCode);
        CollectionAssert.Contains(change.Places.Select(p => p.Name).ToList(), "the creature resistance values");
    }

    [TestMethod]
    public void OrdinaryCodeStaysCode()
    {
        if (!File.Exists(StockExe) || BuildIdentity.Identify(StockExe).Source != "GOG") Assert.Inconclusive("no GOG 1.19F here");
        var map = ExeChanges.Load();
        var data = File.ReadAllBytes(StockExe);
        var text = map.Sections.Single(s => s.Name == ".text");
        data[text.RawOffset + (0x004F79D0 - text.Va)] ^= 0x01;
        var change = map.Compare(data).Changes.Single();
        Assert.IsTrue(change.IsCode);
        Assert.IsFalse(change.IsValuesInCode);
    }
}
