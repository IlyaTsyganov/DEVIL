using System.Globalization;
using System.Text.Json;

namespace Devil.Game;

public sealed record StateVolume(string Key, string Name, string Class, string Kind, string Status, string Geometry,
    uint? Contents, float[]? Bounds, float[]? Transform, string Shape)
{
    public string Display => Name.Length == 0 ? $"<{Class}> [{Key}]" : Name;
}
public sealed record StateManager(string Key, string Name, string Label, int? ThinkFlags, int Enabled, int Paused,
    int SpawnPaused, int WaitingExit, int Script, int Commands)
{
    public bool? Updating => ThinkFlags is { } flags ? flags != 0 : null;
}
public sealed record LevelStateSnapshot(int Process, string Token, long Captured, string Phase, string Map, string Profile,
    string Identity, string CheckpointStatus, string Checkpoint, int Unavailable, bool MembershipComplete, string ManagersPhase,
    IReadOnlyList<string> Members, IReadOnlyList<StateVolume> Volumes, IReadOnlyList<StateManager> Managers)
{
    public bool Ready => Phase == "ready" && MembershipComplete;
    public static bool ValidToken(string? token) => token is { Length: 32 } && token.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
    public static bool ValidKey(string? key) => key?.Split(':') is { Length: 2 } parts && uint.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out _) && uint.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out _);
    public static string FilePath(string directory, int process, string role) => role is "before" or "after"
        ? Path.Combine(directory, $"level-state-{process}-{role}.tsv") : throw new ArgumentException("Invalid capture role");
    public static void Request(string directory, int process, string token, string role)
    {
        if (process <= 0 || !ValidToken(token) || role is not ("before" or "after")) throw new IOException("Invalid snapshot request");
        Directory.CreateDirectory(directory); string path = Path.Combine(directory, $"level-state-{process}.request"), temporary = path + ".tmp";
        File.WriteAllText(temporary, $"#DEVIL_STATE_REQUEST_V1\t{process}\t{token}\t{role}\t{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}\n"); File.Move(temporary, path, true);
    }
    public static LevelStateSnapshot? Read(string directory, int process, string role)
    {
        string path = FilePath(directory, process, role);
        if (process <= 0 || !File.Exists(path) || new FileInfo(path).Length > 64_000_000) return null;
        var result = Parse(File.ReadAllLines(path)); return result?.Process == process ? result : null;
    }
    public static LevelStateSnapshot? Parse(string[] lines)
    {
        if (lines.Length is < 1 or > 45000 || lines.Any(line => line.Length > 4096)) return null;
        string[] header = lines[0].Split('\t');
        if (header.Length != 13 || header[0] != "#DEVIL_LEVEL_STATE_V1" || !int.TryParse(header[1], out int process) || process <= 0 || !ValidToken(header[2])
            || !long.TryParse(header[3], out long captured) || captured <= 0 || captured > 253402300799999 || header[4] is not ("ready" or "loading" or "read_error")
            || header[5].Length > 512 || header[6].Length is 0 or > 128 || header[7].Length is 0 or > 80 || header[8] is not ("known" or "unavailable" or "read_error") || header[9].Length > 1023
            || !int.TryParse(header[10], out int unavailable) || unavailable < 0 || header[11] is not ("0" or "1") || header[12].Length > 40) return null;
        var members = new HashSet<string>(StringComparer.Ordinal); var volumes = new Dictionary<string, StateVolume>(StringComparer.Ordinal); var managers = new Dictionary<string, StateManager>(StringComparer.Ordinal);
        foreach (string line in lines.Skip(1))
        {
            string[] cells = line.Split('\t');
            if (cells.Length < 3 || !uint.TryParse(cells[1], out uint id) || !uint.TryParse(cells[2], out uint generation)) return null;
            string key = $"{id}:{generation}";
            if (cells[0] == "I") { if (cells.Length != 3 || members.Count >= 32768 || !members.Add(key)) return null; }
            else if (cells[0] == "V")
            {
                if (cells.Length != 28 || volumes.Count >= 8192 || cells[3].Length > 512 || cells[4].Length > 128 || cells[4].Length == 0
                    || cells[5] is not ("trigger" or "wall") || cells[6] is not ("enabled" or "disabled" or "removed" or "unavailable")
                    || cells[7] is not ("exact" or "last" or "bounds" or "last_bounds" or "none") || cells[27].Length > 16 || cells[27].Any(c => !char.IsAsciiHexDigit(c))) return null;
                uint? contents = null;
                if (cells[8] != "?") { if (!uint.TryParse(cells[8], out uint flags)) return null; contents = flags; }
                float[]? bounds = null, transform = null;
                var numbers = new List<float>(); bool unknown = cells[9] == "?";
                for (int i = 9; i < 27; ++i)
                {
                    if (unknown) { if (cells[i] != "?") return null; }
                    else if (float.TryParse(cells[i], NumberStyles.Float, CultureInfo.InvariantCulture, out float value) && float.IsFinite(value) && Math.Abs(value) < 1_000_000) numbers.Add(value);
                    else return null;
                }
                if (!unknown) { bounds = numbers.Take(6).ToArray(); transform = numbers.Skip(6).ToArray(); if (Enumerable.Range(0, 3).Any(i => bounds[i] > bounds[i + 3])) return null; }
                if (!volumes.TryAdd(key, new(key, cells[3], cells[4], cells[5], cells[6], cells[7], contents, bounds, transform, cells[27]))) return null;
            }
            else if (cells[0] == "M")
            {
                if (cells.Length != 12 || managers.Count >= 2048 || cells[3].Length > 512 || cells[4].Length > 512) return null;
                int? think = null;
                if (cells[5] != "?") { if (!int.TryParse(cells[5], out int flags)) return null; think = flags; }
                var values = new int[6];
                for (int i = 0; i < 6; ++i) if (!int.TryParse(cells[i + 6], out values[i])) return null;
                if (values.Take(4).Any(value => value is < -1 or > 1) || values[5] is < -1 or > 65536) return null;
                if (!managers.TryAdd(key, new(key, cells[3], cells[4], think, values[0], values[1], values[2], values[3], values[4], values[5]))) return null;
            }
            else return null;
        }
        if (header[4] == "ready" && (header[5].Length == 0 || header[11] != "1") || volumes.Keys.Concat(managers.Keys).Any(key => !members.Contains(key))) return null;
        return new(process, header[2], captured, header[4], header[5], header[6], header[7], header[8], header[9], unavailable, header[11] == "1", header[12], members.ToArray(), volumes.Values.ToArray(), managers.Values.ToArray());
    }
}
public sealed record LevelStateChange(string Category, string Kind, string Key, string Name, string Class, string Before,
    string After, string Detail, StateVolume? BeforeVolume = null, StateVolume? AfterVolume = null)
{
    public bool IsVolume => Category == "Volume";
}
public static class LevelStateComparison
{
    public static IReadOnlyList<LevelStateChange> Compare(LevelStateSnapshot before, LevelStateSnapshot after)
    {
        if (!before.Ready || !after.Ready) throw new InvalidOperationException("Both captures must contain a complete entity table.");
        if (before.Process != after.Process || before.Profile != after.Profile || before.Map != after.Map || before.Identity != after.Identity)
            throw new InvalidOperationException("Different game process, executable profile or map/world instance. Capture a new Before after a level reload.");
        if (after.Captured < before.Captured) throw new InvalidOperationException("After was captured earlier than Before.");
        var changes = new List<LevelStateChange>(); var first = before.Volumes.ToDictionary(row => row.Key); var second = after.Volumes.ToDictionary(row => row.Key);
        var firstMembers = before.Members.ToHashSet(); var secondMembers = after.Members.ToHashSet();
        foreach (string key in first.Keys.Union(second.Keys).Order(StringComparer.Ordinal))
        {
            first.TryGetValue(key, out var a); second.TryGetValue(key, out var b); var item = b ?? a!;
            void Add(string kind, string old, string current, string detail) => changes.Add(new("Volume", kind, key, item.Display, item.Class, old, current, detail, a, b));
            if (a is null) { Add(firstMembers.Contains(key) ? "Observation recovered" : "Added", "Not observed", b!.Status, firstMembers.Contains(key) ? "Entity already existed; it is now recognizable/readable as a volume." : "New entity identity in the complete map table."); continue; }
            if (b is null) { Add(secondMembers.Contains(key) ? "Unavailable" : "Removed", a.Status, secondMembers.Contains(key) ? "Unobserved" : "Absent", secondMembers.Contains(key) ? "Entity still exists; missing volume data is not a deletion." : "Identity is absent from the complete After entity table."); continue; }
            if (a.Status == "unavailable" || b.Status == "unavailable") { Add(b.Status == "unavailable" ? "Unavailable" : "Observation recovered", a.Status, b.Status, "Current collision state could not be read in one capture; no activation inference."); continue; }
            if (a.Status != b.Status) Add(b.Status switch { "disabled" => "Disabled", "enabled" => "Enabled", "removed" => "Removed", _ => "State changed" }, a.Status, b.Status, "Observed collision-state change; not proof of a script event.");
            if (a.Contents is { } oldFlags && b.Contents is { } newFlags && oldFlags != newFlags) Add("Collision flags", $"0x{oldFlags:x}", $"0x{newFlags:x}", "Raw physics contents flags.");
            if (a.Transform is { } oldPose && b.Transform is { } newPose && oldPose.Zip(newPose).Any(pair => Math.Abs(pair.First - pair.Second) > .01f)) Add("Moved / rotated", Position(a), Position(b), "Captured physics transform changed by more than 0.01 units/components.");
            else if (a.Bounds is { } oldBounds && b.Bounds is { } newBounds && oldBounds.Zip(newBounds).Any(pair => Math.Abs(pair.First - pair.Second) > .01f)) Add("Bounds changed", Position(a), Position(b), "Captured volume bounds changed by more than 0.01 units.");
            if (a.Shape.Length > 0 && b.Shape.Length > 0 && a.Shape != b.Shape) Add("Geometry fingerprint", a.Shape, b.Shape, "Decoded local triangles differ (coordinates, topology or ordering); quantization 0.0001 units.");
            if (a.Geometry != b.Geometry) Add("Geometry availability", a.Geometry, b.Geometry, "Exact, saved-last or bounding-only outline availability changed.");
        }
        var oldManagers = before.Managers.ToDictionary(row => row.Key); var newManagers = after.Managers.ToDictionary(row => row.Key);
        foreach (string key in oldManagers.Keys.Union(newManagers.Keys).Order(StringComparer.Ordinal))
        {
            oldManagers.TryGetValue(key, out var a); newManagers.TryGetValue(key, out var b); var item = b ?? a!;
            void Add(string kind, string old, string current, string detail) => changes.Add(new("Manager", kind, key, item.Name, "idEncounterManager", old, current, detail));
            if (a is null) { Add(firstMembers.Contains(key) ? "Observation recovered" : "Added", "Not observed", ManagerText(b!), "Observed manager fields, independent of volume geometry."); continue; }
            if (b is null) { Add(secondMembers.Contains(key) ? "Unavailable" : "Removed", ManagerText(a), "Not observed", secondMembers.Contains(key) ? "Identity still exists; manager reading is unavailable." : "Identity absent from After table."); continue; }
            if (a.ThinkFlags is null || b.ThinkFlags is null) Add("Unavailable", ManagerText(a), ManagerText(b), "Unknown thinkFlags must not be treated as an inactive manager.");
            if (ManagerText(a) != ManagerText(b)) Add("Observed fields differ", ManagerText(a), ManagerText(b), "thinkFlags, enable/pause flags and script counters; -1/? can mean unavailable. Not a combat start/completion event.");
        }
        if (before.CheckpointStatus == "known" && after.CheckpointStatus == "known")
        {
            if (before.Checkpoint != after.Checkpoint) changes.Add(new("Checkpoint", "Loaded checkpoint changed", "", "Loaded checkpoint", "idGameSpawnInfo", before.Checkpoint, after.Checkpoint, "Name in current spawn info; does not prove a new save was written."));
        }
        else changes.Add(new("Checkpoint", "Unavailable", "", "Loaded checkpoint", "idGameSpawnInfo", before.CheckpointStatus, after.CheckpointStatus, "Checkpoint could not be read in both snapshots; no change inference."));
        return changes;
    }
    public static string Position(StateVolume volume) => volume.Bounds is { } b ? FormattableString.Invariant($"({(b[0] + b[3]) / 2:0.000}, {(b[1] + b[4]) / 2:0.000}, {(b[2] + b[5]) / 2:0.000})") : "No bounds";
    private static string ManagerText(StateManager value) => $"think={value.ThinkFlags?.ToString() ?? "?"}; enabled={value.Enabled}; paused={value.Paused}; spawnPause={value.SpawnPaused}; exit={value.WaitingExit}; script={value.Script}; commands={value.Commands}";
}
public sealed record LevelStatePair(string Format, string Note, LevelStateSnapshot Before, LevelStateSnapshot After)
{
    public void Save(string path)
    {
        Validate(this);
        File.WriteAllText(path, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }
    public static LevelStatePair Load(string path)
    {
        if (new FileInfo(path).Length > 64_000_000) throw new IOException("Snapshot pair is too large.");
        LevelStatePair? pair;
        try { pair = JsonSerializer.Deserialize<LevelStatePair>(File.ReadAllText(path)); }
        catch (JsonException ex) { throw new IOException("Snapshot pair JSON is invalid.", ex); }
        Validate(pair); return pair!;
    }
    private static void Validate(LevelStatePair? pair)
    {
        if (pair is null || pair.Format != "DEVIL_LEVEL_STATE_PAIR_V1" || pair.Note is not { Length: <= 1024 } || !Valid(pair.Before) || !Valid(pair.After)) throw new IOException("Invalid snapshot pair.");
        try { _ = LevelStateComparison.Compare(pair.Before, pair.After); }
        catch (InvalidOperationException ex) { throw new IOException(ex.Message, ex); }
    }
    private static bool Valid(LevelStateSnapshot? state)
    {
        if (!(state is { Ready: true } && state.Process > 0 && LevelStateSnapshot.ValidToken(state.Token)
        && state.Captured is > 0 and <= 253402300799999 && state.Map is { Length: > 0 and <= 512 } && state.Profile is { Length: > 0 and <= 128 } && state.Identity is { Length: > 0 and <= 80 }
        && state.Unavailable >= 0 && state.ManagersPhase is { Length: <= 40 } && state.Checkpoint is { Length: <= 1023 } && state.CheckpointStatus is "known" or "unavailable" or "read_error"
        && state.Members is { Count: <= 32768 } && state.Members.All(LevelStateSnapshot.ValidKey) && state.Members.Distinct().Count() == state.Members.Count
        && state.Volumes is { Count: <= 8192 } && state.Volumes.All(v => v is not null && LevelStateSnapshot.ValidKey(v.Key) && v.Name is { Length: <= 512 } && v.Class is { Length: > 0 and <= 128 }
            && v.Status is "enabled" or "disabled" or "removed" or "unavailable" && v.Kind is "trigger" or "wall" && v.Geometry is "exact" or "last" or "bounds" or "last_bounds" or "none"
            && v.Shape is { Length: <= 16 } && v.Shape.All(char.IsAsciiHexDigit) && (v.Bounds is null && v.Transform is null || v.Bounds is { Length: 6 } b && v.Transform is { Length: 12 } t && b.Concat(t).All(n => float.IsFinite(n) && Math.Abs(n) < 1_000_000) && Enumerable.Range(0, 3).All(i => b[i] <= b[i + 3])))
        && state.Volumes.Select(v => v.Key).Distinct().Count() == state.Volumes.Count
        && state.Managers is { Count: <= 2048 } && state.Managers.All(m => m is not null && LevelStateSnapshot.ValidKey(m.Key) && m.Name is { Length: <= 512 } && m.Label is { Length: <= 512 }
            && m.Enabled is >= -1 and <= 1 && m.Paused is >= -1 and <= 1 && m.SpawnPaused is >= -1 and <= 1 && m.WaitingExit is >= -1 and <= 1 && m.Commands is >= -1 and <= 65536)
        && state.Managers.Select(m => m.Key).Distinct().Count() == state.Managers.Count)) return false;
        var members = state.Members.ToHashSet(StringComparer.Ordinal);
        return state.Volumes.Select(v => v.Key).Concat(state.Managers.Select(m => m.Key)).All(members.Contains);
    }
}
