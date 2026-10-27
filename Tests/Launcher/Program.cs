using System.Buffers.Binary;
using System.Text.RegularExpressions;
using Devil.Game;
using Devil.Configuration;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}

// Explicit acceptance modes. The normal fixture run never calls the game/Steam.
if (args.Length == 2 && args[0] == "--firewall-status")
{
    var status = GameFirewall.Inspect(args[1]);
    Console.WriteLine(status.Message); Console.WriteLine(status.Rule);
    Console.WriteLine("Read-only inspection: no rules changed; no process launched.");
    return status.Blocked ? 0 : 1;
}
if (args.Length == 1 && args[0] == "--native-crypto-fixture")
{
    foreach (string name in PracticeSaveVault.RequiredFiles)
        Console.WriteLine(Convert.ToHexStringLower(SteamSaveContainer.Seal(System.Text.Encoding.UTF8.GetBytes("native-fixture:" + name), "76561197960275729", name)));
    return 0;
}
if (args.Length == 4 && args[0] is "--live-restore" or "--live-restore-plan" or "--live-restore-stage")
{
    Check(int.TryParse(args[1], out int process) && process > 0, "Expected running game PID");
    string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DEViL"); var vault = new PracticeSaveVault(directory);
    var plan = vault.PlanLiveRestore(process, args[3], args[2], "DLC1-AUTOSAVE5", true);
    Console.WriteLine(plan.Description); if (args[0] == "--live-restore-plan") { Console.WriteLine("PLAN ONLY: no request sent and no checkpoint files changed."); return 0; }
    vault.StageLiveRestore(directory, plan);
    if (args[0] == "--live-restore-stage") { Console.WriteLine($"STAGE ONLY: token={plan.Token}; account={plan.Account}; bundle={plan.Bundle(directory)}"); return 0; }
    var result = await LivePracticeBridge.ExecuteRestore(directory, () => ProcessExists(process) ? process : 0, plan, true);
    Console.WriteLine($"Live restore: {result.Phase}; PID {result.Process}; loaded checkpoint {result.Checkpoint}; token {result.Token}");
    Console.WriteLine(result.Detail); Console.WriteLine("Original checkpoint: " + plan.Original); return 0;
}
// Explicit live acceptance driver: uses the SAME production protocol as the UI.
// Restricted by native runtime checks to TAG1 slot 3, Steam owner and Cloud off.
if (args.Length == 5 && args[0] == "--live-practice")
{
    Check(int.TryParse(args[1], out int process) && process > 0, "Expected live game PID");
    Check(args[2] is "archive" or "reload", "Expected archive or reload");
    string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DEViL");
    var result = await LivePracticeBridge.Execute(directory, () => ProcessExists(process) ? process : 0, args[2], args[3], "DLC1-AUTOSAVE5", true);
    Console.WriteLine($"Live operation: {result.Phase}; PID {result.Process}; map {result.Map}; loaded checkpoint {result.Checkpoint}; token {result.Token}");
    Console.WriteLine(result.Detail);
    if (args[2] == "archive")
    {
        var vault = new PracticeSaveVault(directory);
        var archive = vault.ImportLiveCheckpoint(directory, result, args[3], args[4], "Live loading acceptance");
        Check(vault.Verify(archive.Id).Files.Count == 4, "Live archive did not authenticate");
        Console.WriteLine($"Verified archive: {archive.Id}; {archive.Files.Count} files; original account {archive.Account}; source {archive.SourceSlot}");
    }
    return 0;
}
static bool ProcessExists(int process)
{
    try { using var game = System.Diagnostics.Process.GetProcessById(process); return !game.HasExited; }
    catch (ArgumentException) { return false; }
}
if (args.Length == 3 && args[0] == "--inspect-save-slot")
{
    var health = new PracticeSaveVault(Path.GetTempPath()).Inspect(args[1], args[2]);
    Console.WriteLine(health); Check(health.Authenticated, "Current save containers did not all authenticate");
    Console.WriteLine("PASS: actual Steam slot checked read-only; both primary and -BACKUP containers authenticate. No save files changed."); return 0;
}
if (args.Length == 2 && args[0] == "--point-snapshot")
{
    var point = BookmarkObservation.Parse(File.ReadAllLines(args[1])); Check(point is { Ready: true }, "Actual native point protocol rejected");
    Console.WriteLine($"PASS: real point parsed: body/camera/aim preserved separately; {point!.Map}; loaded checkpoint {point.Checkpoint}; {point.Profile}."); return 0;
}
if (args.Length == 2 && args[0] == "--level-state-snapshot")
{
    var capture = LevelStateSnapshot.Parse(File.ReadAllLines(args[1])) ?? throw new Exception("Native level state was rejected");
    Check(capture.Ready && capture.Volumes.Count > 0 && capture.Managers.Count > 0 && capture.CheckpointStatus == "known", "Real level state incomplete");
    Console.WriteLine($"PASS: real native level state accepted: {capture.Members.Count} entities, {capture.Volumes.Count} volumes, {capture.Managers.Count} managers, loaded checkpoint {capture.Checkpoint}; {capture.Profile}.");
    return 0;
}
if (args.Length == 2 && args[0] == "--motion-snapshot")
{
    var capture = MotionSnapshot.Parse(File.ReadAllLines(args[1])) ?? throw new Exception("Native player telemetry was rejected");
    Check(capture.Samples.Count > 0 && capture.Samples.All(sample => sample.Collider && sample.Valid), "Real player collider unavailable");
    var sample = capture.Samples[^1];
    Console.WriteLine($"PASS: real player telemetry accepted: {capture.Samples.Count} samples; body {sample.Maximum.X - sample.Minimum.X:0.000} × {sample.Maximum.Y - sample.Minimum.Y:0.000} × {sample.Maximum.Z - sample.Minimum.Z:0.000}; support {sample.Contact}; {capture.Profile}.");
    return 0;
}
if (args.Length == 2 && args[0] == "--trigger-catalog")
{
    var catalogue = TriggerPathCatalog.Read(args[1]);
    Check(catalogue.Contains(new("idTrigger", "trigger/props/")) && catalogue.Contains(new("idTrigger", "trigger/interact/")), "Real trigger catalogue lost the observed folder groups");
    Console.WriteLine($"PASS: native trigger path catalogue accepted: {catalogue.Count} class/folder groups, including props and interact.");
    return 0;
}
if (args.Length == 2 && args[0] == "--graph-snapshot")
{
    var capturedGraph = EntityGraphSnapshot.Parse(File.ReadAllLines(args[1])) ?? throw new Exception("Retail graph snapshot was rejected by the companion parser");
    Check(capturedGraph.Ready && capturedGraph.Nodes.Count > 0 && capturedGraph.Edges.Count > 0, "Retail graph has no entities/links");
    Check(capturedGraph.Edges.Any(edge => edge.Field.StartsWith("owners[") && edge.Evidence == "Managed pointer / map slot checked"), "Retail graph has no validated encounter owner links");
    Check(capturedGraph.Edges.All(edge => edge.From != edge.To), "Retail graph retains internal self pointers");
    int ownerLinks = capturedGraph.Edges.Count(edge => edge.Field.StartsWith("owners["));
    Console.WriteLine($"PASS: retail native graph parsed by companion: {capturedGraph.Nodes.Count} entities, {capturedGraph.Edges.Count} links, {ownerLinks} owner links; {capturedGraph.Map}");
    return 0;
}

