using System.Text;

namespace Devil.Game;

public sealed record LiveRestorePlan(int Process, string Token, string ArchiveId, string ManifestHash, string Remote, string Account,
    string TargetHash, IReadOnlyList<SaveFileEntry> TargetFiles, string Description)
{
    public string Bundle(string directory)
    {
        if (Process <= 0 || Token.Length != 32 || Token.Any(c => !(c is >= '0' and <= '9' or >= 'a' and <= 'f'))) throw new IOException("Invalid live restore scope.");
        return Path.Combine(directory, "live-practice", "restore-pending", Process.ToString(System.Globalization.CultureInfo.InvariantCulture), Token);
    }
    public string Original => Path.Combine(SteamSaveLocation.Parse(Remote).AppRoot, ".devil-live-original-" + Token);
}
public sealed partial class PracticeSaveVault
{
    private static void ValidateLiveArchive(PracticeSave source, SteamSaveLocation location)
    {
        if (source.Account != location.Account) throw new IOException("Archive belongs to a different Steam account. Live restore is blocked; ownership is never changed.");
        if (source.SourceSlot != "DLC1-AUTOSAVE5" || source.Build != "Live checkpoint copy; 6.66 Rev 3" || source.FromBackupPair || source.Files.Count != 4)
            throw new IOException("This experiment restores four-file live archives from TAG1 slot 3 on 6.66 Rev 3 only.");
    }
    public LiveRestorePlan PlanLiveRestore(int process, string id, string remote, string slot, bool cloudDisabled)
    {
        if (process <= 0) throw new IOException("Select the running game first.");
        if (!cloudDisabled) throw new IOException("Confirm Steam Cloud is disabled before live restore.");
        if (slot != "DLC1-AUTOSAVE5") throw new IOException("Live restore is restricted to TAG1 slot 3 (DLC1-AUTOSAVE5).");
        var location = SteamSaveLocation.Parse(remote); var source = VerifyFolder(ArchivePath(id)); ValidateLiveArchive(source.Save, location);
        var current = ReadSet(location.SlotPath(slot), allowWriters: true);
        if (current.Count != 4 || !Health(current, location.Account).Authenticated) throw new IOException("All four current training checkpoint containers must authenticate before this live experiment.");
        string manifestHash = Hash(File.ReadAllBytes(Path.Combine(ArchivePath(id), "manifest.json"))), token = Guid.NewGuid().ToString("N");
        var files = RequiredFiles.Select(name => new SaveFileEntry(name, current[name].Length, Hash(current[name]))).ToArray();
        string original = Path.Combine(location.AppRoot, ".devil-live-original-" + token);
        return new(process, token, id, manifestHash, location.Remote, location.Account, Fingerprint(current), files,
            $"Restore {source.Save.Name} ({source.Save.Category}) into {location.SlotPath(slot)}\nSteam account: {location.Account}\nAll four primary/backup files will be written through Steam's storage API, verified in both Steam and on disk, then loaded through the engine.\nCurrent four-file checkpoint will be retained at {original}\nPROFILE and other campaign slots are not replaced. Steam updates its own slot-file metadata normally.\nPause first; keep Cloud disabled. At 100% loading, press Space in game and pause again. This restores a serialized checkpoint, not arbitrary live simulation state.");
    }
    public void StageLiveRestore(string directory, LiveRestorePlan plan)
    {
        var location = SteamSaveLocation.Parse(plan.Remote); var source = VerifyFolder(ArchivePath(plan.ArchiveId)); ValidateLiveArchive(source.Save, location);
        var current = ReadSet(location.SlotPath("DLC1-AUTOSAVE5"), allowWriters: true);
        if (current.Count != 4 || !Health(current, location.Account).Authenticated) throw new IOException("Current training checkpoint changed or failed authentication.");
        if (plan.Account != location.Account || Hash(File.ReadAllBytes(Path.Combine(ArchivePath(plan.ArchiveId), "manifest.json"))) != plan.ManifestHash || Fingerprint(current) != plan.TargetHash
            || plan.TargetFiles.Count != 4 || RequiredFiles.Where((name, i) => plan.TargetFiles[i].Name != name || plan.TargetFiles[i].Length != current[name].Length || plan.TargetFiles[i].Sha256 != Hash(current[name])).Any())
            throw new IOException("Archive or target changed after review. Create a new live restore plan.");
        string bundle = plan.Bundle(directory); NoLinks(bundle);
        if (Directory.Exists(bundle) || File.Exists(bundle)) throw new IOException("Live restore staging destination already exists.");
        WriteSet(Path.Combine(bundle, "incoming"), source.Files);
        var text = new StringBuilder($"#DEVIL_LIVE_RESTORE_PLAN_V1\t{location.Account}\tDLC1-AUTOSAVE5\t{plan.Token}\t{plan.Process}\t{plan.ArchiveId}\n");
        foreach (var file in plan.TargetFiles) text.Append(file.Name).Append('\t').Append(file.Length.ToString(System.Globalization.CultureInfo.InvariantCulture)).Append('\t').Append(file.Sha256).Append('\n');
        foreach (string name in RequiredFiles) text.Append(name).Append('\t').Append(source.Files[name].Length.ToString(System.Globalization.CultureInfo.InvariantCulture)).Append('\t').Append(Hash(source.Files[name])).Append('\n');
        File.WriteAllText(Path.Combine(bundle, "plan.tsv"), text.ToString(), new UTF8Encoding(false));
    }
}
