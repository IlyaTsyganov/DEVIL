using System.Buffers.Binary;
using System.Text.RegularExpressions;
using Devil.Game;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
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
    Console.WriteLine("PASS: all 22 selected game versions match native profiles; mapped fingerprints, malformed/changed EXEs, lighting policy, Steam restart prevention and child-only launch environment. No game was launched.");
    return 0;
}
finally
{
    Environment.SetEnvironmentVariable(LightingFixPolicy.EnvironmentVariable, inherited);
    Environment.SetEnvironmentVariable("SteamAppId", inheritedSteamAppId);
    Directory.Delete(temporary, recursive: true);
}
