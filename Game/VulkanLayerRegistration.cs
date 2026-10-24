using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Win32;
using Devil.Configuration;

namespace Devil.Game;

/// <summary>
/// Registers the layer through the Vulkan loader's per-user override meta-layer,
/// scoped to the configured game executable. No other process is opted in.
/// </summary>
public sealed class VulkanLayerRegistration
{
    private const string LayerName = "VK_LAYER_DOOM_collision";
    private const string OverrideName = "VK_LAYER_LUNARG_override";
    private const string ExplicitLayersKey = @"Software\Khronos\Vulkan\ExplicitLayers";
    private const string ImplicitLayersKey = @"Software\Khronos\Vulkan\ImplicitLayers";

    private readonly SettingsStore _settingsStore;

    public VulkanLayerRegistration(SettingsStore settingsStore) => _settingsStore = settingsStore;

    public string RuntimeDirectory => Path.Combine(_settingsStore.SettingsDirectory, "viewer-runtime");
    private string LayerManifestPath => Path.Combine(RuntimeDirectory, "DoomCollisionLayer.json");
    private string OverrideManifestPath => Path.Combine(RuntimeDirectory, "DoomCollisionOverride.json");

    public void EnableForGame(string gamePath)
    {
        string fullGamePath = ValidateGame(gamePath);
        PrepareRuntime();
        EnsureNoOtherOverrideRegistration();

        File.WriteAllText(OverrideManifestPath, JsonSerializer.Serialize(new
        {
            file_format_version = "1.1.1",
            layer = new
            {
                name = OverrideName,
                type = "GLOBAL",
                api_version = "1.0.0",
                implementation_version = "1",
                description = "DEViL enables the collision viewer only for the configured DOOM Eternal executable.",
                component_layers = new[] { LayerName },
                app_keys = new[] { fullGamePath },
                disable_environment = new Dictionary<string, string> { ["DEVIL_DISABLE_COLLISION_OVERRIDE"] = "1" }
            }
        }, new JsonSerializerOptions { WriteIndented = true }));

        using RegistryKey explicitLayers = Registry.CurrentUser.CreateSubKey(ExplicitLayersKey, writable: true)
            ?? throw new InvalidOperationException("Could not open the Vulkan explicit layers list.");
        using RegistryKey implicitLayers = Registry.CurrentUser.CreateSubKey(ImplicitLayersKey, writable: true)
            ?? throw new InvalidOperationException("Could not open the Vulkan implicit layers list.");
        try
        {
            explicitLayers.SetValue(LayerManifestPath, 0, RegistryValueKind.DWord);
            implicitLayers.SetValue(OverrideManifestPath, 0, RegistryValueKind.DWord);
        }
        catch
        {
            explicitLayers.DeleteValue(LayerManifestPath, throwOnMissingValue: false);
            implicitLayers.DeleteValue(OverrideManifestPath, throwOnMissingValue: false);
            throw;
        }
    }

    public void Disable()
    {
        using RegistryKey? explicitLayers = Registry.CurrentUser.OpenSubKey(ExplicitLayersKey, writable: true);
        using RegistryKey? implicitLayers = Registry.CurrentUser.OpenSubKey(ImplicitLayersKey, writable: true);
        explicitLayers?.DeleteValue(LayerManifestPath, throwOnMissingValue: false);
        implicitLayers?.DeleteValue(OverrideManifestPath, throwOnMissingValue: false);
    }

    private string ValidateGame(string gamePath)
    {
        if (string.IsNullOrWhiteSpace(gamePath) || !File.Exists(gamePath))
            throw new FileNotFoundException("Select the DOOMEternalx64vk.exe path first.", gamePath);

        string fullPath = Path.GetFullPath(gamePath);
        if (!Path.GetFileName(fullPath).Equals("DOOMEternalx64vk.exe", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Automatic layer loading supports DOOMEternalx64vk.exe.");

        return fullPath;
    }

    private void PrepareRuntime()
    {
        string dllPath = Path.Combine(AppContext.BaseDirectory, "Native", "CollisionViewer", "bin", "DoomCollisionLayer.dll");
        if (!File.Exists(dllPath))
            throw new FileNotFoundException("Collision Viewer has not been built yet. Build Native\\CollisionViewer\\build-viewer.cmd.", dllPath);

        Directory.CreateDirectory(RuntimeDirectory);
        byte[] dllHash = SHA256.HashData(File.ReadAllBytes(dllPath));
        string runtimeDllName = $"DoomCollisionLayer-{Convert.ToHexString(dllHash)[..16].ToLowerInvariant()}.dll";
        string destinationDll = Path.Combine(RuntimeDirectory, runtimeDllName);
        // Keep each built layer version side by side. Windows cannot replace a
        // DLL while a game process has it loaded; the manifest can safely move
        // to the next version without touching the running game's module.
        if (!File.Exists(destinationDll))
            File.Copy(dllPath, destinationDll, overwrite: false);
        File.WriteAllText(LayerManifestPath, JsonSerializer.Serialize(new
        {
            file_format_version = "1.2.0",
            layer = new
            {
                name = LayerName,
                type = "GLOBAL",
                library_path = $@".\{runtimeDllName}",
                api_version = "1.3.0",
                implementation_version = "1",
                description = "DEViL collision viewer for DOOM Eternal",
                functions = new Dictionary<string, string> { ["vkNegotiateLoaderLayerInterfaceVersion"] = "DoomNegotiateLayerInterfaceVersion" }
            }
        }, new JsonSerializerOptions { WriteIndented = true }));
    }

    private void EnsureNoOtherOverrideRegistration()
    {
        foreach (RegistryHive hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
        {
            using RegistryKey baseKey = RegistryKey.OpenBaseKey(hive, RegistryView.Registry64);
            using RegistryKey? key = baseKey.OpenSubKey(ImplicitLayersKey);
            if (key is null) continue;

            foreach (string manifestPath in key.GetValueNames())
            {
                if (Path.GetFullPath(manifestPath).Equals(Path.GetFullPath(OverrideManifestPath), StringComparison.OrdinalIgnoreCase))
                    continue;
                if (!File.Exists(manifestPath)) continue;

                try
                {
                    using JsonDocument document = JsonDocument.Parse(File.ReadAllText(manifestPath));
                    if (document.RootElement.TryGetProperty("layer", out JsonElement layer) &&
                        layer.TryGetProperty("name", out JsonElement name) &&
                        name.GetString()?.Equals(OverrideName, StringComparison.Ordinal) == true)
                        throw new InvalidOperationException("Another Vulkan Override Layer registration was found. DEViL leaves it unchanged. Disable that override in Vulkan Configurator or launch the game with the DEViL button.");
                }
                catch (JsonException)
                {
                    // Ignore unrelated or malformed Vulkan manifests.
                }
            }
        }
    }
}
