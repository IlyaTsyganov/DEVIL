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
    public bool InspectVolumes { get; set; }
    public bool ShowDisabledVolumes { get; set; }
    public bool ShowVolumeHistory { get; set; }
    public bool ShowActiveEncountersHud { get; set; }
    public bool ShowResearchPath { get; set; } = true;
    public bool RecordResearch { get; set; }
    public int ResearchProcess { get; set; }
    public long ResearchReset { get; set; }
    public long ResearchMarker { get; set; }
    public string ResearchMarkerLabel { get; set; } = "";
    public int MotionProcess { get; set; }
    public bool ShowPlayerCollider { get; set; }
    public bool ShowSupportHud { get; set; }
    public bool ShowMotionPaths { get; set; } = true;
    public bool MotionFollowTarget { get; set; } = true;
    public List<string> HiddenTriggerClasses { get; set; } = [];
    public List<TriggerPathInfo> HiddenTriggerPaths { get; set; } = [];
    public RenderStyle Style { get; set; } = RenderStyle.Filled;
    public int ViewDistanceMeters { get; set; } = 40;

    public bool IsTriggerClassVisible(string className) => !(HiddenTriggerClasses?.Contains(className, StringComparer.Ordinal) ?? false);

    public void SetTriggerClassVisible(string className, bool visible)
    {
        if (!TriggerClassCatalog.IsValidName(className)) return;
        HiddenTriggerClasses ??= [];
        HiddenTriggerClasses.RemoveAll(name => name == className);
        if (!visible) HiddenTriggerClasses.Add(className);
    }

    public bool IsTriggerPathExplicitlyVisible(string className, string prefix) => !(HiddenTriggerPaths ?? []).Contains(new(className, prefix));
    public bool IsTriggerPathVisible(string className, string prefix) => IsTriggerClassVisible(className)
        && TriggerPathCatalog.Ancestors(prefix).All(parent => IsTriggerPathExplicitlyVisible(className, parent));
    public void SetTriggerPathVisible(string className, string prefix, bool visible)
    {
        var item = new TriggerPathInfo(className, TriggerPathCatalog.Normalize(prefix));
        if (!TriggerPathCatalog.IsValid(item)) return;
        HiddenTriggerPaths ??= [];
        HiddenTriggerPaths.RemoveAll(existing => existing == item);
        if (!visible) HiddenTriggerPaths.Add(item);
    }

    internal void NormalizeTriggerClasses()
    {
        HiddenTriggerClasses = (HiddenTriggerClasses ?? []).Where(TriggerClassCatalog.IsValidName).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToList();
        HiddenTriggerPaths = (HiddenTriggerPaths ?? []).Where(item => item is not null)
            .Select(item => item with { Prefix = TriggerPathCatalog.Normalize(item.Prefix ?? "") })
            .Where(TriggerPathCatalog.IsValid).Distinct().OrderBy(item => item.ClassName, StringComparer.Ordinal)
            .ThenBy(item => item.Prefix, StringComparer.Ordinal).Take(2048).ToList();
    }
}

