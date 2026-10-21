using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text;

namespace Devil.Configuration;

public enum RenderStyle
{
    Outlines,
    Filled
}

public sealed class DevilSettings
{
    public string GameExecutablePath { get; set; } = string.Empty;
    public bool CollisionViewerEnabled { get; set; }
    public bool ShowTriggers { get; set; } = true;
    public bool ShowWorldCollision { get; set; } = true;
    public bool ShowInvisibleWalls { get; set; } = true;
    public RenderStyle Style { get; set; } = RenderStyle.Filled;
    public int ViewDistanceMeters { get; set; } = 40;
}

public sealed class SettingsStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() }
    };

    public string SettingsDirectory { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DEViL");

    public string SettingsPath => Path.Combine(SettingsDirectory, "settings.json");
    public string ViewerSettingsPath => Path.Combine(SettingsDirectory, "viewer.ini");

    public DevilSettings Load()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return new DevilSettings();
            return JsonSerializer.Deserialize<DevilSettings>(File.ReadAllText(SettingsPath), JsonOptions)
                ?? new DevilSettings();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new DevilSettings();
        }
    }

    public void Save(DevilSettings settings)
    {
        Directory.CreateDirectory(SettingsDirectory);
        string temporaryPath = SettingsPath + ".tmp";
        File.WriteAllText(temporaryPath, JsonSerializer.Serialize(settings, JsonOptions));
        File.Move(temporaryPath, SettingsPath, overwrite: true);
        string viewerIni = $"[Viewer]{Environment.NewLine}" +
            $"Enabled={(settings.CollisionViewerEnabled ? 1 : 0)}{Environment.NewLine}" +
            $"ShowTriggers={(settings.ShowTriggers ? 1 : 0)}{Environment.NewLine}" +
            $"ShowCollision={(settings.ShowWorldCollision ? 1 : 0)}{Environment.NewLine}" +
            $"ShowWalls={(settings.ShowInvisibleWalls ? 1 : 0)}{Environment.NewLine}" +
            $"CollisionMode={(settings.Style == RenderStyle.Filled ? 1 : 0)}{Environment.NewLine}" +
            $"Distance={Math.Clamp(settings.ViewDistanceMeters, 20, 200)}{Environment.NewLine}";
        string viewerTemporaryPath = ViewerSettingsPath + ".tmp";
        File.WriteAllText(viewerTemporaryPath, viewerIni, Encoding.Unicode);
        File.Move(viewerTemporaryPath, ViewerSettingsPath, overwrite: true);
    }
}
