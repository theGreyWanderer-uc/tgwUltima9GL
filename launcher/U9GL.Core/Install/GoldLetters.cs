using System.Text;

namespace U9GL.Core.Install;

public static class GoldLetters
{
    public const string KFile = "gold_K.bmp";
    public const string MFile = "gold_M.bmp";

    private const int FontEntry = 515, KFrame = 43, MFrame = 45, DigitEntry = 614;
    private const int Height = 14, BodyRows = Height - 2, Slant = 2;
    private const double Squeeze = 0.60;
    private static readonly Rgb Light = new(205, 145, 105);

    private readonly record struct Rgb(int R, int G, int B)
    {
        public double Lum => 0.299 * R + 0.587 * G + 0.114 * B;
    }

    private sealed record Frame(int Width, int Height, Rgb?[] Pixels)
    {
        public Rgb? At(int x, int y) => Pixels[y * Width + x];
    }

    public static void EnsureFiles(GameFolder folder, List<string> actions)
    {
        string k = Path.Combine(folder.Bundle, KFile), m = Path.Combine(folder.Bundle, MFile);
        if (File.Exists(k) && File.Exists(m))
        {
            actions.Add($"kept the gold letters already in the u9gl folder ({KFile}, {MFile})");
            return;
        }
        try
        {
            string flx = Path.Combine(folder.Root, "static", "bitmap16.flx");
            var (kBmp, mBmp) = Make(flx);
            if (!File.Exists(k)) SafeFile.WriteAllBytes(k, kBmp);
            if (!File.Exists(m)) SafeFile.WriteAllBytes(m, mBmp);
            actions.Add($"made the backpack's gold letters ({KFile}, {MFile}) from the game's own font");
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            actions.Add($"could not make the gold letters ({e.Message}); the backpack shows thousands without a K");
        }
    }

    public static (byte[] K, byte[] M) Make(string bitmap16Path)
    {
        using var file = File.OpenRead(bitmap16Path);
        var digits = Enumerable.Range(0, 10).Select(i => ReadFrame(file, DigitEntry, i)).ToList();
        var palette = Palette.From(digits);
        return (ToBmp(Glyph(ReadFrame(file, FontEntry, KFrame), palette)),
                ToBmp(Glyph(ReadFrame(file, FontEntry, MFrame), palette)));
    }

    private static Frame ReadFrame(FileStream file, int entry, int index)
    {
        uint count = U32(ReadAt(file, 0x50, 4), 0);
        if (entry >= count) throw new InvalidDataException($"bitmap16.flx has no entry {entry}");
        var slot = ReadAt(file, 0x80 + entry * 8L, 8);
        uint offset = U32(slot, 0), length = U32(slot, 4);
        if (length < 0x10) throw new InvalidDataException($"bitmap16.flx entry {entry} is empty");
        var set = ReadAt(file, offset, (int)length);
        uint frames = U32(set, 8);
        if (index >= frames) throw new InvalidDataException($"entry {entry} has no frame {index}");
        int at = (int)U32(set, 0x10 + index * 8);
        uint flags = U32(set, at);
        int w = (int)U32(set, at + 4), h = (int)U32(set, at + 8);
        if ((flags & 0x400) == 0 || ((flags >> 8) & 3) != 1 || w <= 0 || h <= 0 || w > 256 || h > 256)
            throw new InvalidDataException($"entry {entry} frame {index} is not a 16-bit ARGB1555 frame");
        int px = at + 0x14 + 4 * h;
        if (px + w * h * 2 > set.Length) throw new InvalidDataException($"entry {entry} frame {index} is cut short");
        var pixels = new Rgb?[w * h];
        for (int i = 0; i < w * h; i++)
        {
            int v = set[px + 2 * i] | set[px + 2 * i + 1] << 8;
            if ((v & 0x8000) == 0) continue;
            pixels[i] = new Rgb(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31);
        }
        return new Frame(w, h, pixels);
    }

    private static byte[] ReadAt(FileStream file, long offset, int count)
    {
        var buf = new byte[count];
        file.Position = offset;
        file.ReadExactly(buf);
        return buf;
    }

