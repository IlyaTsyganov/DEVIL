using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Devil.Game;

public sealed record BookmarkEntity(string Key, string Name, string Class, string State, long AimAgeMilliseconds = 0);
public sealed record BookmarkObservation(int Process, string Token, long Captured, string Phase, string Map, string Profile,
    string Identity, string CheckpointStatus, string Checkpoint, MotionVector Body, MotionVector Eye, MotionVector Forward,
    float[] ViewProjection, IReadOnlyList<BookmarkEntity> Entities)
{
    [JsonIgnore] public bool Ready => Phase == "ready";
    [JsonIgnore] public double Yaw => Math.Atan2(Forward.Y, Forward.X) * 180 / Math.PI;
    [JsonIgnore] public double Pitch => -Math.Asin(Math.Clamp(Forward.Z, -1, 1)) * 180 / Math.PI;
    public static string FilePath(string directory, int process) => Path.Combine(directory, $"research-point-{process}.tsv");
    public static void Request(string directory, int process, string token)
    {
        if (process <= 0 || !LevelStateSnapshot.ValidToken(token)) throw new IOException("Invalid point capture request.");
        Directory.CreateDirectory(directory); string path = Path.Combine(directory, $"research-point-{process}.request");
        File.WriteAllText(path + ".tmp", $"#DEVIL_POINT_REQUEST_V1\t{process}\t{token}\t{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}\n"); File.Move(path + ".tmp", path, true);
    }
    public static BookmarkObservation? Read(string directory, int process)
    {
        string path = FilePath(directory, process);
        if (process <= 0 || !File.Exists(path) || new FileInfo(path).Length > 16384) return null;
        var result = Parse(File.ReadAllLines(path)); return result?.Process == process ? result : null;
    }
    public static BookmarkObservation? Parse(string[] lines)
    {
        if (lines.Length is < 1 or > 34 || lines.Any(l => l.Length > 4096)) return null;
        string[] h = lines[0].Split('\t');
        if (h.Length != 10 || h[0] != "#DEVIL_POINT_V1" || !int.TryParse(h[1], out int process) || process <= 0 || !LevelStateSnapshot.ValidToken(h[2])
            || !long.TryParse(h[3], out long epoch) || epoch is <= 0 or > 253402300799999 || h[4] is not ("ready" or "loading" or "read_error" or "changed_during_capture")
            || h[5].Length > 512 || h[6].Length is 0 or > 128 || h[7].Length is 0 or > 80 || h[8] is not ("known" or "unavailable" or "read_error") || h[9].Length > 1023) return null;
        var zero = new MotionVector(0, 0, 0);
        if (h[4] != "ready") return lines.Length == 1 ? new(process, h[2], epoch, h[4], h[5], h[6], h[7], h[8], h[9], zero, zero, zero, [], []) : null;
        if (lines.Length < 2 || h[5].Length == 0) return null;
        string[] p = lines[1].Split('\t'); if (p.Length != 26 || p[0] != "P") return null;
        var values = new float[25];
        for (int i = 0; i < values.Length; ++i)
            if (!float.TryParse(p[i + 1], NumberStyles.Float, CultureInfo.InvariantCulture, out values[i]) || !float.IsFinite(values[i]) || Math.Abs(values[i]) >= 1_000_000) return null;
        MotionVector Vector(int i) => new(values[i], values[i + 1], values[i + 2]);
        var entities = new List<BookmarkEntity>();
        foreach (string line in lines.Skip(2))
        {
            string[] e = line.Split('\t');
            if (e.Length != 7 || e[0] != "E" || !LevelStateSnapshot.ValidKey(e[1] + ":" + e[2]) || e[3].Length > 512 || e[4].Length is 0 or > 128 || e[5] is not ("enabled" or "disabled") || !long.TryParse(e[6], out long age) || age is < 0 or > 10000) return null;
            entities.Add(new(e[1] + ":" + e[2], e[3], e[4], e[5], age));
        }
        var result = new BookmarkObservation(process, h[2], epoch, h[4], h[5], h[6], h[7], h[8], h[9], Vector(0), Vector(3), Vector(6), values.Skip(9).ToArray(), entities);
        return Valid(result) ? result : null;
    }
    public static bool Valid(BookmarkObservation? value) => value is { Ready: true, Process: > 0, Captured: > 0 and <= 253402300799999 }
        && LevelStateSnapshot.ValidToken(value.Token) && value.Map is { Length: > 0 and <= 512 } && value.Profile is { Length: > 0 and <= 128 } && value.Identity is { Length: > 0 and <= 80 }
        && value.CheckpointStatus is "known" or "unavailable" or "read_error" && value.Checkpoint is { Length: <= 1023 }
        && value.Body is { Finite: true } && value.Eye is { Finite: true } && value.Forward is { Finite: true, Length: > .99f and < 1.01f }
        && value.ViewProjection is { Length: 16 } && value.ViewProjection.All(n => float.IsFinite(n) && Math.Abs(n) < 1_000_000)
        && value.Entities is { Count: <= 32 } && value.Entities.All(e => e is not null && LevelStateSnapshot.ValidKey(e.Key) && e.Name is { Length: <= 512 }
            && e.Class is { Length: > 0 and <= 128 } && e.State is "enabled" or "disabled" && e.AimAgeMilliseconds is >= 0 and <= 10000) && value.Entities.Select(e => e.Key).Distinct().Count() == value.Entities.Count;
}
public sealed record ResearchBookmark(string Format, string Id, string Name, string Category, string Note, BookmarkObservation Observation, string SaveId = "")
{
    [JsonIgnore] public string Display => $"{Category} · {Name} · {Observation.Map}";
    public override string ToString() => Display;
    public static bool Valid(ResearchBookmark? value) => value is { Format: "DEVIL_RESEARCH_POINT_V1" } && LevelStateSnapshot.ValidToken(value.Id)
        && value.Name is { Length: > 0 and <= 160 } && value.Category is { Length: > 0 and <= 160 } && value.Note is { Length: <= 8192 }
        && value.SaveId is not null && (value.SaveId.Length == 0 || LevelStateSnapshot.ValidToken(value.SaveId)) && BookmarkObservation.Valid(value.Observation);
    public string PositionText() => FormattableString.Invariant($"Player body: {Observation.Body.X:R} {Observation.Body.Y:R} {Observation.Body.Z:R}\nCamera eye: {Observation.Eye.X:R} {Observation.Eye.Y:R} {Observation.Eye.Z:R}\nAim yaw/pitch: {Observation.Yaw:0.000000} {Observation.Pitch:0.000000}");
    public string ReturnCommand() => FormattableString.Invariant($"setviewpos {Observation.Eye.X:R} {Observation.Eye.Y:R} {Observation.Eye.Z:R} {Observation.Yaw:F6} {Observation.Pitch:F6}");
    public string SpawnInfo()
    {
        double yaw = Observation.Yaw * Math.PI / 180, pitch = Observation.Pitch * Math.PI / 180;
        double cp = Math.Cos(pitch), sp = Math.Sin(pitch), cy = Math.Cos(yaw), sy = Math.Sin(yaw);
        return FormattableString.Invariant($"// Rendered camera eye/aim; roll=0. This is not a saved game.\nspawnPosition = {{ x = {Observation.Eye.X:R}; y = {Observation.Eye.Y:R}; z = {Observation.Eye.Z:R}; }}\nspawnOrientation = {{ mat = {{\n mat[0] = {{ x = {cp * cy:F9}; y = {cp * sy:F9}; z = {-sp:F9}; }}\n mat[1] = {{ x = {-sy:F9}; y = {cy:F9}; z = 0; }}\n mat[2] = {{ x = {sp * cy:F9}; y = {sp * sy:F9}; z = {cp:F9}; }}\n}} }}");
    }
}
public sealed class BookmarkStore(string directory)
{
    private readonly string _root = Path.Combine(directory, "research-points");
    public IReadOnlyList<ResearchBookmark> Load()
    {
        if (!Directory.Exists(_root)) return [];
        var result = new List<ResearchBookmark>();
        foreach (string path in Directory.EnumerateFiles(_root, "*.json").Order(StringComparer.Ordinal).Take(10000))
        {
            try { var point = Read(path); if (Path.GetFileNameWithoutExtension(path) == point.Id) result.Add(point); }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        }
        return result.OrderBy(p => p.Category, StringComparer.OrdinalIgnoreCase).ThenBy(p => p.Name, StringComparer.OrdinalIgnoreCase).ToArray();
    }
    public static ResearchBookmark Read(string path)
    {
        if (new FileInfo(path).Length > 65536) throw new IOException("Point file is too large.");
        ResearchBookmark? value;
        try { value = JsonSerializer.Deserialize<ResearchBookmark>(File.ReadAllText(path)); }
        catch (JsonException ex) { throw new IOException("Invalid research point JSON.", ex); }
        if (!ResearchBookmark.Valid(value)) throw new IOException("Invalid research point data."); return value!;
    }
    public void Save(ResearchBookmark value)
    {
        if (!ResearchBookmark.Valid(value)) throw new IOException("Invalid research point data.");
        Directory.CreateDirectory(_root); string path = Path.Combine(_root, value.Id + ".json");
        Export(value, path + ".tmp"); File.Move(path + ".tmp", path, true);
    }
    public static void Export(ResearchBookmark value, string path)
    {
        if (!ResearchBookmark.Valid(value)) throw new IOException("Invalid research point data.");
        File.WriteAllText(path, JsonSerializer.Serialize(value, new JsonSerializerOptions { WriteIndented = true }));
    }
    public ResearchBookmark Import(string path)
    {
        var point = Read(path) with { Id = Guid.NewGuid().ToString("N") }; Save(point); return point;
    }
}
