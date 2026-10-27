using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;

namespace Devil.Game;

public sealed record SteamSaveLocation(string Remote, string Account)
{
    public string AppRoot => Directory.GetParent(Remote)!.FullName;
    public static SteamSaveLocation Parse(string remote)
    {
        if (string.IsNullOrWhiteSpace(remote) || !Path.IsPathFullyQualified(remote)) throw new IOException("Choose an absolute Steam save-folder path first.");
        string path;
        try { path = Path.TrimEndingDirectorySeparator(Path.GetFullPath(remote)); }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException) { throw new IOException("Invalid Steam save-folder path.", ex); }
        var folder = new DirectoryInfo(path); var app = folder.Parent; var user = app?.Parent;
        if (!folder.Name.Equals("remote", StringComparison.OrdinalIgnoreCase) || app?.Name != "782330" || user?.Parent?.Name != "userdata"
            || !uint.TryParse(user.Name, NumberStyles.None, CultureInfo.InvariantCulture, out uint account) || account == 0 || !folder.Exists)
            throw new IOException("Select Steam/userdata/<account>/782330/remote for DOOM Eternal.");
        PracticeSaveVault.NoLinks(path);
        return new(path, (76561197960265728UL + account).ToString(CultureInfo.InvariantCulture));
    }
    public string SlotPath(string slot)
    {
        if (!PracticeSaveVault.ValidSlot(slot)) throw new IOException("Unsupported save slot name.");
        string path = Path.Combine(Remote, slot); PracticeSaveVault.NoLinks(path); return path;
    }
    public static IReadOnlyList<string> Discover()
    {
        var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam") };
        try { if (Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) is string path) roots.Add(path); }
        catch (Exception ex) when (ex is UnauthorizedAccessException or System.Security.SecurityException) { }
        var result = new List<string>();
        foreach (string root in roots)
        {
            try
            {
                string users = Path.Combine(root, "userdata"); if (!Directory.Exists(users)) continue;
                foreach (string user in Directory.EnumerateDirectories(users).Take(100))
                {
                    string remote = Path.Combine(user, "782330", "remote"); if (Directory.Exists(remote)) { _ = Parse(remote); result.Add(remote); }
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        }
        return result;
    }
}
public sealed record SaveFileEntry(string Name, long Length, string Sha256);
public sealed record PracticeSave(string Format, string Id, string Name, string Category, string Map, string Build, long Captured,
    string Account, string SourceSlot, bool FromBackupPair, IReadOnlyList<SaveFileEntry> Files)
{
    public override string ToString() => $"{Category} · {Name} · {SourceSlot}{(FromBackupPair ? " · recovered backup pair" : "")}";
}
public sealed record SaveFileHealth(string Name, string Status, long Length);
public sealed record SaveHealth(IReadOnlyList<SaveFileHealth> Files, bool Complete, bool Authenticated, bool CanRecover)
{
    public override string ToString() => string.Join(Environment.NewLine, Files.Select(f => $"{f.Name}: {f.Status} ({f.Length:N0} bytes)"))
        + "\nAuthentication verifies the encrypted container, not level/script consistency. Failed authentication can also mean a different account or unsupported format.";
}
public sealed record SaveRestorePlan(string ArchiveId, string ManifestHash, string Remote, string Slot, string TargetHash, string Description);
public sealed record SaveRestoreResult(string OriginalSlot, string Evidence, string Message);