    private static uint U32(byte[] b, int i) => (uint)(b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24);

    private static int Round(double v) => (int)Math.Round(v, MidpointRounding.ToEven);

    private static Rgb Mean(IReadOnlyList<Rgb> cs) =>
        new(Round(cs.Average(c => (double)c.R)), Round(cs.Average(c => (double)c.G)), Round(cs.Average(c => (double)c.B)));

    private static Rgb Mix(Rgb a, Rgb b, double t) =>
        new(Round(a.R + (b.R - a.R) * t), Round(a.G + (b.G - a.G) * t), Round(a.B + (b.B - a.B) * t));

    private sealed class Palette
    {
        public required Rgb Edge { get; init; }
        public required SortedDictionary<int, Rgb> Outer { get; init; }
        public required SortedDictionary<int, Rgb> Inner { get; init; }

        public static Palette From(IReadOnlyList<Frame> digits)
        {
            var rows = new Dictionary<int, List<Rgb>>();
            var rowOrder = new List<int>();
            var edges = new List<Rgb>();
            foreach (var f in digits)
                for (int y = 0; y < f.Height; y++)
                    for (int x = 0; x < f.Width; x++)
                    {
                        if (f.At(x, y) is not { } c) continue;
                        bool edge = Neighbours(x, y).Any(n => n.X < 0 || n.X >= f.Width || n.Y < 0 || n.Y >= f.Height || f.At(n.X, n.Y) == null);
                        if (edge) { edges.Add(c); continue; }
                        if (!rows.TryGetValue(y, out var list)) { rows[y] = list = []; rowOrder.Add(y); }
                        list.Add(c);
                    }
            var all = rowOrder.SelectMany(y => rows[y]).OrderBy(c => c.Lum).ToList();
            var highlight = Mean(all.Skip(all.Count * 85 / 100).ToList());
            var sortedEdges = edges.OrderBy(c => c.Lum).ToList();
            int n = sortedEdges.Count;
            var edgeColour = Mean(sortedEdges.Skip(n / 5).Take(n * 3 / 5 - n / 5).ToList());
            var outer = new SortedDictionary<int, Rgb>();
            var inner = new SortedDictionary<int, Rgb>();
            foreach (int y in rowOrder)
            {
                var mean = Mean(rows[y]);
                var body = Mix(mean, highlight, 0.45);
                outer[y] = Mix(body, edgeColour, 0.12);
                inner[y] = Mix(Mix(mean, highlight, 0.80), Mix(body, Light, 0.55), 0.5);
            }
            return new Palette { Edge = edgeColour, Outer = outer, Inner = inner };
        }

        public static Rgb Row(SortedDictionary<int, Rgb> table, int y) =>
            table.TryGetValue(y, out var c) ? c : table.MinBy(kv => Math.Abs(kv.Key - y)).Value;
    }

