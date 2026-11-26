using System.Security.Cryptography;
using System.Text;
using U9GL.Core.Data;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class ContentCheckTests
{
    private const string PatchFolder = @"C:\Ultima\Ultima9Patches";

    private static byte[] Flx(params byte[][] entries)
    {
        int at = 0x80 + entries.Length * 8;
        byte[] header = new byte[at];
        BitConverter.TryWriteBytes(header.AsSpan(0x50), entries.Length);
        for (int i = 0; i < entries.Length; i++)
        {
            BitConverter.TryWriteBytes(header.AsSpan(0x80 + i * 8), entries[i].Length == 0 ? 0 : at);
            BitConverter.TryWriteBytes(header.AsSpan(0x84 + i * 8), entries[i].Length);
            at += entries[i].Length;
        }
        return [.. header, .. entries.SelectMany(e => e)];
    }

    private static byte[] Book(string title, string body)
    {
        byte[] t = Encoding.Latin1.GetBytes(title + "\0"), b = Encoding.Latin1.GetBytes(body);
        return [.. BitConverter.GetBytes(t.Length), .. t, .. BitConverter.GetBytes(b.Length), .. b];
    }

    private static string Row(string build, params byte[][] entries) =>
        $"static\tBOOKS-EN.FLX\t{build}\tentry\t{entries.Length}\t" +
        Convert.ToBase64String(entries.SelectMany(e => e.Length == 0 ? new byte[4] : SHA256.HashData(e)[..4]).ToArray());

    private static readonly byte[] Shop = Book("1: Shop", "Smithy\r\n!\r\n#2673 \"Short Sword\" $45\r\n");
    private static readonly byte[] Lore = Book("2: Lore", "Once upon a time.");

    [TestMethod]
    public void AnUnknownEditIsDescribedByItsClosestVersionAndKinds()
    {
        var shopPatched = Book("1: Shop", "Smithy\r\n!\r\n#2673 \"Short Sword\" $90\r\n");
        var check = ContentCheck.Parse(string.Join("\n", Row("1.19F", Shop, Lore), Row("Economy", shopPatched, Lore)));

        var diff = check.Compare("static", "BOOKS-EN.FLX", Flx(shopPatched, Book("2: Lore", "A new tale.")))!;
        Assert.AreEqual(0, diff.Exact.Count);
        CollectionAssert.AreEqual(new[] { "Economy" }, diff.Closest.ToArray());
        Assert.AreEqual(1, diff.FromClosest);
        Assert.AreEqual(1, diff.FromStock[EntryKind.ShopList]);
        Assert.AreEqual(1, diff.FromStock[EntryKind.Book]);
    }

    [TestMethod]
    public void AKnownVersionMatchesExactly()
    {
        var check = ContentCheck.Parse(Row("1.19F", Shop, Lore));
        var diff = check.Compare("static", "BOOKS-EN.FLX", Flx(Shop, Lore))!;
        Assert.IsTrue(diff.IsStock);
        Assert.AreEqual(0, diff.ChangedFromStock);
    }

    [TestMethod]
    public void TheEmbeddedFingerprintsLoad()
    {
        var check = ContentCheck.Load();
        CollectionAssert.IsSubsetOf(new[] { ("static", "text.flx"), ("static", "books-en.flx"), ("runtime", "npc.flx") },
                                    check.Files.ToArray());
    }

    private static byte[] Patch(string relative)
    {
        string path = Path.Combine(PatchFolder, relative);
        if (!File.Exists(path)) Assert.Inconclusive($"{path} is not on this machine");
        return File.ReadAllBytes(path);
    }

    [TestMethod]
    public void TheDialoguePatchIsRecognisedAndARenamedAvatarIsFound()
    {
        var check = ContentCheck.Load();
        byte[] text = Patch(@"u9diapatch150\TEXT.FLX");
        var diff = check.Compare("static", "text.flx", text)!;
        CollectionAssert.AreEqual(new[] { "DP 1.5" }, diff.Exact.ToArray());
        Assert.IsTrue(diff.FromStock[EntryKind.DialogueLine] > 4000);

        byte[] from = Encoding.Unicode.GetBytes("Grandor"), to = Encoding.Unicode.GetBytes("Kestrel");
        byte[] renamed = (byte[])text.Clone();
        for (int i = 0; i + from.Length <= renamed.Length; i++)
            if (renamed.AsSpan(i, from.Length).SequenceEqual(from)) to.CopyTo(renamed, i);
        var named = check.Compare("static", "text.flx", renamed)!;
        Assert.AreEqual(0, named.Exact.Count);
        CollectionAssert.AreEqual(new[] { "DP 1.5" }, named.Closest.ToArray());
        Assert.AreEqual(81, named.FromClosest);
        Assert.AreEqual("Kestrel", named.AvatarName);
    }

    [TestMethod]
    public void ASaveStartedBeforeTheMonsterPatchIsTold()
    {
        const string SavePath = @"C:\Ultima\Ultima9dev\savegame\u9game241.sav";
        byte[] npc = Patch(@"u9diapatch150\npc.flx");
        if (!File.Exists(SavePath)) Assert.Inconclusive($"{SavePath} is not on this machine");
        string root = Path.Combine(Path.GetTempPath(), "u9gl-npc-saves-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "runtime"));
            Directory.CreateDirectory(Path.Combine(root, "savegame"));
            File.WriteAllBytes(Path.Combine(root, "runtime", "NPC.FLX"), npc);
            byte[] save = File.ReadAllBytes(SavePath);
            File.WriteAllBytes(Path.Combine(root, "savegame", "u9game1.sav"), save);

            var changed = ContentCheck.Load().ChangedHealth(npc);
            var processes = U9GL.Core.Saves.U9SaveArchive.FromBytes(save).Processes;
            foreach (var (record, value) in changed)
                BitConverter.TryWriteBytes(save.AsSpan(processes.Offset + U9GL.Core.Saves.NpcPatchSavesCheck.TableAt
                                                       + record * 0x13C + ContentCheck.NpcBaseMaximumAt), value);
            File.WriteAllBytes(Path.Combine(root, "savegame", "u9game2.sav"), save);

            var result = U9GL.Core.Saves.NpcPatchSavesCheck.Check(Path.Combine(root, "savegame"), Path.Combine(root, "runtime"));
            Assert.AreEqual(65, result.ChangedRecords);
            Assert.AreEqual(2, result.Saves.Count);
            Assert.IsTrue(result.Saves[0].StartedBefore, "the real save predates the patch");
            Assert.IsFalse(result.Saves[1].StartedBefore, "the save given the patch's values");
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [TestMethod]
    public void TheEconomyAndMonsterPatchesAreTold()
    {
        var check = ContentCheck.Load();
        var books = check.Compare("static", "BOOKS-EN.FLX", Patch(@"u9diapatch150\book&economy"))!;
        CollectionAssert.AreEqual(new[] { "Economy" }, books.Exact.ToArray());
        Assert.IsTrue(books.FromStock[EntryKind.ShopList] >= 47, $"{books.FromStock.GetValueOrDefault(EntryKind.ShopList)} shop lists");

        var npc = check.Compare("runtime", "NPC.FLX", Patch(@"u9diapatch150\npc.flx"))!;
        CollectionAssert.AreEqual(new[] { "Monster" }, npc.Exact.ToArray());
        Assert.IsTrue(npc.FromStock[EntryKind.NpcHealth] > 0);
    }
}