// Steam save container framing documented by DOOMSaveManager/Crypto.cs. Uses
// .NET AES-GCM, independently implemented here; no third-party code is bundled.
public static class SteamSaveContainer
{
    private static byte[] Aad(string account, string name) => Encoding.UTF8.GetBytes(account + "MANCUBUS" + name);
    public static byte[] Open(byte[] data, string account, string name)
    {
        if (data.Length < 28) throw new CryptographicException("Truncated encrypted save container.");
        byte[] aad = Aad(account, name), plain = new byte[data.Length - 28];
        using var aes = new AesGcm(SHA256.HashData(aad).AsSpan(0, 16), 16);
        aes.Decrypt(data.AsSpan(0, 12), data.AsSpan(12, data.Length - 28), data.AsSpan(data.Length - 16, 16), plain, aad); return plain;
    }
    public static byte[] Seal(byte[] plain, string account, string name)
    {
        byte[] aad = Aad(account, name), result = new byte[plain.Length + 28]; RandomNumberGenerator.Fill(result.AsSpan(0, 12));
        using var aes = new AesGcm(SHA256.HashData(aad).AsSpan(0, 16), 16);
        aes.Encrypt(result.AsSpan(0, 12), plain, result.AsSpan(12, plain.Length), result.AsSpan(12 + plain.Length, 16), aad); return result;
    }
    public static bool Verified(byte[] data, string account, string name)
    {
        try { _ = Open(data, account, name); return true; } catch (CryptographicException) { return false; }
    }
}
public sealed partial class PracticeSaveVault
{
    public static readonly string[] RequiredFiles = ["game.details", "game.details-BACKUP", "game_duration.dat", "game_duration.dat-BACKUP"];
    public static readonly string[] Slots = ["GAME-AUTOSAVE0", "GAME-AUTOSAVE1", "GAME-AUTOSAVE2", "DLC1-AUTOSAVE3", "DLC1-AUTOSAVE4", "DLC1-AUTOSAVE5", "DLC2-AUTOSAVE6", "DLC2-AUTOSAVE7", "DLC2-AUTOSAVE8"];
    private readonly string _root;
    private readonly Func<IReadOnlyList<string>> _running;
    private readonly Action<string>? _step;
    public PracticeSaveVault(string directory, Func<IReadOnlyList<string>>? running = null, Action<string>? step = null)
    {
        _root = Path.Combine(directory, "practice-saves"); _running = running ?? RunningProcesses; _step = step;
    }
    private static IReadOnlyList<string> RunningProcesses()
    {
        var result = new List<string>();
        foreach (string name in new[] { "DOOMEternalx64vk", "DOOMEternalx64", "DOOMEternal", "steam", "steamwebhelper" })
            foreach (var process in Process.GetProcessesByName(name)) { using (process) result.Add(name); }
        return result;
    }
    public void RequireClosed()
    {
        var running = _running(); if (running.Count > 0) throw new IOException("Close DOOM Eternal and Steam first. Running: " + string.Join(", ", running.Distinct()));
    }
    public static bool ValidSlot(string slot) => Slots.Contains(slot, StringComparer.Ordinal);
    private static string Family(string slot) => slot.Split('-')[0];
    internal static void NoLinks(string path)
    {
        for (var item = new DirectoryInfo(Path.GetFullPath(path)); item is not null; item = item.Parent)
        {
            FileAttributes attributes;
            try { attributes = File.GetAttributes(item.FullName); }
            catch (Exception ex) when (ex is FileNotFoundException or DirectoryNotFoundException) { continue; }
            if ((attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Linked/junction paths are not supported for save operations: " + item.FullName);
        }
    }
    private static bool ValidName(string name) => name.Length is > 0 and <= 120 && name != "." && name != ".."
        && name == Path.GetFileName(name) && !name.Any(c => char.IsControl(c) || c is '/' or '\\' or ':' || Path.GetInvalidFileNameChars().Contains(c));
    private static string Hash(byte[] data) => Convert.ToHexStringLower(SHA256.HashData(data));
    private static string Fingerprint(IReadOnlyDictionary<string, byte[]> files) => Hash(Encoding.UTF8.GetBytes(string.Join('\n', files.OrderBy(f => f.Key, StringComparer.Ordinal).Select(f => f.Key + ":" + Hash(f.Value)))));
    private static Dictionary<string, byte[]> ReadSet(string directory, bool allowWriters = false)
    {
        NoLinks(directory); if (!Directory.Exists(directory)) throw new IOException("Save folder does not exist.");
        if (Directory.EnumerateDirectories(directory).Any()) throw new IOException("Unexpected subfolders in save slot; this layout is unsupported.");
        string[] paths = Directory.GetFiles(directory); if (paths.Length > 256) throw new IOException("Too many files in save folder.");
        var result = new Dictionary<string, byte[]>(StringComparer.Ordinal); long total = 0;
        var streams = new List<FileStream>();
        try
        {
            // Archive/restore operations run offline and deny concurrent
            // writers. Read-only inspection must not block a running game's
            // autosave writer; its result is explicitly only an observation.
            foreach (string path in paths.Order(StringComparer.Ordinal))
            {
                string name = Path.GetFileName(path); NoLinks(path);
                if (!ValidName(name) || result.Keys.Any(n => n.Equals(name, StringComparison.OrdinalIgnoreCase))) throw new IOException("Invalid or ambiguous save filename.");
                var before = new FileInfo(path); long previousLength = before.Length; DateTime previousWrite = before.LastWriteTimeUtc;
                var stream = new FileStream(path, FileMode.Open, FileAccess.Read, allowWriters ? FileShare.ReadWrite | FileShare.Delete : FileShare.Read); streams.Add(stream);
                if (stream.Length > 64_000_000 || (total += stream.Length) > 128_000_000) throw new IOException("Save folder exceeds supported size.");
                byte[] bytes = new byte[checked((int)stream.Length)]; stream.ReadExactly(bytes); result.Add(name, bytes);
                if (allowWriters) { var after = new FileInfo(path); if (after.Length != previousLength || after.LastWriteTimeUtc != previousWrite) throw new IOException("Save file changed during inspection. Close game/Steam and retry."); }
            }
            if (Directory.GetFiles(directory).Select(Path.GetFileName).Order(StringComparer.Ordinal).SequenceEqual(result.Keys.Order(StringComparer.Ordinal)) == false)
                throw new IOException("Save folder changed while reading. Close the game and Steam and retry.");
            return result;
        }
        finally { foreach (var stream in streams) stream.Dispose(); }
    }
    private static SaveHealth Health(IReadOnlyDictionary<string, byte[]> files, string account)
    {
        var health = RequiredFiles.Select(name => !files.TryGetValue(name, out var data) ? new SaveFileHealth(name, "Missing (filename case must match)", 0)
            : new SaveFileHealth(name, SteamSaveContainer.Verified(data, account, name) ? "Authenticated" : "Authentication failed / wrong account / unsupported format", data.Length)).ToArray();
        bool Good(string name) => health.Single(h => h.Name == name).Status == "Authenticated";
        return new(health, RequiredFiles.All(files.ContainsKey), health.All(h => h.Status == "Authenticated"),
            Good("game.details-BACKUP") && Good("game_duration.dat-BACKUP") && (!Good("game.details") || !Good("game_duration.dat")));
    }
    public SaveHealth Inspect(string remote, string slot)
    {
        var location = SteamSaveLocation.Parse(remote); return Health(ReadSet(location.SlotPath(slot), allowWriters: true), location.Account);
    }
    private static void WriteSet(string directory, IReadOnlyDictionary<string, byte[]> files)
    {
        NoLinks(directory); Directory.CreateDirectory(directory);
        foreach (var item in files) { if (!ValidName(item.Key)) throw new IOException("Invalid filename."); File.WriteAllBytes(Path.Combine(directory, item.Key), item.Value); }
    }
    private string ArchivePath(string id)
    {
        if (!LevelStateSnapshot.ValidToken(id)) throw new IOException("Invalid archive identifier.");
        string path = Path.Combine(_root, id); NoLinks(path); return path;
    }
    private PracticeSave Store(IReadOnlyDictionary<string, byte[]> files, string account, string slot, string name, string category, string map, string build, bool recovered)
    {
        if (!Health(files, account).Authenticated) throw new IOException("All four current/backup containers must authenticate before a practice archive can be created.");
        string id = Guid.NewGuid().ToString("N"); var value = new PracticeSave("DEVIL_PRACTICE_SAVE_V1", id, name, category, map, build,
            DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), account, slot, recovered, files.Select(f => new SaveFileEntry(f.Key, f.Value.Length, Hash(f.Value))).ToArray());
        Validate(value); string path = ArchivePath(id), temporary = path + ".pending";
        WriteSet(Path.Combine(temporary, "slot"), files);
        File.WriteAllText(Path.Combine(temporary, "manifest.json"), JsonSerializer.Serialize(value, new JsonSerializerOptions { WriteIndented = true }));
        _ = VerifyFolder(temporary); Directory.Move(temporary, path); return value;
    }
    public PracticeSave Capture(string remote, string slot, string name, string category, string map = "", string build = "")
    {
        RequireClosed(); var location = SteamSaveLocation.Parse(remote); var files = ReadSet(location.SlotPath(slot)); RequireClosed();
        return Store(files, location.Account, slot, name, category, map, build, false);
    }
    // This consumes a COPY made inside the paused, idle engine frame callback.
    // It never reads/replaces the live slot and never relaxes offline restore.
    public PracticeSave ImportLiveCheckpoint(string directory, LivePracticeResult capture, string remote, string name, string category, string map = "")
    {
        var location = SteamSaveLocation.Parse(remote); long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (capture.Account != location.Account || capture.Slot != "DLC1-AUTOSAVE5" || capture.Phase != "archived" || capture.Captured > now + 2000 || capture.Captured < now - 90000)
            throw new IOException("Live checkpoint capture is stale, belongs to a different account, or has not completed.");
        var files = ReadSet(capture.CaptureFolder(directory));
        return Store(files, location.Account, capture.Slot, name, category, map, "Live checkpoint copy; 6.66 Rev 3", false);
    }
    public PracticeSave RecoverBackupPair(string remote, string slot, string name, string category)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Length > 160 || string.IsNullOrWhiteSpace(category) || category.Length > 160) throw new IOException("Enter a name and category for the recovered archive.");
        RequireClosed(); var location = SteamSaveLocation.Parse(remote); var files = ReadSet(location.SlotPath(slot));
        if (!Health(files, location.Account).CanRecover) throw new IOException("Recovery requires both authenticated -BACKUP containers and a missing/failed current pair.");
        // Preserve the exact originals before producing a separate recovered
        // archive. Backup containers bind their filename in AES-GCM associated
        // data, so renaming/copying their ciphertext to a primary is invalid.
        string evidence = Path.Combine(_root, "recovery-evidence", Guid.NewGuid().ToString("N")); WriteSet(evidence, files);
        foreach (string primary in new[] { "game.details", "game_duration.dat" })
            files[primary] = SteamSaveContainer.Seal(SteamSaveContainer.Open(files[primary + "-BACKUP"], location.Account, primary + "-BACKUP"), location.Account, primary);
        RequireClosed(); return Store(files, location.Account, slot, name, category, "", "", true);
    }
    private static void Validate(PracticeSave? value)
    {
        if (value is not { Format: "DEVIL_PRACTICE_SAVE_V1" } || !LevelStateSnapshot.ValidToken(value.Id) || value.Name is not { Length: > 0 and <= 160 }
            || value.Category is not { Length: > 0 and <= 160 } || value.Map is not { Length: <= 512 } || value.Build is not { Length: <= 128 }
            || value.Captured is <= 0 or > 253402300799999 || value.Account is not { Length: 17 } || !ulong.TryParse(value.Account, NumberStyles.None, CultureInfo.InvariantCulture, out ulong account)
            || account <= 76561197960265728UL || account > 76561197960265728UL + uint.MaxValue || !ValidSlot(value.SourceSlot)
            || value.Files is not { Count: >= 4 and <= 256 } || value.Files.Any(f => f is null || !ValidName(f.Name) || f.Length is < 0 or > 64_000_000
                || f.Sha256 is not { Length: 64 } || !f.Sha256.All(char.IsAsciiHexDigit)) || value.Files.Select(f => f.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count() != value.Files.Count
            || value.Files.Sum(f => f.Length) > 128_000_000 || RequiredFiles.Any(name => !value.Files.Any(f => f.Name == name))) throw new IOException("Invalid practice-save manifest.");
    }
    private static (PracticeSave Save, Dictionary<string, byte[]> Files) VerifyFolder(string folder)
    {
        NoLinks(folder); string manifest = Path.Combine(folder, "manifest.json"); NoLinks(manifest);
        if (new FileInfo(manifest).Length > 131072) throw new IOException("Manifest too large.");
        PracticeSave? value; try { value = JsonSerializer.Deserialize<PracticeSave>(File.ReadAllText(manifest)); }
        catch (JsonException ex) { throw new IOException("Invalid archive JSON.", ex); }
        Validate(value); var files = ReadSet(Path.Combine(folder, "slot"));
        if (files.Count != value!.Files.Count || value.Files.Any(f => !files.TryGetValue(f.Name, out var data) || data.Length != f.Length || !Hash(data).Equals(f.Sha256, StringComparison.OrdinalIgnoreCase)))
            throw new IOException("Archive checksum/contents mismatch. Nothing was restored.");
        if (!Health(files, value.Account).Authenticated) throw new IOException("Archive encryption authentication failed. Nothing was restored.");
        return (value, files);
    }
    public PracticeSave Verify(string id) => VerifyFolder(ArchivePath(id)).Save;
    public IReadOnlyList<PracticeSave> List()
    {
        if (!Directory.Exists(_root)) return []; var result = new List<PracticeSave>();
        foreach (string folder in Directory.EnumerateDirectories(_root).Where(p => LevelStateSnapshot.ValidToken(Path.GetFileName(p))).Take(10000))
        {
            // Listing checks metadata only; full hashes/authentication are
            // rechecked before linking/exporting/restoring an archive.
            try
            {
                NoLinks(folder); string manifest = Path.Combine(folder, "manifest.json"); NoLinks(manifest);
                if (new FileInfo(manifest).Length > 131072) continue;
                var value = JsonSerializer.Deserialize<PracticeSave>(File.ReadAllText(manifest)); Validate(value);
                if (value!.Id == Path.GetFileName(folder)) result.Add(value);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException) { }
        }
        return result.OrderBy(s => s.Category, StringComparer.OrdinalIgnoreCase).ThenBy(s => s.Name, StringComparer.OrdinalIgnoreCase).ToArray();
    }
    public PracticeSave Import(string folder, string expectedAccount)
    {
        var source = VerifyFolder(folder);
        if (source.Save.Account != expectedAccount) throw new IOException("Archive belongs to a different Steam account. Import is blocked; account ownership is never changed.");
        return Store(source.Files, source.Save.Account, source.Save.SourceSlot, source.Save.Name, source.Save.Category, source.Save.Map, source.Save.Build, source.Save.FromBackupPair);
    }
    public void Export(string id, string destination)
    {
        var source = VerifyFolder(ArchivePath(id)); NoLinks(destination);
        if (Directory.Exists(destination) || File.Exists(destination)) throw new IOException("Choose a new export directory.");
        WriteSet(Path.Combine(destination, "slot"), source.Files);
        File.WriteAllText(Path.Combine(destination, "manifest.json"), JsonSerializer.Serialize(source.Save, new JsonSerializerOptions { WriteIndented = true }));
    }
    public SaveRestorePlan PlanRestore(string id, string remote, string slot, bool cloudDisabled)
    {
        RequireClosed(); if (!cloudDisabled) throw new IOException("Disable DOOM Eternal Steam Cloud and confirm it before restoring a practice save.");
        var location = SteamSaveLocation.Parse(remote); var source = VerifyFolder(ArchivePath(id)); string target = location.SlotPath(slot);
        if (source.Save.Account != location.Account) throw new IOException("Archive belongs to a different Steam account. Restore is blocked; account ownership is never changed.");
        if (Family(source.Save.SourceSlot) != Family(slot)) throw new IOException("Campaign/TAG1/TAG2 slots cannot be mixed.");
        var current = ReadSet(target);
        if (!RequiredFiles.Any(current.ContainsKey)) throw new IOException("Target must be an existing occupied game slot. Create it in the game, close the game/Steam, then retry.");
        string manifestHash = Hash(File.ReadAllBytes(Path.Combine(ArchivePath(id), "manifest.json")));
        return new(id, manifestHash, location.Remote, slot, Fingerprint(current), $"Replace {target}\nwith {source.Save.Name} ({source.Save.Category})\n{source.Files.Count} files, including BOTH -BACKUP files.\nThe original slot, PROFILE and Steam app metadata will be retained as recovery evidence. PROFILE and Steam metadata will not be replaced.\nThis loads the game's checkpoint save; it does not restore arbitrary live enemy/cooldown state.");
    }
    public SaveRestoreResult Restore(SaveRestorePlan plan)
    {
        RequireClosed(); var location = SteamSaveLocation.Parse(plan.Remote); string target = location.SlotPath(plan.Slot);
        var current = ReadSet(target); var source = VerifyFolder(ArchivePath(plan.ArchiveId));
        if (source.Save.Account != location.Account || Family(source.Save.SourceSlot) != Family(plan.Slot)
            || Hash(File.ReadAllBytes(Path.Combine(ArchivePath(plan.ArchiveId), "manifest.json"))) != plan.ManifestHash || Fingerprint(current) != plan.TargetHash)
            throw new IOException("Source or target changed after the preview. Review a new restore plan.");
        string id = Guid.NewGuid().ToString("N"), evidence = Path.Combine(_root, "restore-evidence", id), stage = Path.Combine(location.AppRoot, ".devil-stage-" + id), original = Path.Combine(location.AppRoot, ".devil-original-" + id);
        // All rename targets are direct children of the resolved app directory;
        // neither manifest content nor caller-supplied filenames choose them.
        NoLinks(location.AppRoot); NoLinks(stage); NoLinks(original);
        WriteSet(Path.Combine(evidence, "slot"), current);
        string profile = Path.Combine(location.Remote, "PROFILE"); if (Directory.Exists(profile)) WriteSet(Path.Combine(evidence, "PROFILE"), ReadSet(profile));
        var metadata = ReadAppMetadata(location.AppRoot); WriteSet(Path.Combine(evidence, "app-metadata"), metadata);
        File.WriteAllText(Path.Combine(evidence, "restore-plan.json"), JsonSerializer.Serialize(plan, new JsonSerializerOptions { WriteIndented = true }));
        File.WriteAllText(Path.Combine(evidence, "RESTORE_PATHS.txt"), "Target: " + target + "\nOriginal: " + original + "\nStaging: " + stage + "\nIf the application/PC stopped between directory renames and Target is missing, keep game/Steam closed and recover Original to Target. Do not merge folders or delete PROFILE/Steam metadata.");
        WriteSet(stage, source.Files);
        if (Fingerprint(ReadSet(stage)) != Fingerprint(source.Files) || Fingerprint(ReadSet(target)) != plan.TargetHash) throw new IOException("Staging/target changed. Original slot untouched.");
        RequireClosed(); _step?.Invoke("staged"); bool moved = false;
        try
        {
            RequireClosed();
            if (Fingerprint(ReadSet(target)) != plan.TargetHash) throw new IOException("Target changed before replacement; original slot untouched.");
            Directory.Move(target, original); moved = true; _step?.Invoke("original_moved"); RequireClosed(); Directory.Move(stage, target);
        }
        catch
        {
            if (moved && !Directory.Exists(target)) Directory.Move(original, target); throw;
        }
        try { File.WriteAllText(Path.Combine(evidence, "COMPLETED.txt"), "Original slot retained at: " + original); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { /* Rename committed; retain an accurate successful outcome even if this optional marker cannot be written. */ }
        return new(original, evidence, "Practice save restored. Original slot and profile/Steam metadata copies are retained. Start Steam/game and load the chosen slot normally.");
    }
    private static Dictionary<string, byte[]> ReadAppMetadata(string appRoot)
    {
        var result = new Dictionary<string, byte[]>(StringComparer.Ordinal); long total = 0;
        string[] paths = Directory.GetFiles(appRoot); if (paths.Length > 256) throw new IOException("Too many Steam metadata files to preserve completely.");
        foreach (string path in paths)
        {
            string name = Path.GetFileName(path); NoLinks(path);
            if (!ValidName(name)) throw new IOException("Unexpected Steam metadata filename.");
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            if (stream.Length > 16_000_000 || (total += stream.Length) > 32_000_000) throw new IOException("Steam metadata exceeds backup limit.");
            byte[] data = new byte[checked((int)stream.Length)]; stream.ReadExactly(data); result.Add(name, data);
        }
        return result;
    }
}
