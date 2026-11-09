using System.Text;

namespace U9GL.Core.Data;

public sealed class TypeNames
{
    private readonly string?[] _names;

    private TypeNames(string?[] names) => _names = names;

    public int Count => _names.Length;

    public string? this[int id] => id >= 0 && id < _names.Length ? _names[id] : null;

    public static TypeNames? Load(string gameRoot)
    {
        string path = Path.Combine(gameRoot, "static", "TYPENAME.FLX");
        try
        {
            return File.Exists(path) ? Parse(File.ReadAllBytes(path)) : null;
        }
        catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }

    public static TypeNames? Parse(byte[] data)
    {
        const int DirectoryAt = 0x80;
        if (data.Length < DirectoryAt) return null;
        int count = BitConverter.ToInt32(data, 0x50);
        if (count <= 0 || count > 65536 || DirectoryAt + (long)count * 8 > data.Length) return null;
        var names = new string?[count];
        for (int i = 0; i < count; i++)
        {
            int offset = BitConverter.ToInt32(data, DirectoryAt + i * 8);
            int length = BitConverter.ToInt32(data, DirectoryAt + i * 8 + 4);
            if (length <= 6 || offset < DirectoryAt || (long)offset + length > data.Length) continue;
            int start = offset + 6, end = start;
            while (end < offset + length && data[end] != 0) end++;
            if (end == start) continue;
            string name = Encoding.Latin1.GetString(data, start, end - start).Trim();
            names[i] = name.Length > 0 ? name : null;
        }
        return new TypeNames(names);
    }
}