    private static IEnumerable<(int X, int Y)> Neighbours(int x, int y) =>
        [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)];

    private static Frame Glyph(Frame font, Palette pal)
    {
        int x0 = int.MaxValue, y0 = int.MaxValue, x1 = -1, y1 = -1;
        bool Ink(int x, int y) => font.At(x, y) is { } c && c.Lum < 100;
        for (int y = 0; y < font.Height; y++)
            for (int x = 0; x < font.Width; x++)
                if (Ink(x, y)) { x0 = Math.Min(x0, x); y0 = Math.Min(y0, y); x1 = Math.Max(x1, x); y1 = Math.Max(y1, y); }
        if (x1 < 0) throw new InvalidDataException("the font letter has no ink");
        int mw = x1 - x0 + 1, mh = y1 - y0 + 1;

        int bw = Math.Max(3, Round(mw * (double)BodyRows / mh * Squeeze));
        int bigW = mw * 8, bigH = mh * 8;
        var big = new int[bigW * bigH];
        for (int y = 0; y < bigH; y++)
            for (int x = 0; x < bigW; x++)
                big[y * bigW + x] = Ink(x0 + x / 8, y0 + y / 8) ? 255 : 0;
        var horiz = BoxPass(big, bigW, bigH, bw, horizontal: true);
        var small = BoxPass(horiz, bw, bigH, BodyRows, horizontal: false);

        var body = new HashSet<(int X, int Y)>();
        for (int y = 0; y < BodyRows; y++)
        {
            int off = Round(Slant * (BodyRows - 1 - y) / (double)(BodyRows - 1));
            for (int x = 0; x < bw; x++)
                if (small[y * bw + x] >= 110) body.Add((x + 1 + off, y + 1));
        }
        var ring = body.SelectMany(p => Neighbours(p.X, p.Y)).Where(p => !body.Contains(p)).ToHashSet();

        int w = bw + Slant + 2;
        var px = new Rgb?[w * Height];
        foreach (var (x, y) in ring) px[y * w + x] = pal.Edge;
        foreach (var (x, y) in body)
        {
            bool inner = Neighbours(x, y).All(body.Contains);
            px[y * w + x] = Palette.Row(inner ? pal.Inner : pal.Outer, y);
        }

        int left = Enumerable.Range(0, w).First(x => Enumerable.Range(0, Height).Any(y => px[y * w + x] != null));
        int right = Enumerable.Range(0, w).Last(x => Enumerable.Range(0, Height).Any(y => px[y * w + x] != null));
        int cw = right - left + 1;
        var cropped = new Rgb?[cw * Height];
        for (int y = 0; y < Height; y++)
            for (int x = 0; x < cw; x++)
                cropped[y * cw + x] = px[y * w + x + left];
        return new Frame(cw, Height, cropped);
    }

    private static int[] BoxPass(int[] src, int w, int h, int outSize, bool horizontal)
    {
        int inSize = horizontal ? w : h;
        double scale = (double)inSize / outSize, support = 0.5 * scale;
        int ow = horizontal ? outSize : w, oh = horizontal ? h : outSize;
        var dst = new int[ow * oh];
        for (int o = 0; o < outSize; o++)
        {
            double centre = (o + 0.5) * scale;
            int lo = Math.Max((int)(centre - support + 0.5), 0);
            int hi = Math.Min((int)(centre + support + 0.5), inSize);
            var weights = new List<(int I, double W)>();
            double total = 0;
            for (int i = lo; i < hi; i++)
            {
                double t = (i - centre + 0.5) / scale;
                double wgt = t >= -0.5 && t < 0.5 ? 1.0 : 0.0;
                if (wgt == 0) continue;
                weights.Add((i, wgt)); total += wgt;
            }
            int across = horizontal ? h : w;
            for (int a = 0; a < across; a++)
            {
                double sum = 0;
                foreach (var (i, wgt) in weights)
                    sum += wgt * (horizontal ? src[a * w + i] : src[i * w + a]);
                int v = Math.Clamp((int)Math.Floor(sum / total + 0.5), 0, 255);
                if (horizontal) dst[a * ow + o] = v; else dst[o * ow + a] = v;
            }
        }
        return dst;
    }

    private static byte[] ToBmp(Frame f)
    {
        int stride = (f.Width * 3 + 3) & ~3, size = 54 + stride * f.Height;
        var b = new byte[size];
        void Put32(int at, int v) { b[at] = (byte)v; b[at + 1] = (byte)(v >> 8); b[at + 2] = (byte)(v >> 16); b[at + 3] = (byte)(v >> 24); }
        b[0] = (byte)'B'; b[1] = (byte)'M';
        Put32(2, size); Put32(10, 54); Put32(14, 40);
        Put32(18, f.Width); Put32(22, f.Height);
        b[26] = 1; b[28] = 24;
        Put32(34, stride * f.Height);
        for (int y = 0; y < f.Height; y++)
        {
            int row = 54 + (f.Height - 1 - y) * stride;
            for (int x = 0; x < f.Width; x++)
            {
                var c = f.At(x, y) ?? new Rgb(255, 0, 255);
                b[row + x * 3] = (byte)c.B; b[row + x * 3 + 1] = (byte)c.G; b[row + x * 3 + 2] = (byte)c.R;
            }
        }
        return b;
    }
}
