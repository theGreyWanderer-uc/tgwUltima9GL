using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;

namespace U9GL.Core.Install;

public sealed record ExeSection(string Name, uint Va, uint VirtualSize, long RawOffset, long RawSize);

public sealed record ExePlace(string Name, uint Va, uint End, bool CheckedByU9gl, bool ValuesInCode = false);

public sealed record ExeChange(string Part, long FileStart, long FileEnd, uint? VaStart, uint? VaEnd,
                               IReadOnlyList<ExePlace> Places, bool IsCode, bool IsValuesInCode = false);

public sealed record ExeComparison(long StockSize, long Size, IReadOnlyList<ExeChange> Changes)
{
    public bool Identical => Changes.Count == 0 && Size == StockSize;
    public long Added => Math.Max(0, Size - StockSize);
}

public sealed class ExeChanges
{
    public const int BlockSize = 512;

    public static IReadOnlyList<ExePlace> Places { get; } =
    [
        new("the armour table", 0x00780C74, 0x007810F4, false),
        new("the gold pile types", 0x007885E0, 0x00788628, false),
        new("the weapon grip table", 0x0079EB38, 0x0079EF30, false),
        new("the weapon table (damage, reach, shields)", 0x0079F268, 0x0079F818, false),
        new("the bloody weapon table", 0x0079F818, 0x0079F984, false),
        new("the clean-to-bloody weapon pairs", 0x0079F988, 0x0079F9BC, false),
        new("the bow frames", 0x0079F9C0, 0x0079F9FC, false),
        new("the damage type table (damage kind, blocking, hit effects)", 0x0079FA00, 0x0079FE28, false),
        new("the weapon trail table", 0x0079FE28, 0x007A0E68, false),
        new("the creature resistance values", 0x0044A040, 0x0044B504, false, true),
        new("the all-10.0 resistance row (the Crusty, flipped)", 0x00449D30, 0x00449D9F, false, true),
        new("the magic-only resistance row (the Slasher)", 0x00449E10, 0x00449E7F, false, true),
        new("the creature attack multipliers (unused in 1.19F)", 0x004143F0, 0x00415A41, false, true),
        new("the Glide renderer's vtable", 0x0074FD10, 0x0074FDEC, true),
        new("Actor_NeedsRebuild", 0x004163F0, 0x004163F3, true),
        new("Actor_ResetTransientState", 0x0043CB5C, 0x0043CB62, true),
        new("Actor_Tick_PruneRefs", 0x004DD0F0, 0x004DD0F6, true),
        new("Actor_Create", 0x004F6250, 0x004F6251, true),
        new("SpellTeleport_Tick", 0x00649747, 0x00649763, true),
    ];

    private readonly string[] _blocks;

    private ExeChanges(long size, IReadOnlyList<ExeSection> sections, string[] blocks)
    {
        StockSize = size;
        Sections = sections;
        _blocks = blocks;
    }

    public long StockSize { get; }
    public IReadOnlyList<ExeSection> Sections { get; }

    public static ExeChanges Load()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("U9GL.Core.Data.u9-exe-blocks.tsv")
            ?? throw new InvalidDataException("u9-exe-blocks.tsv is not embedded");
        using var reader = new StreamReader(stream);
        return Parse(reader.ReadToEnd());
    }

    public static ExeChanges Parse(string text)
    {
        long size = 0;
        var sections = new List<ExeSection>();
        var blocks = new List<string>();
        foreach (string raw in text.Split('\n'))
        {
            string line = raw.TrimEnd('\r');
            if (line.Length == 0 || line.StartsWith("# ", StringComparison.Ordinal)) continue;
            var cells = line.Split('\t');
            switch (cells[0])
            {
                case "#size": size = long.Parse(cells[1], CultureInfo.InvariantCulture); break;
                case "#block":
                    if (cells[1] != BlockSize.ToString(CultureInfo.InvariantCulture)) throw new InvalidDataException("unexpected block size");
                    break;
                case "#section":
                    sections.Add(new ExeSection(cells[1], Hex(cells[2]), Hex(cells[3]), Hex(cells[4]), Hex(cells[5])));
                    break;
                default: blocks.Add(line.ToLowerInvariant()); break;
            }
        }
        if (blocks.Count != (size + BlockSize - 1) / BlockSize) throw new InvalidDataException("block count does not match the size");
        return new ExeChanges(size, sections, blocks.ToArray());
    }

    private static uint Hex(string s) => uint.Parse(s.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? s[2..] : s, NumberStyles.HexNumber, CultureInfo.InvariantCulture);

    public static string BlockHash(ReadOnlySpan<byte> block) => Convert.ToHexStringLower(SHA256.HashData(block))[..16];

    public ExeComparison Compare(string exePath) => Compare(File.ReadAllBytes(exePath));

    public ExeComparison Compare(byte[] data)
    {
        var changed = new List<long>();
        long common = Math.Min(data.LongLength, StockSize);
        for (long at = 0; at < common; at += BlockSize)
        {
            int length = (int)Math.Min(BlockSize, data.LongLength - at);
            if (BlockHash(data.AsSpan((int)at, length)) != _blocks[at / BlockSize]) changed.Add(at);
        }

        var changes = new List<ExeChange>();
        int i = 0;
        while (i < changed.Count)
        {
            long start = changed[i];
            string part = PartOf(start);
            int j = i;
            while (j + 1 < changed.Count && changed[j + 1] == changed[j] + BlockSize && PartOf(changed[j + 1]) == part) j++;
            long end = Math.Min(changed[j] + BlockSize, common);
            changes.Add(Describe(part, start, end));
            i = j + 1;
        }
        return new ExeComparison(StockSize, data.LongLength, changes);
    }

    private string PartOf(long offset)
    {
        var section = Sections.FirstOrDefault(s => offset >= s.RawOffset && offset < s.RawOffset + s.RawSize);
        if (section != null) return section.Name;
        return offset < Sections.Min(s => s.RawOffset) ? "header" : "after the sections";
    }

    private ExeChange Describe(string part, long start, long end)
    {
        var section = Sections.FirstOrDefault(s => s.Name == part);
        uint? vaStart = null, vaEnd = null;
        var places = new List<ExePlace>();
        bool valuesInCode = false;
        if (section != null)
        {
            vaStart = (uint)(section.Va + (start - section.RawOffset));
            vaEnd = (uint)(section.Va + (end - section.RawOffset));
            places.AddRange(Places.Where(p => p.Va < vaEnd && p.End > vaStart));
            valuesInCode = part == ".text" && places.Count > 0 && !places.Any(p => p.CheckedByU9gl)
                && EveryBlockHoldsValues(section, start, end);
        }
        return new ExeChange(part, start, end, vaStart, vaEnd, places, part == ".text", valuesInCode);
    }

    private static bool EveryBlockHoldsValues(ExeSection section, long start, long end)
    {
        for (long at = start; at < end; at += BlockSize)
        {
            uint blockVa = (uint)(section.Va + (at - section.RawOffset));
            uint blockEnd = blockVa + BlockSize;
            if (!Places.Any(p => p.ValuesInCode && p.Va < blockEnd && p.End > blockVa)) return false;
        }
        return true;
    }
}
