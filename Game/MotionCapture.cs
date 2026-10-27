using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Devil.Game;

public sealed record MotionVector(float X, float Y, float Z)
{
    [JsonIgnore] public float Length => MathF.Sqrt(X * X + Y * Y + Z * Z);
    [JsonIgnore] public bool Finite => float.IsFinite(X) && float.IsFinite(Y) && float.IsFinite(Z) && Math.Abs(X) < 1_000_000 && Math.Abs(Y) < 1_000_000 && Math.Abs(Z) < 1_000_000;
    public static float Distance(MotionVector a, MotionVector b) => new MotionVector(a.X - b.X, a.Y - b.Y, a.Z - b.Z).Length;
}
public sealed record MotionSample(long Sequence, long Epoch, ulong Segment, MotionVector Position, MotionVector Velocity,
    MotionVector Minimum, MotionVector Maximum, bool Collider, int Shape, float Radius, bool Contact, MotionVector ContactPoint,
    MotionVector Normal, MotionVector Up, uint TargetId, uint TargetGeneration, float? OriginDistance, float? SkinGap)
{
    [JsonIgnore] public float Speed => Velocity.Length;
    [JsonIgnore] public float HorizontalSpeed => Up.Length > .9f ? MathF.Sqrt(Math.Max(0, Speed * Speed - MathF.Pow(Velocity.X * Up.X + Velocity.Y * Up.Y + Velocity.Z * Up.Z, 2))) : MathF.Sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y);
    [JsonIgnore] public float? Slope => Contact && Up.Length > .9f ? MathF.Acos(Math.Clamp(Normal.X * Up.X + Normal.Y * Up.Y + Normal.Z * Up.Z, -1, 1)) * 180 / MathF.PI : null;
    [JsonIgnore] public bool Valid => Sequence > 0 && Epoch > 0 && Position is { Finite: true } && Velocity is { Finite: true }
        && Minimum is { Finite: true } && Maximum is { Finite: true } && ContactPoint is { Finite: true } && Normal is { Finite: true } && Up is { Finite: true }
        && Maximum.X > Minimum.X && Maximum.Y > Minimum.Y && Maximum.Z > Minimum.Z
        && Maximum.X - Minimum.X <= 100 && Maximum.Y - Minimum.Y <= 100 && Maximum.Z - Minimum.Z <= 100
        && Shape is >= 0 and <= 8 && float.IsFinite(Radius) && Radius is >= 0 and < 10
        && (!Contact || Normal.Length is > .9f and < 1.1f) && (OriginDistance is null or >= 0) && (OriginDistance is null || float.IsFinite(OriginDistance.Value))
        && (SkinGap is null || float.IsFinite(SkinGap.Value));
}
public sealed record MotionSnapshot(int Process, long Captured, string Phase, string Map, string Profile, string Identity, IReadOnlyList<MotionSample> Samples)
{
    public bool Fresh(long now) => Captured <= now + 5000 && Captured >= now - 2000;
    public static void Request(string directory, int process)
    {
        if (process <= 0) return;
        Directory.CreateDirectory(directory); string path = Path.Combine(directory, $"motion-{process}.request"), temporary = path + ".tmp";
        File.WriteAllText(temporary, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds().ToString(CultureInfo.InvariantCulture)); File.Move(temporary, path, true);
    }
    public static MotionSnapshot? Read(string directory, int process)
    {
        string file = Path.Combine(directory, $"motion-{process}.tsv");
        if (process <= 0 || !File.Exists(file) || new FileInfo(file).Length > 1_000_000) return null;
        var snapshot = Parse(File.ReadAllLines(file)); return snapshot?.Process == process ? snapshot : null;
    }
    public static MotionSnapshot? Parse(string[] lines)
    {
        if (lines.Length is < 1 or > 257 || lines.Any(line => line.Length > 4096)) return null;
        string[] header = lines[0].Split('\t');
        if (header.Length != 7 || header[0] != "#DEVIL_MOTION_V1" || !int.TryParse(header[1], out int process) || process <= 0
            || !long.TryParse(header[2], out long captured) || captured <= 0 || header[4].Length > 512 || header[5].Length > 128 || header[6].Length > 80
            || header[3] is not ("live" or "paused" or "loading" or "metadata_unavailable" or "read_error")) return null;
        var samples = new List<MotionSample>();
        foreach (string line in lines.Skip(1))
        {
            string[] cells = line.Split('\t');
            if (cells.Length != 33 || cells[0] != "S" || !long.TryParse(cells[1], out long sequence) || !long.TryParse(cells[2], out long epoch)
                || !ulong.TryParse(cells[3], out ulong segment) || !int.TryParse(cells[17], out int shape)
                || !uint.TryParse(cells[29], out uint id) || !uint.TryParse(cells[30], out uint generation)
                || cells[16] is not ("0" or "1") || cells[19] is not ("0" or "1")) return null;
            float Number(int i) => float.TryParse(cells[i], NumberStyles.Float, CultureInfo.InvariantCulture, out float value) ? value : float.NaN;
            MotionVector Vector(int i) => new(Number(i), Number(i + 1), Number(i + 2));
            float distance = Number(31); float? gap = cells[32].Length == 0 ? null : Number(32);
            if (!float.IsFinite(distance)) return null;
            var sample = new MotionSample(sequence, epoch, segment, Vector(4), Vector(7), Vector(10), Vector(13), cells[16] == "1", shape, Number(18), cells[19] == "1",
                Vector(20), Vector(23), Vector(26), id, generation, distance < 0 ? null : distance, gap);
            if (!sample.Valid || epoch > captured + 1000 || (samples.Count > 0 && (samples[^1].Sequence >= sequence || samples[^1].Epoch > epoch))) return null;
            samples.Add(sample);
        }
        return new(process, captured, header[3], header[4], header[5], header[6], samples);
    }
}
public enum AttemptOutcome { Unmarked, Success, Failure }
public sealed class MotionAttempt
{
    public string Format { get; set; } = "DEVIL_ATTEMPT_V1";
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string Name { get; set; } = "Attempt";
    public AttemptOutcome Outcome { get; set; }
    public int Process { get; set; }
    public string Map { get; set; } = "";
    public string Profile { get; set; } = "";
    public long Started { get; set; }
    public bool Finished { get; set; }
    public List<MotionSample> Samples { get; set; } = [];
    [JsonIgnore] public long Duration => Samples.Count == 0 ? 0 : Samples[^1].Epoch - Started;
    public bool Compatible(MotionSnapshot? snapshot) => snapshot is not null && snapshot.Map == Map && snapshot.Profile == Profile;
    public MotionSample? At(long elapsed)
    {
        long epoch = Started + elapsed; int lo = 0, hi = Samples.Count;
        while (lo < hi) { int mid = (lo + hi) / 2; if (Samples[mid].Epoch < epoch) lo = mid + 1; else hi = mid; }
        var candidates = new[] { lo - 1, lo }.Where(i => i >= 0 && i < Samples.Count).Select(i => Samples[i]).OrderBy(sample => Math.Abs(sample.Epoch - epoch));
        var nearest = candidates.FirstOrDefault(); return nearest is not null && Math.Abs(nearest.Epoch - epoch) <= 75 ? nearest : null;
    }
    public override string ToString() => $"{Name} · {Outcome} · {Duration / 1000f:0.0}s";
    public bool Valid => Format == "DEVIL_ATTEMPT_V1" && Guid.TryParseExact(Id, "N", out _) && Name is { Length: > 0 and <= 80 }
        && Map is { Length: > 0 and <= 512 } && Profile is { Length: > 0 and <= 128 } && Process > 0 && Started > 0 && Enum.IsDefined(Outcome)
        && Samples is { Count: > 0 and <= 24000 } && Samples.All(sample => sample is not null && sample.Valid && sample.Epoch >= Started && sample.Epoch - Started <= 7_200_000)
        && Samples.Zip(Samples.Skip(1)).All(pair => pair.First.Epoch < pair.Second.Epoch && pair.First.Sequence < pair.Second.Sequence && pair.First.Segment <= pair.Second.Segment);
}
public sealed class MotionStore(string settingsDirectory)
{
    private readonly string _directory = Path.Combine(settingsDirectory, "attempts");
    public void Save(MotionAttempt attempt)
    {
        if (!attempt.Valid) throw new IOException("Attempt data is invalid.");
        Directory.CreateDirectory(_directory); string path = Path.Combine(_directory, attempt.Id + ".json"), temporary = path + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(attempt)); File.Move(temporary, path, true);
    }
    public IReadOnlyList<MotionAttempt> Load()
    {
        if (!Directory.Exists(_directory)) return [];
        var result = new List<MotionAttempt>();
        foreach (var file in new DirectoryInfo(_directory).EnumerateFiles("*.json").OrderByDescending(file => file.LastWriteTimeUtc).Take(64))
        {
            if (file.Length > 32_000_000) continue;
            try { var attempt = JsonSerializer.Deserialize<MotionAttempt>(File.ReadAllText(file.FullName)); if (attempt is { Valid: true } && file.Name == attempt.Id + ".json" && result.Sum(item => item.Samples.Count) + attempt.Samples.Count <= 200_000) result.Add(attempt); }
            catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException) { }
        }
        return result.OrderByDescending(attempt => attempt.Started).ToArray();
    }
    public static int ColorIndex(MotionAttempt attempt, int index) => attempt.Outcome switch { AttemptOutcome.Success => 0, AttemptOutcome.Failure => 1, _ => 2 + index % 4 };
    public static void Overlay(string directory, int process, IReadOnlyList<MotionAttempt> attempts, MotionSnapshot? snapshot)
    {
        Directory.CreateDirectory(directory); string path = Path.Combine(directory, "motion-overlays.tsv"), temporary = path + ".tmp";
        using (var writer = new StreamWriter(temporary, false, new UTF8Encoding(false)))
        {
            writer.WriteLine($"#DEVIL_MOTION_OVERLAY_V1\t{process}\t{snapshot?.Map}\t{snapshot?.Profile}");
            int index = 0;
            foreach (var attempt in attempts.Where(attempt => attempt.Compatible(snapshot)).Take(6))
            {
                int stride = Math.Max(1, (int)Math.Ceiling(attempt.Samples.Count / 7900d));
                for (int i = 0; i < attempt.Samples.Count; ++i) if (i % stride == 0 || i == attempt.Samples.Count - 1)
                {
                    var sample = attempt.Samples[i]; var p = sample.Position;
                    writer.WriteLine(FormattableString.Invariant($"{index}\t{ColorIndex(attempt, index)}\t{sample.Segment}\t{p.X:R}\t{p.Y:R}\t{p.Z:R}"));
                }
                ++index;
            }
        }
        File.Move(temporary, path, true);
    }
    public static void ExportCsv(MotionAttempt attempt, string path)
    {
        static string Cell(string value) => '"' + value.Replace("\"", "\"\"") + '"';
        using var writer = new StreamWriter(path, false, new UTF8Encoding(true));
        writer.WriteLine("attempt,outcome,map,profile,elapsed_ms,segment,x,y,z,vx,vy,vz,speed,shape_index,width,depth,height,rounded_hull,convex_radius,support_contact,contact_x,contact_y,contact_z,normal_x,normal_y,normal_z,slope_deg,target_id,target_generation,origin_to_boundary,skin_gap");
        foreach (var s in attempt.Samples)
        {
            string N(float? value) => value?.ToString("R", CultureInfo.InvariantCulture) ?? "";
            writer.WriteLine(string.Join(',', Cell(attempt.Name), attempt.Outcome.ToString(), Cell(attempt.Map), Cell(attempt.Profile), (s.Epoch - attempt.Started).ToString(CultureInfo.InvariantCulture), s.Segment.ToString(CultureInfo.InvariantCulture),
                N(s.Position.X), N(s.Position.Y), N(s.Position.Z), N(s.Velocity.X), N(s.Velocity.Y), N(s.Velocity.Z), N(s.Speed), s.Shape.ToString(CultureInfo.InvariantCulture),
                N(s.Maximum.X - s.Minimum.X), N(s.Maximum.Y - s.Minimum.Y), N(s.Maximum.Z - s.Minimum.Z), s.Collider ? "1" : "0", N(s.Radius), s.Contact ? "1" : "0",
                s.Contact ? N(s.ContactPoint.X) : "", s.Contact ? N(s.ContactPoint.Y) : "", s.Contact ? N(s.ContactPoint.Z) : "", s.Contact ? N(s.Normal.X) : "", s.Contact ? N(s.Normal.Y) : "", s.Contact ? N(s.Normal.Z) : "", N(s.Slope),
                s.OriginDistance is not null ? s.TargetId.ToString(CultureInfo.InvariantCulture) : "", s.OriginDistance is not null ? s.TargetGeneration.ToString(CultureInfo.InvariantCulture) : "", N(s.OriginDistance), N(s.SkinGap)));
        }
    }
}
