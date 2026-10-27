using Devil.Configuration;
using Devil.Game;

internal static class MotionChecks
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    internal static void Run(string temporary)
    {
        long epoch = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        string header = $"#DEVIL_MOTION_V1\t1234\t{epoch + 1000}\tlive\tmap_a\tprofile_a\t1:2";
        string Row(int i, long time, int segment = 1) => FormattableString.Invariant($"S\t{i}\t{time}\t{segment}\t1\t2\t3\t3\t4\t0\t-0.4\t-0.4\t0\t0.4\t0.4\t1.8\t1\t0\t0.3\t1\t1\t2\t3\t0\t0\t1\t0\t0\t1\t7\t2\t2\t1.5");
        var lines = new[] { header, Row(1, epoch), Row(2, epoch + 50), Row(3, epoch + 100), Row(4, epoch + 1000, 2) };
        var snapshot = MotionSnapshot.Parse(lines) ?? throw new Exception("Movement protocol rejected valid rows");
        Check(snapshot.Samples.Count == 4 && snapshot.Samples[0].Speed == 5 && snapshot.Samples[0].HorizontalSpeed == 5 && snapshot.Samples[0].Slope == 0, "Physical speed/support angle changed");
        Check(MotionSnapshot.Parse([header, lines[1].Replace("\t3\t4\t0\t", "\tNaN\t4\t0\t")]) is null, "NaN entered movement capture");
        Check(MotionSnapshot.Parse([header, lines[1], lines[1]]) is null && MotionSnapshot.Parse([header, lines[1] + "\t0"]) is null, "Duplicate/partial movement rows accepted");
        Check(MotionSnapshot.Parse([header, lines[1].Replace("\t0.4\t0.4\t1.8", "\t-0.4\t0.4\t1.8")]) is null, "Inverted collider bounds accepted");
        Check(snapshot.Fresh(epoch) && !snapshot.Fresh(epoch + 5000), "Motion freshness boundary failed");
        var attempt = new MotionAttempt { Name = "Attempt, \"one\"", Process = 1234, Map = "map_a", Profile = "profile_a", Started = epoch, Samples = snapshot.Samples.ToList(), Outcome = AttemptOutcome.Success, Finished = true };
        Check(attempt.Valid && attempt.At(20)?.Sequence == 1 && attempt.At(500) is null && attempt.At(1000)?.Segment == 2 && attempt.At(2000) is null, "Comparison extrapolated or crossed a sampling gap");
        var tilted = snapshot.Samples[0] with { Normal = new(0, MathF.Sqrt(.5f), MathF.Sqrt(.5f)) };
        Check(Math.Abs(tilted.Slope!.Value - 45) < .001f && (tilted with { Contact = false }).Slope is null, "Support slope did not use gravity/contact availability");
        var plane = SupportPlane.From(snapshot.Samples[0])!;
        Check(plane.Slope == 0 && plane.Cuts[0].TangentAngle is null && plane.Cuts[1].TangentAngle == 0 && plane.Cuts[2].TangentAngle == 0, "Flat floor fabricated an XY tangent line");
        var incline = SupportPlane.From(tilted)!;
        Check(Math.Abs(incline.Cuts[2].TangentAngle!.Value + 45) < .001f && incline.Cuts[1].TangentAngle == 0, "Signed YZ tangent is not perpendicular to projected normal");
        var reversed = SupportPlane.From(tilted with { Normal = new(0, -MathF.Sqrt(.5f), -MathF.Sqrt(.5f)) })!;
        Check(Math.Abs(incline.Cuts[2].TangentAngle!.Value - reversed.Cuts[2].TangentAngle!.Value) < .001f, "Plane cuts changed when normal sign changed");
        var vertical = SupportPlane.From(tilted with { Normal = new(1, 0, 0) })!;
        Check(vertical.Cuts[2].TangentAngle is null && vertical.Cuts[1].TangentAngle == 90 && SupportPlane.From(tilted with { Contact = false }) is null, "Vertical/coincident/no-contact projections failed");
        Check(SupportPlane.From(tilted with { Up = tilted.Normal })!.Slope < .01f, "Surface slope ignored rotated gravity");
        string directory = Path.Combine(temporary, "movement"); Directory.CreateDirectory(directory); var store = new MotionStore(directory);
        store.Save(attempt); var reloaded = store.Load().Single();
        Check(reloaded.Samples.Count == 4 && reloaded.Outcome == AttemptOutcome.Success && reloaded.Samples[0].Collider, "Attempt JSON lost physics or result data");
        string originalId = attempt.Id; attempt.Id = "../unsafe";
        bool refused = false; try { store.Save(attempt); } catch (IOException) { refused = true; }
        Check(refused, "Attempt identity escaped its storage directory"); attempt.Id = originalId;
        var second = new MotionAttempt { Name = "second", Process = 1234, Map = "map_a", Profile = "profile_a", Started = epoch, Samples = snapshot.Samples.ToList(), Outcome = AttemptOutcome.Success, Finished = true };
        var otherMap = new MotionAttempt { Name = "other", Process = 1234, Map = "map_b", Profile = "profile_a", Started = epoch, Samples = snapshot.Samples.ToList() };
        MotionStore.Overlay(directory, 1234, [attempt, second, otherMap], snapshot);
        var overlay = File.ReadAllLines(Path.Combine(directory, "motion-overlays.tsv"));
        Check(overlay.Length == 9 && overlay.Skip(1).All(line => line.Split('\t').Length == 6) && overlay[1].StartsWith("0\t0\t") && overlay[5].StartsWith("1\t0\t"), "Same-color attempts merged identities or foreign-map attempt was overlaid");
        string csv = Path.Combine(directory, "attempt.csv"); MotionStore.ExportCsv(attempt, csv); string[] exported = File.ReadAllLines(csv);
        Check(exported.Length == 5 && exported[1].Contains("\"Attempt, \"\"one\"\"\"") && exported[0].Contains("skin_gap") && exported[0].Contains("contact_x"), "CSV omitted samples, contact/clearance columns or quoting");
        File.WriteAllLines(Path.Combine(directory, "motion-1234.tsv"), lines);
        Check(MotionSnapshot.Read(directory, 1234)?.Process == 1234 && MotionSnapshot.Read(directory, 4321) is null, "Movement capture crossed game PID");
        MotionSnapshot.Request(directory, 1234); Check(File.Exists(Path.Combine(directory, "motion-1234.request")), "Motion request not published");
        var settings = new DevilSettings(); Check(LabCommands.Apply(settings, ["lab_player", "1"], 1234, 1) && settings.ShowPlayerCollider && settings.MotionProcess == 1234, "Player visibility cvar transport failed");
        Check(LabCommands.Apply(settings, ["lab_attempts", "0"], 1234, 1) && !settings.ShowMotionPaths, "Comparison visibility cvar transport failed");
        Check(LabCommands.Apply(settings, ["lab_surface", "1"], 1234, 1) && settings.ShowSupportHud, "Surface HUD cvar transport failed");
        var settingsStore = new SettingsStore(Path.Combine(directory, "settings")); settingsStore.Save(settings);
        Check(settingsStore.Load().ShowPlayerCollider && File.ReadAllText(settingsStore.ViewerSettingsPath).Contains("MotionProcess=1234"), "Motion settings not persisted/exported");
        Console.WriteLine("PASS: motion parsing, physical metrics, gap-aware comparison, attempts/save/load, outcomes, overlay scope/identity, CSV and visibility transport.");
    }
}
