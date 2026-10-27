using Devil.Game;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

internal static class BookmarkSaveChecks
{
    private static void Check(bool value, string text) { if (!value) throw new Exception(text); }
    private static void Reject(Action action, string message)
    {
        try { action(); } catch (Exception ex) when (ex is IOException or CryptographicException) { return; } throw new Exception(message);
    }
    internal static void Run(string directory)
    {
        string root = Path.Combine(directory, "points-and-saves"); Directory.CreateDirectory(root);
        long epoch = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(); string token = new('a', 32);
        string[] lines = [$"#DEVIL_POINT_V1\t1234\t{token}\t{epoch}\tready\tmap_a\tprofile_a\t1:2\tknown\tcp_a",
            "P\t0\t0\t0\t1\t2\t3\t0\t1\t0\t1\t0\t0\t-1\t0\t0\t1\t-3\t0\t0\t0\t0\t0\t1\t0\t-2",
            "E\t100\t3\ttrigger/interact/exit\tidTrigger\tenabled\t200"];
        var observation = BookmarkObservation.Parse(lines); Check(observation is { Ready: true } && observation.Body.Z == 0 && observation.Eye.Z == 3 && observation.Yaw == 90, "Point lost distinct body/camera/aim");
        Check(BookmarkObservation.Parse(lines.Select(l => l.Replace("P\t0", "P\tNaN")).ToArray()) is null, "Nonfinite point accepted");
        Check(BookmarkObservation.Parse([.. lines, lines[2]]) is null, "Duplicate selected identities accepted");
        var point = new ResearchBookmark("DEVIL_RESEARCH_POINT_V1", Guid.NewGuid().ToString("N"), "Before trick", "Any% Restricted", "Test hypothesis", observation!);
        var store = new BookmarkStore(root); store.Save(point); Check(store.Load().Count == 1, "Point not persisted");
        Check(point.ReturnCommand() == "setviewpos 1 2 3 90.000000 -0.000000" || point.ReturnCommand() == "setviewpos 1 2 3 90.000000 0.000000", "Return command not invariant or used body instead of eye");
        string export = Path.Combine(root, "point.json"); BookmarkStore.Export(point, export); var imported = store.Import(export);
        Check(imported.Id != point.Id && imported.Observation.Entities.Single().Key == "100:3" && store.Load().Count == 2, "Point import lost data or replaced original");
        Reject(() => store.Save(point with { Id = "../escape" }), "Unsafe point ID saved");
        File.WriteAllText(export, JsonSerializer.Serialize(point with { Observation = observation! with { Forward = new(0, 0, 0) } })); Reject(() => BookmarkStore.Read(export), "Invalid saved aim loaded");
        BookmarkObservation.Request(root, 1234, token); Check(File.ReadAllText(Path.Combine(root, "research-point-1234.request")).Contains(token), "Scoped point request absent");
        var location = MakeLocation(root, "10001"); string slot0 = location.SlotPath("GAME-AUTOSAVE0"), slot1 = location.SlotPath("GAME-AUTOSAVE1");
        Reject(() => SteamSaveLocation.Parse(""), "Empty Steam path crashed or was accepted");
        Reject(() => SteamSaveLocation.Parse("relative/path"), "Relative Steam path accepted");
        var original = WriteSlot(slot0, location.Account, "baseline"); var target = WriteSlot(slot1, location.Account, "old target");
        string profile = Path.Combine(location.Remote, "PROFILE"); Directory.CreateDirectory(profile); File.WriteAllText(Path.Combine(profile, "profile.bin"), "profile sentinel");
        File.WriteAllText(Path.Combine(location.AppRoot, "remotecache.vdf"), "cloud sentinel");
        IReadOnlyList<string> running = []; var vault = new PracticeSaveVault(root, () => running);
        var saved = vault.Capture(location.Remote, "GAME-AUTOSAVE0", "Before arena", "Any% Restricted", "map_a");
        Check(saved.Files.Count == 4 && saved.Files.Count(f => f.Name.EndsWith("-BACKUP")) == 2 && vault.Verify(saved.Id).Id == saved.Id, "Full archive lost backup files or failed verification");
        using (var writer = new FileStream(Path.Combine(slot0, "game.details"), FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite | FileShare.Delete))
        {
            Check(vault.Inspect(location.Remote, "GAME-AUTOSAVE0").Authenticated, "Read-only inspection blocks an existing writer");
            Reject(() => vault.Capture(location.Remote, "GAME-AUTOSAVE0", "Concurrent writer", "Research"), "Archive capture ignored a concurrent writer");
        }
        running = ["steam"]; Reject(() => vault.Capture(location.Remote, "GAME-AUTOSAVE0", "bad", "Research"), "Running Steam allowed archive capture"); running = [];
        Reject(() => vault.PlanRestore(saved.Id, location.Remote, "GAME-AUTOSAVE1", false), "Cloud confirmation missing");
        Reject(() => vault.PlanRestore(saved.Id, location.Remote, "GAME-AUTOSAVE2", true), "Restore created an empty/unoccupied slot");
        WriteSlot(location.SlotPath("DLC1-AUTOSAVE3"), location.Account, "TAG1"); Reject(() => vault.PlanRestore(saved.Id, location.Remote, "DLC1-AUTOSAVE3", true), "Campaign families mixed");
        var foreign = MakeLocation(root, "20002"); WriteSlot(foreign.SlotPath("GAME-AUTOSAVE1"), foreign.Account, "foreign");
        Reject(() => vault.PlanRestore(saved.Id, foreign.Remote, "GAME-AUTOSAVE1", true), "Different owner allowed without resigning");
        var plan = vault.PlanRestore(saved.Id, location.Remote, "GAME-AUTOSAVE1", true);
        File.WriteAllText(Path.Combine(slot1, "game.details"), "changed after review"); Reject(() => vault.Restore(plan), "Changed target overwritten");
        RestoreFiles(slot1, target); plan = vault.PlanRestore(saved.Id, location.Remote, "GAME-AUTOSAVE1", true);
        string archiveSlot = Path.Combine(root, "practice-saves", saved.Id, "slot"), file = Path.Combine(archiveSlot, "game.details");
        byte[] tampered = File.ReadAllBytes(file); tampered[^1] ^= 1; File.WriteAllBytes(file, tampered); Reject(() => vault.Restore(plan), "Tampered archive restored"); File.WriteAllBytes(file, original["game.details"]);
        var fault = new PracticeSaveVault(root, () => [], stage => { if (stage == "original_moved") throw new IOException("Simulated rename failure"); });
        Reject(() => fault.Restore(plan), "Injected failure not observed"); Check(Same(slot1, target), "Failed replacement did not roll original slot back");
        var race = new PracticeSaveVault(root, () => running, stage => { if (stage == "original_moved") running = ["DOOMEternalx64vk"]; });
        Reject(() => race.Restore(plan), "Game starting mid-restore not detected"); Check(Same(slot1, target), "Process race did not roll back original"); running = [];
        var result = vault.Restore(plan); Check(Same(slot1, original) && Same(result.OriginalSlot, target), "Restored files or retained original differ");
        Check(File.ReadAllText(Path.Combine(profile, "profile.bin")) == "profile sentinel" && File.ReadAllText(Path.Combine(location.AppRoot, "remotecache.vdf")) == "cloud sentinel", "Restore altered PROFILE or Steam metadata");
        Check(File.ReadAllText(Path.Combine(result.Evidence, "PROFILE", "profile.bin")) == "profile sentinel" && File.Exists(Path.Combine(result.Evidence, "app-metadata", "remotecache.vdf")), "Recovery evidence missing");
        Check(!Directory.EnumerateDirectories(location.Remote).Any(p => Path.GetFileName(p).StartsWith(".devil")), "Staging left non-slot directories inside remote");
        string exported = Path.Combine(root, "exported-archive"); vault.Export(saved.Id, exported);
        Reject(() => vault.Import(exported, foreign.Account), "Foreign-account archive imported");
        var copy = vault.Import(exported, location.Account); Check(copy.Id != saved.Id && vault.Verify(copy.Id).Files.Count == 4, "Archive import/export damaged data");
        string manifest = Path.Combine(exported, "manifest.json"); var bad = saved with { Files = [.. saved.Files.Take(3), new("../escape", 1, new('0', 64))] };
        File.WriteAllText(manifest, JsonSerializer.Serialize(bad)); Reject(() => vault.Import(exported, location.Account), "Manifest path traversal accepted");
        // Corruption repair always produces a separate archive, preserving the
        // exact original ciphertext and correctly rebinding backup filenames.
        var broken = original.ToDictionary(f => f.Key, f => f.Value.ToArray()); broken["game.details"][^1] ^= 1; RestoreFiles(slot0, broken);
        Check(vault.Inspect(location.Remote, "GAME-AUTOSAVE0").CanRecover, "Authenticated backup pair not offered for recovery");
        var recovered = vault.RecoverBackupPair(location.Remote, "GAME-AUTOSAVE0", "Recovered mission start", "Recovery");
        Check(recovered.FromBackupPair && Same(slot0, broken), "Recovery altered live slot");
        byte[] recoveredPrimary = File.ReadAllBytes(Path.Combine(root, "practice-saves", recovered.Id, "slot", "game.details"));
        Check(SteamSaveContainer.Open(recoveredPrimary, location.Account, "game.details").SequenceEqual(SteamSaveContainer.Open(original["game.details-BACKUP"], location.Account, "game.details-BACKUP")), "Recovered primary does not match backup plaintext");
        Check(!SteamSaveContainer.Verified(original["game.details-BACKUP"], location.Account, "game.details"), "Backup ciphertext unexpectedly valid under primary filename");
        broken["game_duration.dat-BACKUP"][^1] ^= 1; RestoreFiles(slot0, broken);
        Reject(() => vault.RecoverBackupPair(location.Remote, "GAME-AUTOSAVE0", "Bad recovery", "Recovery"), "Broken backup pair used for recovery");
        Console.WriteLine("PASS: research point parsing/persistence/import, distinct body/camera/aim and scoped identities; complete authenticated save archives, backup-pair recovery, account/family/Cloud/process guards, tamper rejection, rename-failure rollback and profile/Steam preservation.");
    }
    private static SteamSaveLocation MakeLocation(string root, string account)
    {
        string remote = Path.Combine(root, "Steam", "userdata", account, "782330", "remote"); Directory.CreateDirectory(remote); return SteamSaveLocation.Parse(remote);
    }
    private static Dictionary<string, byte[]> WriteSlot(string path, string account, string prefix)
    {
        var result = PracticeSaveVault.RequiredFiles.ToDictionary(name => name, name => SteamSaveContainer.Seal(Encoding.UTF8.GetBytes(prefix + ":" + name), account, name)); RestoreFiles(path, result); return result;
    }
    private static void RestoreFiles(string path, IReadOnlyDictionary<string, byte[]> files) { Directory.CreateDirectory(path); foreach (var file in files) File.WriteAllBytes(Path.Combine(path, file.Key), file.Value); }
    private static bool Same(string path, IReadOnlyDictionary<string, byte[]> files) => files.All(f => File.ReadAllBytes(Path.Combine(path, f.Key)).SequenceEqual(f.Value));
}
