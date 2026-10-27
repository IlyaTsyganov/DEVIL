using System.Diagnostics;

namespace Devil.Game;

public sealed record LivePracticeResult(int Process, string Token, long Captured, string Phase, string Account, string Slot, string Map, string Checkpoint, string Detail)
{
    public static LivePracticeResult? Parse(string text)
    {
        if (text.Length > 8192 || text.TrimEnd('\r', '\n').Contains('\n')) return null;
        string[] h = text.TrimEnd('\r', '\n').Split('\t');
        if (h.Length != 10 || h[0] != "#DEVIL_LIVE_PRACTICE_V1" || !int.TryParse(h[1], out int process) || process <= 0 || !LevelStateSnapshot.ValidToken(h[2])
            || !long.TryParse(h[3], out long epoch) || epoch is <= 0 or > 253402300799999 || h[4] is not ("waiting" or "archived" or "loading" or "reloaded" or "error")
            || h[5].Length != 17 || !ulong.TryParse(h[5], out ulong account) || account <= 76561197960265728UL || account > 76561197960265728UL + uint.MaxValue
            || h[6] != "DLC1-AUTOSAVE5" || h[7].Length > 512 || h[8].Length > 1023 || h[9].Length > 2048 || h.Any(s => s.Any(char.IsControl))) return null;
        return new(process, h[2], epoch, h[4], h[5], h[6], h[7], h[8], h[9]);
    }
    public string CaptureFolder(string directory)
    {
        if (Phase != "archived" || !LevelStateSnapshot.ValidToken(Token) || Process <= 0) throw new IOException("Not a completed live checkpoint capture.");
        return Path.Combine(directory, "live-practice", Process.ToString(System.Globalization.CultureInfo.InvariantCulture), Token, "slot");
    }
}
public static class LivePracticeBridge
{
    public static string RequestText(int process, string token, long now, string operation, SteamSaveLocation location, string slot, bool cloudDisabled)
    {
        if (!cloudDisabled) throw new IOException("Confirm Steam Cloud is disabled for DOOM Eternal first.");
        if (process <= 0 || !LevelStateSnapshot.ValidToken(token) || operation is not ("archive" or "reload")) throw new IOException("Invalid live checkpoint request.");
        if (slot != "DLC1-AUTOSAVE5") throw new IOException("This first live experiment supports TAG1 slot 3 (DLC1-AUTOSAVE5) only.");
        if (location.Remote.Any(char.IsControl)) throw new IOException("Unsupported save-folder path.");
        return $"#DEVIL_LIVE_PRACTICE_REQUEST_V1\t{process}\t{token}\t{now}\t{operation}\t{location.Account}\t{slot}\t{location.Remote}\n";
    }
    public static async Task<LivePracticeResult> Execute(string directory, Func<int> gameProcess, string operation, string remote, string slot, bool cloudDisabled, Action<LivePracticeResult>? progress = null)
    {
        int process = gameProcess(); var location = SteamSaveLocation.Parse(remote); string token = Guid.NewGuid().ToString("N");
        string request = RequestText(process, token, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), operation, location, slot, cloudDisabled);
        return await Submit(directory, gameProcess, operation, location, slot, process, token, request, progress);
    }
    public static async Task<LivePracticeResult> ExecuteRestore(string directory, Func<int> gameProcess, LiveRestorePlan plan, bool cloudDisabled, Action<LivePracticeResult>? progress = null)
    {
        if (!cloudDisabled) throw new IOException("Confirm Steam Cloud is disabled before live restore.");
        if (gameProcess() != plan.Process) throw new IOException("Game process changed after live restore review.");
        var location = SteamSaveLocation.Parse(plan.Remote);
        if (location.Account != plan.Account) throw new IOException("Live restore owner mismatch. Account mixing is blocked.");
        _ = plan.Bundle(directory);
        string request = $"#DEVIL_LIVE_PRACTICE_REQUEST_V1\t{plan.Process}\t{plan.Token}\t{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}\trestore\t{location.Account}\tDLC1-AUTOSAVE5\t{location.Remote}\n";
        return await Submit(directory, gameProcess, "restore", location, "DLC1-AUTOSAVE5", plan.Process, plan.Token, request, progress);
    }
    private static async Task<LivePracticeResult> Submit(string directory, Func<int> gameProcess, string operation, SteamSaveLocation location, string slot, int process, string token, string request, Action<LivePracticeResult>? progress)
    {
        Directory.CreateDirectory(directory); PracticeSaveVault.NoLinks(directory);
        string stem = Path.Combine(directory, $"practice-live-{process}"), path = stem + ".request";
        PracticeSaveVault.NoLinks(path); PracticeSaveVault.NoLinks(path + ".tmp");
        File.WriteAllText(path + ".tmp", request, new System.Text.UTF8Encoding(false)); File.Move(path + ".tmp", path, true);
        long started = Environment.TickCount64, deadline = started + (operation == "archive" ? 15000 : 330000), reported = 0; bool received = false;
        while (Environment.TickCount64 < deadline)
        {
            await Task.Delay(100);
            if (!received && Environment.TickCount64 - started >= 15000) break;
            if (gameProcess() != process) throw new IOException("Selected game process changed; live operation stopped.");
            try { using var game = Process.GetProcessById(process); if (game.HasExited) throw new IOException("Game closed during live operation."); }
            catch (ArgumentException) { throw new IOException("Game closed during live operation."); }
            string response = stem + ".tsv"; PracticeSaveVault.NoLinks(response);
            if (!File.Exists(response) || new FileInfo(response).Length > 8192) continue;
            var result = LivePracticeResult.Parse(await File.ReadAllTextAsync(response));
            if (result is null || result.Process != process || result.Token != token) continue;
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(); if (result.Captured > now + 2000 || result.Captured < now - 90000) continue;
            if (result.Account != location.Account || result.Slot != slot) throw new IOException("Live response owner/slot mismatch. Account mixing is blocked.");
            received = true; if (result.Phase == "error") throw new IOException(result.Detail);
            if (operation == "archive" && result.Phase is "loading" or "reloaded" || operation != "archive" && result.Phase == "archived") throw new IOException("Live response does not match the requested operation.");
            if (reported != result.Captured) { progress?.Invoke(result); reported = result.Captured; }
            if (result.Phase is "archived" or "reloaded") return result;
        }
        throw new IOException(received ? operation == "restore" ? "Restore completion was not observed. Files may already be installed; original checkpoint retained at " + Path.Combine(location.AppRoot, ".devil-live-original-" + token)
            : "Engine operation did not report completion. At 100% loading press Space in game, then pause."
            : "No response from the live-save bridge. The updated native layer must be loaded by restarting the game once through this DEViL build.");
    }
}
