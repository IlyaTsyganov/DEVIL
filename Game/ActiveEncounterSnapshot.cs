using System.Globalization;

namespace Devil.Game;

public sealed record ActiveEncounter(uint Id, uint Generation, string Manager, string Label, int? ThinkFlags,
    int Enabled, int Paused, int SpawnPaused, int WaitingExit, int Script, int Commands)
{
    public bool Active => ThinkFlags is { } flags && flags != 0;
    public string DisplayName => string.IsNullOrEmpty(Label) ? "No name in reference" : Label;
    public string State => ThinkFlags is null ? "Unavailable" : Enabled == 0 ? "Disabled"
        : Paused == 1 ? "Paused" : SpawnPaused == 1 ? "Spawn limit pause"
        : WaitingExit == 1 ? "Waiting for exit" : Active ? "Updating" : "Inactive";
}

public sealed record ActiveEncounterSnapshot(int Process, DateTimeOffset Captured, string Phase, string Map,
    int Unreadable, IReadOnlyList<ActiveEncounter> Rows)
{
    public bool Ready => Phase is "live" or "paused";
    public bool IsFresh(DateTimeOffset now) => Captured <= now.AddSeconds(5) && now - Captured <= TimeSpan.FromSeconds(2);

    public IEnumerable<ActiveEncounter> Select(bool includeInactive, string query) => !Ready ? [] : Rows
        .Where(row => (includeInactive || row.Active || row.ThinkFlags is null)
            && (row.Manager + " " + row.Label + " " + row.State + " " + row.Id.ToString(CultureInfo.InvariantCulture))
                .Contains(query.Trim(), StringComparison.OrdinalIgnoreCase))
        .OrderByDescending(row => row.Active).ThenBy(row => string.IsNullOrEmpty(row.Label) ? row.Manager : row.Label, StringComparer.Ordinal)
        .ThenBy(row => row.Id).ThenBy(row => row.Generation);

    public static ActiveEncounterSnapshot? Read(string directory, int process)
    {
        if (process <= 0) return null;
        string path = Path.Combine(directory, $"active-encounters-{process}.tsv");
        if (!File.Exists(path) || new FileInfo(path).Length > 2_000_000) return null;
        var snapshot = Parse(File.ReadAllLines(path));
        return snapshot?.Process == process ? snapshot : null;
    }

    public static ActiveEncounterSnapshot? Parse(string[] lines)
    {
        if (lines.Length < 2 || lines.Length > 2050) return null;
        string[] header = lines[0].Split('\t');
        if (header.Length != 6 || header[0] != "#DEVIL_ENCOUNTERS_V1" || !int.TryParse(header[1], out int process) || process <= 0
            || !long.TryParse(header[2], out long time) || !int.TryParse(header[5], out int unreadable) || unreadable < 0) return null;
        DateTimeOffset captured;
        try { captured = DateTimeOffset.FromUnixTimeMilliseconds(time); }
        catch (ArgumentOutOfRangeException) { return null; }
        var rows = new List<ActiveEncounter>();
        foreach (string line in lines.Skip(2))
        {
            string[] cells = line.Split('\t');
            if (cells.Length != 11 || cells.Any(cell => cell.Length > 1024) || !uint.TryParse(cells[0], out uint id)
                || !uint.TryParse(cells[1], out uint generation)) return null;
            int? think = null;
            if (cells[4] != "?") { if (!int.TryParse(cells[4], out int flags)) return null; think = flags; }
            int[] state = new int[6];
            for (int i = 0; i < state.Length; ++i) if (!int.TryParse(cells[i + 5], out state[i])) return null;
            if (state.Take(4).Any(value => value is < -1 or > 1) || state[5] is < -1 or > 65536) return null;
            rows.Add(new(id, generation, cells[2], cells[3], think, state[0], state[1], state[2], state[3], state[4], state[5]));
        }
        return new(process, captured, header[3], header[4], unreadable, rows);
    }
}
