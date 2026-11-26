using U9GL.Core.Data;

namespace U9GL.Core.Saves;

public sealed record NpcPatchSave(int Slot, int Matching, int Changed)
{
    public bool StartedBefore => Matching * 2 < Changed;
}

public sealed record NpcPatchSaves(int ChangedRecords, IReadOnlyList<NpcPatchSave> Saves, int Unreadable)
{
    public IReadOnlyList<NpcPatchSave> Before => Saves.Where(s => s.StartedBefore).ToList();
}

public static class NpcPatchSavesCheck
{
    public const int TableAt = 8 + 1024;

    private const int Record = 0x13C;

    public static NpcPatchSaves Check(string saveDirectory, string runtimeDirectory, CancellationToken cancel = default)
    {
        string? npc = Directory.Exists(runtimeDirectory)
            ? Directory.EnumerateFiles(runtimeDirectory).FirstOrDefault(p => string.Equals(Path.GetFileName(p), "NPC.FLX", StringComparison.OrdinalIgnoreCase))
            : null;
        if (npc == null) return new NpcPatchSaves(0, [], 0);
        var changed = ContentCheck.Load().ChangedHealth(File.ReadAllBytes(npc));
        if (changed.Count == 0) return new NpcPatchSaves(0, [], 0);

        var rows = new List<NpcPatchSave>();
        int unreadable = 0;
        if (Directory.Exists(saveDirectory))
        {
            foreach (string path in Directory.EnumerateFiles(saveDirectory, "u9game*.sav"))
            {
                cancel.ThrowIfCancellationRequested();
                if (!int.TryParse(Path.GetFileNameWithoutExtension(path).AsSpan(6), out int slot)) continue;
                try
                {
                    var processes = U9SaveArchive.FromBytes(File.ReadAllBytes(path)).Processes.Data.Span;
                    int matching = 0;
                    foreach (var (record, value) in changed)
                    {
                        long at = TableAt + (long)record * Record + ContentCheck.NpcBaseMaximumAt;
                        if (at + 2 <= processes.Length && U9Bytes.U16(processes, at) == value) matching++;
                    }
                    rows.Add(new NpcPatchSave(slot, matching, changed.Count));
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException or U9SaveException
                                              or ArgumentException or IndexOutOfRangeException)
                {
                    unreadable++;
                }
            }
        }
        return new NpcPatchSaves(changed.Count, rows.OrderBy(r => r.Slot).ToList(), unreadable);
    }
}
