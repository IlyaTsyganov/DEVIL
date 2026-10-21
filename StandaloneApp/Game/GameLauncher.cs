using System.Diagnostics;
using Devil.Configuration;

namespace Devil.Game;

public sealed class GameLauncher(SettingsStore settingsStore)
{
    private readonly VulkanLayerRegistration _layerRegistration = new(settingsStore);

    public string RuntimeDirectory => _layerRegistration.RuntimeDirectory;

    public Process Start(DevilSettings settings)
    {
        string gamePath = settings.GameExecutablePath;
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

        if (settings.CollisionViewerEnabled)
        {
            _layerRegistration.EnableForGame(gamePath);
        }

        return Process.Start(startInfo) ?? throw new InvalidOperationException("Windows could not start DOOM Eternal.");
    }
}