// Minimal PE files exercise section RVA-to-file-offset mapping; no fixture is
// executable. These tests never launch the game or alter Vulkan registration.
static byte[] Fixture(uint timestamp, uint size, uint rva, bool validCode = true)
{
    byte[] data = new byte[0x400];
    void U16(int offset, ushort value) => BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(offset), value);
    void U32(int offset, uint value) => BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(offset), value);
    U16(0, 0x5a4d); U32(0x3c, 0x80); U32(0x80, 0x4550);
    U16(0x84, 0x8664); U16(0x86, 1); U32(0x88, timestamp); U16(0x94, 0xf0); U16(0x96, 0x22);
    U16(0x98, 0x20b); U32(0x98 + 32, 0x1000); U32(0x98 + 36, 0x200);
    U32(0x98 + 56, size); U32(0x98 + 60, 0x200); U32(0x98 + 108, 16);
    ".text"u8.CopyTo(data.AsSpan(0x188));
    U32(0x188 + 8, 0x200); U32(0x188 + 12, rva); U32(0x188 + 16, 0x200); U32(0x188 + 20, 0x200);
    byte[] code = [0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20];
    code.CopyTo(data, 0x200);
    if (!validCode) data[0x200] ^= 0xff;
    return data;
}

string temporary = Path.Combine(Path.GetTempPath(), "DEViL-Launcher-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(temporary);
string? inherited = Environment.GetEnvironmentVariable(LightingFixPolicy.EnvironmentVariable);
string? inheritedSteamAppId = Environment.GetEnvironmentVariable("SteamAppId");
try
{
    MotionChecks.Run(temporary);
    var store = new SettingsStore(Path.Combine(temporary, "settings"));
    Directory.CreateDirectory(store.SettingsDirectory);
    File.WriteAllText(store.SettingsPath, "{\"ShowTriggers\":true}");
    var settings = store.Load();
    Check(settings.IsTriggerClassVisible("idTrigger") && settings.IsTriggerClassVisible("idTrigger_NewClass"), "Old settings must show all classes");
    settings.SetTriggerClassVisible("idTrigger", false);
    settings.SetTriggerClassVisible("idEncounterTrigger_Commit", false);
    settings.SetTriggerClassVisible("idTrigger_NewClass", false);
    settings.SetTriggerClassVisible("idTrigger\r\nShowCollision=0", false);
    store.Save(settings);
    var reloaded = store.Load();
    Check(!reloaded.IsTriggerClassVisible("idTrigger") && !reloaded.IsTriggerClassVisible("idEncounterTrigger_Commit")
        && !reloaded.IsTriggerClassVisible("idTrigger_NewClass"), "Per-class choices did not survive reload");
    Check(reloaded.IsTriggerClassVisible("idTrigger_Teleporter"), "Hiding idTrigger must not hide derived class names");
    string viewerSettings = File.ReadAllText(store.ViewerSettingsPath);
    Check(viewerSettings.Contains("[TriggerClasses]") && viewerSettings.Contains("idTrigger_NewClass=0"), "Native class filter was not exported");
    Check(reloaded.HiddenTriggerClasses.Count == 3, "Invalid class name accepted");
    reloaded.SetTriggerClassVisible("idTrigger", true);
    store.Save(reloaded);
    Check(!File.ReadAllText(store.ViewerSettingsPath).Contains("idTrigger=0"), "Re-enabled class remained hidden in native settings");
    Check(TriggerClassCatalog.Known.All(item => TriggerClassCatalog.IsValidName(item.ClassName))
        && TriggerClassCatalog.Known.Select(item => item.ClassName).Distinct().Count() == TriggerClassCatalog.Known.Count, "Invalid or duplicate catalog class");
    Check(TriggerClassCatalog.Choices(["idTrigger_NewClass", "idTrigger_NewClass", "not_a_class"]).Count
        == TriggerClassCatalog.Known.Count + 1, "Discovered classes must be validated and deduplicated");
    File.WriteAllText(store.SettingsPath, "{\"HiddenTriggerClasses\":null}");
    Check(store.Load().HiddenTriggerClasses.Count == 0, "Null class preferences must load safely");
    var pathSettings = new DevilSettings();
    pathSettings.SetTriggerPathVisible("idTrigger", "Trigger\\Props\\", false);
    pathSettings.SetTriggerPathVisible("idTrigger", "trigger/../", false);
    pathSettings.SetTriggerPathVisible("idTrigger", "trigger/props/\nEnabled=0", false);
    pathSettings.SetTriggerPathVisible("invalid", "trigger/props/", false);
    pathSettings.SetTriggerPathVisible("idTrigger", "@no_path", false);
    store.Save(pathSettings); var pathsReloaded = store.Load();
    Check(pathsReloaded.HiddenTriggerPaths.Count == 1 && !pathsReloaded.IsTriggerPathVisible("idTrigger", "trigger/props/small/")
        && pathsReloaded.IsTriggerPathVisible("idTrigger", "trigger/props_extra/") && pathsReloaded.IsTriggerPathVisible("idTrigger", "trigger/interact/")
        && pathsReloaded.IsTriggerPathVisible("idTrigger_Teleporter", "trigger/props/"), "Path hierarchy or class isolation failed");
    Check(File.ReadAllText(store.ViewerSettingsPath).Contains("[TriggerPaths]")
        && File.ReadAllText(store.ViewerSettingsPath).Contains("idTrigger|trigger/props/=0"), "Native path rules were not exported");
    pathsReloaded.SetTriggerClassVisible("idTrigger", false); pathsReloaded.SetTriggerClassVisible("idTrigger", true);
    Check(!pathsReloaded.IsTriggerPathVisible("idTrigger", "trigger/props/") && pathsReloaded.IsTriggerPathVisible("idTrigger", "trigger/interact/"), "Class toggle overwrote child preferences");
    pathsReloaded.SetTriggerPathVisible("idTrigger", "trigger/props/", true); store.Save(pathsReloaded);
    Check(store.Load().IsTriggerPathVisible("idTrigger", "trigger/props/") && !File.ReadAllText(store.ViewerSettingsPath).Contains("idTrigger|trigger/props/=0"), "Re-enabled path remained hidden");
    File.WriteAllText(store.SettingsPath, "{\"HiddenTriggerPaths\":null}");
    Check(store.Load().HiddenTriggerPaths.Count == 0, "Null legacy path preferences were not normalized");
    File.WriteAllText(store.SettingsPath, "{\"HiddenTriggerPaths\":[{\"ClassName\":\"idTrigger\",\"Prefix\":\"@no_path\"}]}");
    var legacyPaths = store.Load(); store.Save(legacyPaths);
    Check(legacyPaths.HiddenTriggerPaths.Count == 0 && !File.ReadAllText(store.ViewerSettingsPath).Contains("@no_path"), "Retired no-directory preference survived migration");
    Check(TriggerPathCatalog.Known.Count == 24 && TriggerPathCatalog.Known.All(TriggerPathCatalog.IsValid)
        && TriggerPathCatalog.Known.Select(item => item.ClassName).Distinct().Count() == 17
        && TriggerPathCatalog.Known.Contains(new("idTrigger_Hurt", "trigger/ai/"))
        && TriggerPathCatalog.Known.Contains(new("idTrigger", "trigger/props/weapons/"))
        && TriggerPathCatalog.Known.Contains(new("idTrigger_StatDrivenEvent", "trigger/stat_event/")), "Bundled resource catalogue missing or incomplete");
    var researchSettings = new DevilSettings();
    Check(!researchSettings.RecordResearch, "Research must not record automatically");
    Check(LabCommands.Apply(researchSettings, ["lab_record", "1"], 1234, 100)
        && researchSettings.RecordResearch && researchSettings.ResearchProcess == 1234, "Console recording did not use the sending game process");
    Check(LabCommands.Apply(researchSettings, ["lab_path", "0"], 1234, 100)
        && !researchSettings.ShowResearchPath && researchSettings.RecordResearch, "Path visibility must not stop recording");
    Check(LabCommands.Apply(researchSettings, ["lab_mark", "checkpoint", "test"], 1234, 101)
        && researchSettings.ResearchMarker == 101 && researchSettings.ResearchMarkerLabel == "checkpoint test", "Console marker was not preserved");
    Check(LabCommands.Apply(researchSettings, ["lab_clear"], 1234, 102) && researchSettings.ResearchReset == 102, "Clear did not change session token");
    Check(LabCommands.Apply(researchSettings, ["lab_trigger", "idTrigger", "0"], 1234, 102)
        && !researchSettings.IsTriggerClassVisible("idTrigger") && researchSettings.IsTriggerClassVisible("idTrigger_Teleporter"), "Console class filter must use exact names");
    Check(!LabCommands.Apply(researchSettings, ["lab_distance", "201"], 1234, 102)
        && !LabCommands.Apply(researchSettings, ["quit", "1"], 1234, 102), "Unexpected console command accepted");
    store.Save(researchSettings);
    Check(File.ReadAllText(store.ViewerSettingsPath).Contains("ResearchProcess=1234") && store.Load().ResearchMarker == 101, "Research controls did not reach the native settings");
    string[] sessionLines = ["#DEVIL_RESEARCH_V1\t1234\t0\t2\t1234-1000", "sequence\telapsed_ms\tkind\tmap\tid\tgeneration\tclass\tx\ty\tz\tdetail",
        "1\t100\tposition\tmap_a\t0\t0\t\t1.25\t-2.5\t3\t1", "2\t200\tmarker\tmap_a\t0\t0\t\t1.25\t-2.5\t3\tcheckpoint, note", "partial\trow"];
    var researchLog = ResearchLog.Parse(sessionLines) ?? throw new Exception("Session header was not recognized");
    Check(researchLog.Rows.Count == 2 && researchLog.Rows[0].Y == -2.5f && researchLog.Omitted == 2, "Session parsing lost coordinates or accepted partial rows");
    string csv = Path.Combine(temporary, "research.csv"); researchLog.ExportCsv(csv);
    Check(File.ReadAllText(csv).Contains("\"checkpoint, note\""), "CSV marker was not quoted");
    Directory.CreateDirectory(Path.Combine(store.SettingsDirectory, "research"));
    File.WriteAllLines(Path.Combine(store.SettingsDirectory, "research", "1234-1000.full.tsv"), sessionLines);
    researchLog.ExportFullCsv(store.SettingsDirectory, csv);
    Check(File.ReadAllLines(csv).Length == 3, "Full journal CSV export lost valid samples");
    File.WriteAllText(store.ResearchCurrentPath, "../../outside.tsv");
    Check(ResearchLog.ReadLatest(store.SettingsDirectory) is null, "Session pointer escaped the research directory");
    string pipeName = "DEViL-Lab-Test-" + Guid.NewGuid().ToString("N");
    string presenceName = @"Local\" + pipeName;
    using var received = new ManualResetEventSlim(); int receivedProcess = 0; string[] receivedArgs = [];
    using (var server = new LabCommandServer((process, command) => { receivedProcess = process; receivedArgs = command; received.Set(); }, pipeName, presenceName))
    {
        using (var presence = EventWaitHandle.OpenExisting(presenceName)) Check(presence.WaitOne(0), "Running companion presence was not signalled");
        using var client = new System.IO.Pipes.NamedPipeClientStream(".", pipeName, System.IO.Pipes.PipeDirection.Out);
        client.Connect(1500); byte[] message = System.Text.Encoding.UTF8.GetBytes("1234\tlab_path\t1\n"); client.Write(message); client.Flush();
        Check(received.Wait(1500) && receivedProcess == 1234 && receivedArgs.SequenceEqual(new[] { "lab_path", "1" }), "lab_ IPC lost its sender or arguments");
    }
    bool presenceRemains = EventWaitHandle.TryOpenExisting(presenceName, out var stalePresence); stalePresence?.Dispose();
    Check(!presenceRemains, "Console availability must end with the companion server");
    Check(LabCommands.Apply(researchSettings, ["lab_encounters", "1"], 1234, 102) && researchSettings.ShowActiveEncountersHud, "Encounter HUD value did not reach shared UI settings");
    store.Save(researchSettings);
    Check(File.ReadAllText(store.ViewerSettingsPath).Contains("ShowActiveEncountersHud=1"), "Encounter HUD setting did not reach the native layer");
    long encounterTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    string[] encounterLines = [$"#DEVIL_ENCOUNTERS_V1\t1234\t{encounterTime}\tlive\tgame/dlc/e4m1_rig/e4m1_rig\t1",
        "id\tgeneration\tmanager\tlabel\tthinkFlags\tenabled\tpaused\tspawnPaused\twaitingExit\tscript\tcommands",
        "100\t3\tstarting_platform_encounter_manager_1\tUAC Atlantica / Combat 1 Landing Pad 1\t1\t1\t0\t0\t0\t0\t1",
        "101\t3\tinactive_manager\t\t0\t1\t0\t0\t0\t-1\t0",
        "102\t3\tpaused_manager\t\t2\t1\t1\t0\t0\t2\t3",
        "103\t3\tunreadable_manager\t\t?\t-1\t-1\t-1\t-1\t-1\t-1"];
    var encounters = ActiveEncounterSnapshot.Parse(encounterLines) ?? throw new Exception("Encounter snapshot was not parsed");
    Check(encounters.IsFresh(DateTimeOffset.UtcNow) && encounters.Rows.Count == 4 && encounters.Rows.Count(row => row.Active) == 2, "Active encounter values were lost");
    Check(encounters.Select(false, "").Count() == 3 && encounters.Select(true, "").Count() == 4, "Inactive filtering must retain unknown states");
    Check(encounters.Select(false, "landing pad").Single().Manager == "starting_platform_encounter_manager_1", "Human-name search did not retain the technical manager");
    Check(encounters.Rows[2].State == "Paused" && encounters.Rows[3].State == "Unavailable", "Pause and unreadable states must be distinct");
    Check(!(encounters with { Captured = DateTimeOffset.UtcNow.AddSeconds(-3) }).IsFresh(DateTimeOffset.UtcNow), "Stale encounters shown as fresh");
    Check(!(encounters with { Captured = DateTimeOffset.UtcNow.AddMinutes(1) }).IsFresh(DateTimeOffset.UtcNow), "Future encounter timestamp accepted");
    Check(!(encounters with { Phase = "read_error" }).Select(true, "").Any(), "Read errors must not retain an apparently current active list");
    string encounterFile = Path.Combine(store.SettingsDirectory, "active-encounters-1234.tsv"); File.WriteAllLines(encounterFile, encounterLines);
    Check(ActiveEncounterSnapshot.Read(store.SettingsDirectory, 1234)?.Rows.Count == 4, "Encounter file round-trip failed");
    File.WriteAllLines(encounterFile, new[] { encounterLines[0].Replace("\t1234\t", "\t4321\t"), encounterLines[1] });
    Check(ActiveEncounterSnapshot.Read(store.SettingsDirectory, 1234) is null, "Encounter snapshot from a different game process accepted");
    Check(ActiveEncounterSnapshot.Parse(encounterLines.Append("partial\trow").ToArray()) is null, "Partial encounter snapshot accepted as a valid list");
    string[] graphLines = [$"#DEVIL_GRAPH_V1\t1234\t{encounterTime}\tlive\tgame/dlc/e4m1_rig/e4m1_rig\t500:600\t1\t0\t0\t100:3",
        "N\t100:3\tarena_trigger\tidEncounterTrigger_Commit\t",
        "N\t101:3\tstarting_platform_encounter_manager_1\tidEncounterManager\tEncounter: UAC Atlantica / Combat 1 Landing Pad 1",
        "N\t102:3\tgate_entity\tidEntity\t", "N\t103:3\tunlinked_entity\tidEntity\t",
        "E\t100:3\t101:3\towners[0]\tManaged pointer / map slot checked",
        "E\t101:3\t102:3\ttarget\tEntity pointer"];
    var graph = EntityGraphSnapshot.Parse(graphLines) ?? throw new Exception("Graph snapshot did not parse");
    Check(graph.IsFresh(DateTimeOffset.UtcNow) && graph.Selected == "100:3" && graph.Edges.Count == 2, "Graph selection, freshness or edges lost");
    Check(graph.Search("", true).Count() == 3 && graph.Search("", false).Count() == 4, "Graph isolated-node filter failed");
    Check(graph.Search("landing pad", true).Single().Key == "101:3", "Graph reference-name search failed");
    Check(EntityGraphSnapshot.Parse(graphLines.Append("E\t100:3\t999:3\ttarget\tEntity pointer").ToArray()) is null, "Dangling graph target accepted");
    Check(EntityGraphSnapshot.Parse(graphLines.Append(graphLines[1]).ToArray()) is null, "Duplicate graph identity accepted");
    Check(EntityGraphSnapshot.Parse(graphLines.Append("partial row").ToArray()) is null, "Partial graph accepted");
    Check(EntityGraphSnapshot.Parse(graphLines.Select(line => line.Replace("Entity pointer", "Guessed relationship")).ToArray()) is null, "Unknown evidence accepted as a runtime reference");
    Check(!(graph with { Captured = DateTimeOffset.UtcNow.AddSeconds(-5) }).IsFresh(DateTimeOffset.UtcNow), "Stale graph is live");
    Check(!(graph with { Captured = DateTimeOffset.UtcNow.AddSeconds(30) }).IsFresh(DateTimeOffset.UtcNow), "Future graph accepted");
    string graphFile = Path.Combine(store.SettingsDirectory, "entity-graph-1234.tsv"); File.WriteAllLines(graphFile, graphLines);
    Check(EntityGraphSnapshot.Read(store.SettingsDirectory, 1234)?.Nodes.Count == 4, "Graph transport round trip failed");
    File.WriteAllLines(graphFile, graphLines.Select(line => line.Replace("\t1234\t", "\t4321\t")));
    Check(EntityGraphSnapshot.Read(store.SettingsDirectory, 1234) is null, "Graph from another process accepted");
    EntityGraphSnapshot.Request(store.SettingsDirectory, 1234);
    Check(long.TryParse(File.ReadAllText(EntityGraphSnapshot.RequestPath(store.SettingsDirectory, 1234)), out _), "Graph request timestamp is not numeric");
    string graphExport = Path.Combine(temporary, "graph.json"); graph.ExportJson(graphExport);
    using (var json = System.Text.Json.JsonDocument.Parse(File.ReadAllText(graphExport)))
        Check(json.RootElement.GetProperty("Edges").GetArrayLength() == 2 && json.RootElement.GetProperty("MapIdentity").GetString() == "500:600", "Graph JSON lost edges or map identity");
    const string expectedViewer = "DoomCollisionLayer-49e948b4f93f2050.dll";
    Check(ProcessMonitor.UsesExpectedViewer(["XINPUT1_3.dll", expectedViewer], expectedViewer), "Current viewer build not recognised");
    Check(!ProcessMonitor.UsesExpectedViewer(["DoomCollisionLayer-51548400bd3b5c96.dll"], expectedViewer), "Old loaded viewer falsely reported as current");
    Check(!ProcessMonitor.UsesExpectedViewer([], expectedViewer), "Missing viewer falsely reported as loaded");
    Check(!ProcessMonitor.UsesExpectedViewer([expectedViewer, "DoomCollisionLayer-51548400bd3b5c96.dll"], expectedViewer), "Conflicting loaded viewer builds accepted");
    Check(ProcessMonitor.UsesExpectedViewer([expectedViewer.ToUpperInvariant()], expectedViewer), "Module comparison must ignore Windows filename case");
    string path = Path.Combine(temporary, "DOOMEternalx64vk.exe");
    // Cross-check the companion's version catalog against the independently
    // compiled native reader, including every 6.66 revision and changed code.
    string nativeProfiles = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "native_profile.h"));
    var matches = Regex.Matches(nativeProfiles, "static constexpr GameProfile kProfile\\w+\\{\\s*\"DOOM Eternal (?<version>[^\"]+)\",0x(?<timestamp>[0-9a-f]+),0x(?<size>[0-9a-f]+),\\s*0x(?<rva>[0-9a-f]+),", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    Check(matches.Count == 22, "Native profile fixtures not found");
    foreach (Match match in matches)
    {
        string expectedVersion = match.Groups["version"].Value.Replace(" (Release)", "");
        uint timestamp = Convert.ToUInt32(match.Groups["timestamp"].Value, 16);
        uint size = Convert.ToUInt32(match.Groups["size"].Value, 16);
        uint rva = Convert.ToUInt32(match.Groups["rva"].Value, 16);
        File.WriteAllBytes(path, Fixture(timestamp, size, rva));
        var info = GameExecutableInfo.Read(path) ?? throw new Exception("Game version not recognized: " + expectedVersion);
        Check(info.Version == expectedVersion, "Wrong selected game version: " + expectedVersion);
        Check(info.NeedsLightingFix == (expectedVersion is "1.0" or "1.1" or "2.0"), "Wrong lighting policy: " + expectedVersion);
        File.WriteAllBytes(path, Fixture(timestamp, size, rva, validCode: false));
        Check(GameExecutableInfo.Read(path) is null, "Unknown build shown as supported: " + expectedVersion);
    }
    var builds = new (uint Timestamp, uint Size, uint Rva)[]
    {
        (0x5e794131, 0x1e706000, 0x336210),
        (0x5ec7deca, 0x1d551000, 0x339b10),
        (0x5eeb0267, 0x1d396000, 0x303dc0)
    };
    Environment.SetEnvironmentVariable(LightingFixPolicy.EnvironmentVariable, "0");
    Environment.SetEnvironmentVariable("SteamAppId", "launcher-test-parent");
    foreach (var build in builds)
    {
        File.WriteAllBytes(path, Fixture(build.Timestamp, build.Size, build.Rva));
        var start = GameLauncher.CreateStartInfo(path);
        Check(start.Environment[LightingFixPolicy.EnvironmentVariable] == "1", "Old build not opted in");
        Check(start.Environment["SteamAppId"] == "782330", "Steam restart would drop the lighting-fix opt-in");
        Check(!start.UseShellExecute && start.FileName == path && start.WorkingDirectory == temporary, "Launch target changed");
        Check(Environment.GetEnvironmentVariable(LightingFixPolicy.EnvironmentVariable) == "0", "Parent process environment changed");
        Check(Environment.GetEnvironmentVariable("SteamAppId") == "launcher-test-parent", "Parent Steam environment changed");
        File.WriteAllBytes(path, Fixture(build.Timestamp, build.Size, build.Rva, validCode: false));
        Check(!LightingFixPolicy.NeedsFix(path), "Wrong code fingerprint accepted");
        File.WriteAllBytes(path, Fixture(build.Timestamp, build.Size + 0x1000, build.Rva));
        Check(!LightingFixPolicy.NeedsFix(path), "Wrong image size accepted");
    }
    Environment.SetEnvironmentVariable(LightingFixPolicy.EnvironmentVariable, "1");
    // 2.1 is deliberately excluded even though it shares much of 2.0's layout.
    File.WriteAllBytes(path, Fixture(0x5f203d59, 0x1e1ef000, 0x304520));
    var laterBuild = GameLauncher.CreateStartInfo(path);
    Check(laterBuild.Environment[LightingFixPolicy.EnvironmentVariable] == "0", "2.1 inherited a stale fix opt-in");
    Check(laterBuild.Environment["SteamAppId"] == "launcher-test-parent", "Unaffected game launch environment changed");
    File.WriteAllBytes(path, [1, 2, 3]);
    Check(!LightingFixPolicy.NeedsFix(path), "Malformed PE accepted");
    // Optional: verify actual installed EXEs, using +path or -path for expected eligibility.
    foreach (string argument in args)
    {
        Check(argument.Length > 1 && argument[0] is '+' or '-', "Expected +path or -path");
        Check(LightingFixPolicy.NeedsFix(argument[1..]) == (argument[0] == '+'), "Unexpected eligibility: " + argument[1..]);
    }
    Console.WriteLine("PASS: per-class visibility persistence/backward compatibility and all 22 selected game versions; mapped fingerprints, malformed/changed EXEs, lighting policy, Steam restart prevention and child-only launch environment. No game was launched.");
    LevelStateChecks.Run(temporary);
    BookmarkSaveChecks.Run(temporary);
    LivePracticeChecks.Run(temporary);
    FirewallSourceChecks.Run(temporary);
    return 0;
}
finally
{
    Environment.SetEnvironmentVariable(LightingFixPolicy.EnvironmentVariable, inherited);
    Environment.SetEnvironmentVariable("SteamAppId", inheritedSteamAppId);
    Directory.Delete(temporary, recursive: true);
}
