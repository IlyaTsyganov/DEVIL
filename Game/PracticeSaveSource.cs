using Microsoft.Win32;

namespace Devil.Game;

public static class PracticeSaveSource
{
    public const string LiveSlot = "DLC1-AUTOSAVE5";
    public static bool SupportsLive(PracticeSave save) => save.SourceSlot == LiveSlot && save.Build == "Live checkpoint copy; 6.66 Rev 3" && !save.FromBackupPair && save.Files.Count == 4;
    public static string? Choose(IEnumerable<string> paths, string? runtimeAccount, string? activeAccount)
    {
        var locations = new List<SteamSaveLocation>();
        foreach (string path in paths.Distinct(StringComparer.OrdinalIgnoreCase))
            try { locations.Add(SteamSaveLocation.Parse(path)); } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        string? account = runtimeAccount ?? activeAccount;
        var matches = account is null ? locations : locations.Where(p => p.Account == account).ToList();
        return matches.Count == 1 ? matches[0].Remote : null;
    }
    public static string? RuntimeAccount(string directory, int process)
    {
        if (process <= 0) return null;
        try
        {
            string path = Path.Combine(directory, $"practice-live-{process}.tsv"); PracticeSaveVault.NoLinks(path);
            if (!File.Exists(path) || new FileInfo(path).Length > 8192) return null;
            var value = LivePracticeResult.Parse(File.ReadAllText(path));
            using var game = System.Diagnostics.Process.GetProcessById(process);
            long started = new DateTimeOffset(game.StartTime.ToUniversalTime()).ToUnixTimeMilliseconds();
            return value?.Process == process && value.Captured >= started ? value.Account : null;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception) { return null; }
    }
    public static string? ActiveAccount()
    {
        try
        {
            object? value = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam\ActiveProcess", "ActiveUser", null);
            uint id = value is int number ? unchecked((uint)number) : 0;
            return id == 0 ? null : (76561197960265728UL + id).ToString(System.Globalization.CultureInfo.InvariantCulture);
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or System.Security.SecurityException or IOException) { return null; }
    }
}
