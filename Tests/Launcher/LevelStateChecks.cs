using Devil.Game;
using System.Globalization;
using System.Text.Json;

internal static class LevelStateChecks
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Reject(Action action, string message)
    {
        try { action(); } catch (Exception ex) when (ex is IOException or InvalidOperationException) { return; }
        throw new Exception(message);
    }
    private static string Volume(int id, string status = "enabled", float x = 0, string shape = "abcd") =>
        string.Join('\t', new[] { "V", id.ToString(), "3", $"trigger/test/{id}", "idTrigger", "trigger", status, "exact", "16384" }
            .Concat(new float[] { x, 0, 0, x + 1, 1, 1, 1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, 0 }.Select(n => n.ToString(CultureInfo.InvariantCulture))).Append(shape));
    internal static void Run(string directory)
    {
        string token = new('a', 32);
        string header = $"#DEVIL_LEVEL_STATE_V1\t1234\t{token}\t1000\tready\tmap_a\tprofile_a\t1:2\tknown\tcp_a\t0\t1\tlive";
        string[] lines = [header, "I\t1\t3", "I\t2\t3", "I\t3\t3", "I\t4\t3", Volume(1), Volume(2), "M\t4\t3\tmanager\tArena 1\t1\t1\t0\t0\t0\t2\t3"];
        var before = LevelStateSnapshot.Parse(lines)!;
        Check(before is { Ready: true, Volumes.Count: 2, Members.Count: 4, Managers.Count: 1 }, "Valid state protocol rejected");
        Check(LevelStateSnapshot.Parse([.. lines, "I\t1\t3"]) is null, "Duplicate membership accepted");
        Check(LevelStateSnapshot.Parse(lines.Where(l => l != "I\t2\t3").ToArray()) is null, "Dangling volume accepted");
        Check(LevelStateSnapshot.Parse(lines.Select(l => l.Replace("\t16384\t0\t", "\t16384\tNaN\t")).ToArray()) is null, "Non-finite bounds accepted");
        Check(LevelStateSnapshot.Parse(lines.Select(l => l.Replace("\tready\tmap_a", "\tready\t")).ToArray()) is null, "Ready snapshot with no map accepted");
        var after = before with { Token = new('b', 32), Captured = 2000, Checkpoint = "cp_b", Members = ["1:3", "3:3", "4:3", "5:3"],
            Volumes = [before.Volumes[0] with { Status = "disabled", Contents = 3, Transform = [1, 0, 0, 2, 0, 1, 0, 0, 0, 0, 1, 0], Shape = "cdef" }, before.Volumes[0] with { Key = "5:3" }],
            Managers = [before.Managers[0] with { ThinkFlags = 0, Paused = 1 }] };
        var changes = LevelStateComparison.Compare(before, after);
        foreach (string kind in new[] { "Disabled", "Removed", "Added", "Collision flags", "Moved / rotated", "Geometry fingerprint", "Observed fields differ", "Loaded checkpoint changed" })
            Check(changes.Any(c => c.Kind == kind), "Missing comparison: " + kind);
        var unreadable = after with { Members = before.Members, Volumes = [] };
        Check(LevelStateComparison.Compare(before, unreadable).Where(c => c.IsVolume).All(c => c.Kind == "Unavailable"), "Unreadable entity was called deleted");
        var recovered = before with { Volumes = [.. before.Volumes, before.Volumes[0] with { Key = "3:3" }] };
        Check(LevelStateComparison.Compare(before, recovered).Single().Kind == "Observation recovered", "Pre-existing identity was called added");
        var reused = before with { Volumes = [before.Volumes[0] with { Key = "1:4" }, before.Volumes[1]], Members = ["1:4", "2:3", "3:3", "4:3"] };
        var reuse = LevelStateComparison.Compare(before, reused);
        Check(reuse.Any(c => c.Key == "1:3" && c.Kind == "Removed") && reuse.Any(c => c.Key == "1:4" && c.Kind == "Added"), "Generation reuse merged identities");
        var unknown = before with { Volumes = [before.Volumes[0] with { Status = "unavailable" }, before.Volumes[1]], CheckpointStatus = "read_error", Managers = [before.Managers[0] with { ThinkFlags = null }] };
        Check(LevelStateComparison.Compare(before, unknown).Count(c => c.Kind == "Unavailable") == 3 && !LevelStateComparison.Compare(before, unknown).Any(c => c.Kind is "Enabled" or "Disabled" or "Loaded checkpoint changed"), "Unavailable data fabricated state transitions");
        foreach (var incompatible in new[] { after with { Process = 9 }, after with { Identity = "3:4" }, after with { Map = "map_b" }, after with { Profile = "other" }, after with { Captured = 1 }, after with { MembershipComplete = false } })
            Reject(() => LevelStateComparison.Compare(before, incompatible), "Incompatible snapshots compared");
        string path = Path.Combine(directory, "state-pair.json"); var pair = new LevelStatePair("DEVIL_LEVEL_STATE_PAIR_V1", "Test experiment", before, after);
        pair.Save(path); var loaded = LevelStatePair.Load(path);
        Check(loaded.Note == pair.Note && LevelStateComparison.Compare(loaded.Before, loaded.After).Count == changes.Count, "JSON round trip lost changes");
        File.WriteAllText(path, JsonSerializer.Serialize(pair with { Before = before with { Managers = [before.Managers[0] with { Enabled = 99 }] } }));
        Reject(() => LevelStatePair.Load(path), "Invalid manager flags loaded");
        File.WriteAllText(path, "{oops"); Reject(() => LevelStatePair.Load(path), "Malformed JSON loaded");
        Reject(() => (pair with { After = after with { Identity = "3:4" } }).Save(path), "Incompatible pair saved");
        LevelStateSnapshot.Request(directory, 1234, token, "before");
        string request = File.ReadAllText(Path.Combine(directory, "level-state-1234.request"));
        Check(request.StartsWith($"#DEVIL_STATE_REQUEST_V1\t1234\t{token}\tbefore\t") && !File.Exists(Path.Combine(directory, "level-state-1234.request.tmp")), "Request not atomically scoped");
        Reject(() => LevelStateSnapshot.Request(directory, 1234, "bad", "before"), "Invalid token requested");
        Reject(() => LevelStateSnapshot.Request(directory, 1234, token, "../after"), "Invalid role requested");
        File.WriteAllLines(LevelStateSnapshot.FilePath(directory, 5678, "before"), lines);
        Check(LevelStateSnapshot.Read(directory, 5678, "before") is null, "Foreign process capture accepted");
        Console.WriteLine("PASS: level-state protocol, genuine additions/deletions vs unavailable reads, generation reuse, state/geometry/manager/checkpoint changes, scope, JSON validation and capture requests.");
    }
}
