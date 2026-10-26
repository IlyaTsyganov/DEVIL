using System.Diagnostics;
using Devil.Configuration;

namespace Devil.Game;

public sealed class GameLauncher(SettingsStore settingsStore)
{
    private readonly VulkanLayerRegistration _layerRegistration = new(settingsStore);

    public string RuntimeDirectory => _layerRegistration.RuntimeDirectory;

    public Process Start(DevilSettings settings)
    {
        var startInfo = CreateStartInfo(settings.GameExecutablePath);
        if (settings.CollisionViewerEnabled || startInfo.Environment[LightingFixPolicy.EnvironmentVariable] == "1")
            _layerRegistration.EnableForGame(startInfo.FileName);

        return Process.Start(startInfo) ?? throw new InvalidOperationException("Windows could not start DOOM Eternal.");
    }

    internal static ProcessStartInfo CreateStartInfo(string gamePath)
    {
        if (string.IsNullOrWhiteSpace(gamePath) || !File.Exists(gamePath))
            throw new FileNotFoundException("Select an existing DOOM Eternal executable in the game path section.", gamePath);

        string executableName = Path.GetFileName(gamePath);
        if (!executableName.Equals("DOOMEternalx64vk.exe", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Select DOOMEternalx64vk.exe to launch the Collision Viewer.");

        var startInfo = new ProcessStartInfo
        {
            FileName = gamePath,
            WorkingDirectory = Path.GetDirectoryName(gamePath)!,
            UseShellExecute = false
        };

        // Child process only: manual/Steam launches keep their existing behavior.
        // Explicitly clear an inherited opt-in for all other game versions.
        bool lightingFix = LightingFixPolicy.NeedsFix(gamePath);
        startInfo.Environment[LightingFixPolicy.EnvironmentVariable] = lightingFix ? "1" : "0";
        // SteamAPI_RestartAppIfNecessary can otherwise restart a retail build
        // through Steam and discard the child-only lighting-fix environment.
        // Match the upstream doomfix launcher; do not write steam_appid.txt.
        if (lightingFix)
            startInfo.Environment["SteamAppId"] = "782330";
        return startInfo;
    }
}