public sealed class SettingsStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() }
    };

    public SettingsStore(string? settingsDirectory = null) => SettingsDirectory = settingsDirectory ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DEViL");

    public string SettingsDirectory { get; }

    public string SettingsPath => Path.Combine(SettingsDirectory, "settings.json");
    public string ViewerSettingsPath => Path.Combine(SettingsDirectory, "viewer.ini");
    public string TriggerClassesPath => Path.Combine(SettingsDirectory, "trigger-classes.txt");
    public string TriggerPathsPath => Path.Combine(SettingsDirectory, "trigger-paths.tsv");
    public string ResearchCurrentPath => Path.Combine(SettingsDirectory, "research-current.txt");

    public DevilSettings Load()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return new DevilSettings();
            var settings = JsonSerializer.Deserialize<DevilSettings>(File.ReadAllText(SettingsPath), JsonOptions)
                ?? new DevilSettings();
            settings.NormalizeTriggerClasses();
            return settings;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new DevilSettings();
        }
    }

    public void Save(DevilSettings settings)
    {
        settings.NormalizeTriggerClasses();
        Directory.CreateDirectory(SettingsDirectory);
        string temporaryPath = SettingsPath + ".tmp";
        File.WriteAllText(temporaryPath, JsonSerializer.Serialize(settings, JsonOptions));
        File.Move(temporaryPath, SettingsPath, overwrite: true);
        string viewerIni = $"[Viewer]{Environment.NewLine}" +
            $"Enabled={(settings.CollisionViewerEnabled ? 1 : 0)}{Environment.NewLine}" +
            $"ShowTriggers={(settings.ShowTriggers ? 1 : 0)}{Environment.NewLine}" +
            $"ShowCollision={(settings.ShowWorldCollision ? 1 : 0)}{Environment.NewLine}" +
            $"ShowWalls={(settings.ShowInvisibleWalls ? 1 : 0)}{Environment.NewLine}" +
            $"InspectVolumes={(settings.InspectVolumes ? 1 : 0)}{Environment.NewLine}" +
            $"ShowDisabledVolumes={(settings.ShowDisabledVolumes ? 1 : 0)}{Environment.NewLine}" +
            $"ShowVolumeHistory={(settings.ShowVolumeHistory ? 1 : 0)}{Environment.NewLine}" +
            $"ShowActiveEncountersHud={(settings.ShowActiveEncountersHud ? 1 : 0)}{Environment.NewLine}" +
            $"ShowResearchPath={(settings.ShowResearchPath ? 1 : 0)}{Environment.NewLine}" +
            $"RecordResearch={(settings.RecordResearch ? 1 : 0)}{Environment.NewLine}" +
            $"ResearchProcess={settings.ResearchProcess}{Environment.NewLine}" +
            $"ResearchReset={settings.ResearchReset}{Environment.NewLine}" +
            $"ResearchMarker={settings.ResearchMarker}{Environment.NewLine}" +
            $"ResearchMarkerLabel={CleanMarker(settings.ResearchMarkerLabel)}{Environment.NewLine}" +
            $"MotionProcess={settings.MotionProcess}{Environment.NewLine}" +
            $"ShowPlayerCollider={(settings.ShowPlayerCollider ? 1 : 0)}{Environment.NewLine}" +
            $"ShowSupportHud={(settings.ShowSupportHud ? 1 : 0)}{Environment.NewLine}" +
            $"ShowMotionPaths={(settings.ShowMotionPaths ? 1 : 0)}{Environment.NewLine}" +
            $"MotionFollowTarget={(settings.MotionFollowTarget ? 1 : 0)}{Environment.NewLine}" +
            $"CollisionMode={(settings.Style == RenderStyle.Filled ? 1 : 0)}{Environment.NewLine}" +
            $"Distance={Math.Clamp(settings.ViewDistanceMeters, 20, 200)}{Environment.NewLine}";
        viewerIni += $"{Environment.NewLine}[TriggerClasses]{Environment.NewLine}";
        foreach (string className in settings.HiddenTriggerClasses)
            viewerIni += $"{className}=0{Environment.NewLine}";
        viewerIni += $"{Environment.NewLine}[TriggerPaths]{Environment.NewLine}";
        foreach (var path in settings.HiddenTriggerPaths)
            viewerIni += $"{path.ClassName}|{path.Prefix}=0{Environment.NewLine}";
        string viewerTemporaryPath = ViewerSettingsPath + ".tmp";
        File.WriteAllText(viewerTemporaryPath, viewerIni, Encoding.Unicode);
        File.Move(viewerTemporaryPath, ViewerSettingsPath, overwrite: true);
    }

    public static string CleanMarker(string? text) => new((text ?? "").Where(c => !char.IsControl(c)).Take(160).ToArray());
}
