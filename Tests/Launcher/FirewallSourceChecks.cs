using Devil.Configuration;
using Devil.Game;

internal static class FirewallSourceChecks
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    internal static void Run(string temporary)
    {
        string exe = Path.Combine(temporary, "firewall", "DOOMEternalx64vk.exe");
        var block = new GameFirewallRule("arbitrary rule name", exe, true, 2, 0, int.MaxValue);
        var state = new GameFirewallState(4, 7, false, true, [block]);
        Check(GameFirewall.Evaluate(exe.ToUpperInvariant(), state).Blocked, "Exact executable case normalization failed");
        foreach (var invalid in new[] { block with { Enabled = false }, block with { Direction = 1 }, block with { Action = 1 },
            block with { Application = Path.Combine(temporary, "other", "DOOMEternalx64vk.exe") }, block with { Profiles = 2 }, block with { Protocol = 6 },
            block with { RemoteAddresses = "1.2.3.4" }, block with { LocalAddresses = "192.168.0.0/24" }, block with { InterfaceTypes = "Lan" },
            block with { Service = "SomeService" }, block with { LimitedInterfaces = true } })
            Check(!GameFirewall.Evaluate(exe, state with { Rules = [invalid] }).Blocked, "Restricted/ineffective rule accepted: " + invalid);
        foreach (var invalid in new[] { state with { Rules = [] }, state with { ActiveProfiles = 0 }, state with { ActiveProfiles = 8 },
            state with { EnabledProfiles = 3 }, state with { ExcludedInterfaces = true }, state with { LocalPolicyEffective = false } })
            Check(!GameFirewall.Evaluate(exe, invalid).Blocked, "Unprotected firewall policy accepted");
        Check(!GameFirewall.Evaluate(exe, state with { ActiveProfiles = 6, Rules = [block with { Profiles = 2 }] }).Blocked, "Second active profile was ignored");
        Check(GameFirewall.Evaluate(exe, state with { ActiveProfiles = 6, Rules = [block with { Profiles = 2 }, block with { Profiles = 4 }] }).Blocked, "Separate profile blocks were ignored");
        Directory.CreateDirectory(Path.GetDirectoryName(exe)!); File.WriteAllBytes(exe, [1, 2, 3]);
        var store = new SettingsStore(Path.Combine(temporary, "firewall-settings"));
        var launcher = new GameLauncher(store, path => { Check(path == exe, "Checked a different launch target"); return new(false, "test-blocked"); });
        try { launcher.Start(new() { GameExecutablePath = exe, CollisionViewerEnabled = true }); throw new Exception("Unprotected game launch allowed"); }
        catch (InvalidOperationException ex) when (ex.Message == "test-blocked") { }
        Check(!Directory.Exists(launcher.RuntimeDirectory), "Layer registered before firewall gate");

        string Remote(string account) { string path = Path.Combine(temporary, "sources", "userdata", account, "782330", "remote"); Directory.CreateDirectory(path); return path; }
        string one = Remote("10001"), two = Remote("10002");
        string owner = SteamSaveLocation.Parse(one).Account, other = SteamSaveLocation.Parse(two).Account;
        Check(PracticeSaveSource.Choose([one], null, null) == one, "Single source not chosen");
        Check(PracticeSaveSource.Choose([one, two], null, null) is null, "Ambiguous account chosen");
        Check(PracticeSaveSource.Choose([one, two], owner, other) == one, "Runtime owner did not override registry");
        Check(PracticeSaveSource.Choose([one], other, null) is null, "Wrong account fallback chosen");
        Check(PracticeSaveSource.Choose([one, one], null, null) == one, "Duplicate roots made source ambiguous");
        string response = Path.Combine(temporary, $"practice-live-{Environment.ProcessId}.tsv"), token = new('a', 32);
        string Reply(long time) => $"#DEVIL_LIVE_PRACTICE_V1\t{Environment.ProcessId}\t{token}\t{time}\tarchived\t{owner}\tDLC1-AUTOSAVE5\tgame/dlc/e4m1_rig/e4m1_rig\tcp_01a\tfixture";
        File.WriteAllText(response, Reply(1)); Check(PracticeSaveSource.RuntimeAccount(temporary, Environment.ProcessId) is null, "Stale PID reply selected an owner");
        File.WriteAllText(response, Reply(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds())); Check(PracticeSaveSource.RuntimeAccount(temporary, Environment.ProcessId) == owner, "Current owner response not recognized");
        Console.WriteLine("PASS: firewall path/profile/protocol/address/interface/policy checks and blocked launch before registration; automatic save source chooses one matching owner and rejects stale PID data.");
    }
}
