using System.Text.RegularExpressions;

namespace U9GL.Core.Saves;

public enum SaveFileKind
{
    Archive,
    Selector,
    Processes,
    Diary,
    Nonfixed,
    UnfinishedSave,
    Other,
}

public sealed record SaveSlotInfo(int Slot, string Path, long Bytes, DateTime Modified, U9SaveHeader? Header, string? HeaderError)
{
    public string FileName => System.IO.Path.GetFileName(Path);
}

public sealed record SaveFolderFile(string Name, string Path, long Bytes, DateTime Modified, SaveFileKind Kind, int? Number);

public sealed partial class SaveFolderScan
{
    private const int HeaderPrefix = 64 * 1024;

    [GeneratedRegex(@"^u9game(\d+)\.(sav|tmp)$", RegexOptions.IgnoreCase)]
    private static partial Regex SlotFile();

    [GeneratedRegex(@"^nonfixed\.(\d+)$", RegexOptions.IgnoreCase)]
    private static partial Regex NonfixedFile();

    private SaveFolderScan(string directory) => Directory = directory;

    public string Directory { get; }
    public bool Exists { get; private init; }
    public int? SelectedSlot { get; private init; }
    public string? SelectorError { get; private init; }
    public IReadOnlyList<SaveSlotInfo> Slots { get; private init; } = [];

    public IReadOnlyList<int> DuplicateSlots { get; private init; } = [];

    public IReadOnlyList<SaveFolderFile> OtherFiles { get; private init; } = [];

    public SaveSlotInfo? Selected => Slots.FirstOrDefault(s => s.Slot == SelectedSlot);

    public static SaveFolderScan Read(string saveDirectory)
    {
        if (!System.IO.Directory.Exists(saveDirectory)) return new SaveFolderScan(saveDirectory);

        var slots = new List<SaveSlotInfo>();
        var others = new List<SaveFolderFile>();
        foreach (var info in new DirectoryInfo(saveDirectory).EnumerateFiles())
        {
            var slot = SlotFile().Match(info.Name);
            if (slot.Success && int.TryParse(slot.Groups[1].Value, out int number))
            {
                if (slot.Groups[2].Value.Equals("sav", StringComparison.OrdinalIgnoreCase))
                    slots.Add(ReadSlot(info, number));
                else
                    others.Add(new SaveFolderFile(info.Name, info.FullName, info.Length, info.LastWriteTime, SaveFileKind.UnfinishedSave, number));
                continue;
            }
            var nonfixed = NonfixedFile().Match(info.Name);
            var (kind, map) = info.Name.ToLowerInvariant() switch
            {
                "start.dat" => (SaveFileKind.Selector, (int?)null),
                "processes.dat" => (SaveFileKind.Processes, null),
                "diary.txt" => (SaveFileKind.Diary, null),
                _ when nonfixed.Success && int.TryParse(nonfixed.Groups[1].Value, out int m) => (SaveFileKind.Nonfixed, m),
                _ => (SaveFileKind.Other, null),
            };
            others.Add(new SaveFolderFile(info.Name, info.FullName, info.Length, info.LastWriteTime, kind, map));
        }

        int? selected = null;
        string? selectorError = null;
        string start = Path.Combine(saveDirectory, "start.dat");
        try
        {
            selected = U9StartDat.FromFile(start).Slot;
        }
        catch (FileNotFoundException)
        {
            selectorError = "start.dat is missing, so the game has no save to load at start.";
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or U9SaveException)
        {
            selectorError = e.Message;
        }

        slots.Sort((a, b) => a.Slot != b.Slot ? a.Slot.CompareTo(b.Slot) : string.Compare(a.Path, b.Path, StringComparison.OrdinalIgnoreCase));
        others.Sort((a, b) => a.Kind != b.Kind ? a.Kind.CompareTo(b.Kind)
                            : a.Number != b.Number ? Nullable.Compare(a.Number, b.Number)
                            : string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase));
        return new SaveFolderScan(saveDirectory)
        {
            Exists = true,
            SelectedSlot = selected,
            SelectorError = selectorError,
            Slots = slots,
            DuplicateSlots = slots.GroupBy(s => s.Slot).Where(g => g.Count() > 1).Select(g => g.Key).ToList(),
            OtherFiles = others,
        };
    }

    private static SaveSlotInfo ReadSlot(FileInfo info, int slot)
    {
        try
        {
            using var stream = info.OpenRead();
            byte[] prefix = new byte[(int)Math.Min(info.Length, HeaderPrefix)];
            stream.ReadExactly(prefix);
            U9SaveHeader header;
            try
            {
                header = U9SaveArchive.ReadHeader(prefix);
            }
            catch (U9SaveException) when (info.Length > prefix.Length)
            {
                header = U9SaveArchive.ReadHeader(File.ReadAllBytes(info.FullName));
            }
            return new SaveSlotInfo(slot, info.FullName, info.Length, info.LastWriteTime, header, null);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or U9SaveException or IndexOutOfRangeException)
        {
            return new SaveSlotInfo(slot, info.FullName, info.Length, info.LastWriteTime, null, e.Message);
        }
    }
}
