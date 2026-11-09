using System.Text;
using U9GL.Core.Data;

namespace U9GL.Core.Tests;

[TestClass]
public sealed class TypeNamesTests
{
    private static byte[] Archive(params (int id, string? name)[] entries)
    {
        int count = entries.Max(e => e.id) + 1;
        var payload = new List<byte>();
        var dir = new (int offset, int length)[count];
        int at = 0x80 + count * 8;
        foreach (var (id, name) in entries)
        {
            var entry = new List<byte>();
            entry.AddRange(BitConverter.GetBytes(0));
            entry.AddRange(BitConverter.GetBytes((ushort)7));
            if (name != null) { entry.AddRange(Encoding.Latin1.GetBytes(name)); entry.Add(0); }
            dir[id] = (at + payload.Count, entry.Count);
            payload.AddRange(entry);
        }
        var data = new byte[at + payload.Count];
        BitConverter.GetBytes(count).CopyTo(data, 0x50);
        BitConverter.GetBytes(2).CopyTo(data, 0x54);
        for (int i = 0; i < count; i++)
        {
            BitConverter.GetBytes(dir[i].offset).CopyTo(data, 0x80 + i * 8);
            BitConverter.GetBytes(dir[i].length).CopyTo(data, 0x80 + i * 8 + 4);
        }
        payload.CopyTo(data, at);
        return data;
    }

    [TestMethod]
    public void NamesComeFromTheirSlotAndUnnamedSlotsAreNull()
    {
        var names = TypeNames.Parse(Archive((1, "Arrow"), (3, null), (5, "Iolo's bow")))!;
        Assert.AreEqual(6, names.Count);
        Assert.AreEqual("Arrow", names[1]);
        Assert.AreEqual("Iolo's bow", names[5]);
        Assert.IsNull(names[3]);
        Assert.IsNull(names[2]);
        Assert.IsNull(names[99]);
    }

    [TestMethod]
    public void ATruncatedArchiveIsRefused() => Assert.IsNull(TypeNames.Parse(new byte[0x40]));
}
