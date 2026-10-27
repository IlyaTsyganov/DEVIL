using Devil.Configuration;

namespace Devil.Game;

public static class LabCommands
{
    public static bool Apply(DevilSettings settings, string[] args, int gameProcess, long timestamp)
    {
        if (args.Length == 0) return false;
        if (args[0] == "lab_clear" && args.Length == 1) { settings.ResearchReset = timestamp; return true; }
        if (args[0] == "lab_mark" && args.Length > 1)
        {
            settings.ResearchMarkerLabel = SettingsStore.CleanMarker(string.Join(' ', args.Skip(1)));
            settings.ResearchMarker = timestamp; return true;
        }
        if (args[0] == "lab_trigger" && args.Length == 3 && TriggerClassCatalog.IsValidName(args[1]) && args[2] is "0" or "1")
        { settings.SetTriggerClassVisible(args[1], args[2] == "1"); return true; }
        if (args.Length != 2 || !int.TryParse(args[1], out int number)) return false;
        if (args[0] == "lab_distance")
        {
            if (number is < 20 or > 200) return false;
            settings.ViewDistanceMeters = number; return true;
        }
        if (number is not (0 or 1)) return false;
        bool value = number == 1;
        switch (args[0])
        {
            case "lab_viewer": settings.CollisionViewerEnabled = value; break;
            case "lab_triggers": settings.ShowTriggers = value; break;
            case "lab_collision": settings.ShowWorldCollision = value; break;
            case "lab_walls": settings.ShowInvisibleWalls = value; break;
            case "lab_inspector": settings.InspectVolumes = value; break;
            case "lab_disabled": settings.ShowDisabledVolumes = value; break;
            case "lab_history": settings.ShowVolumeHistory = value; break;
            case "lab_encounters": settings.ShowActiveEncountersHud = value; break;
            case "lab_fill": settings.Style = value ? RenderStyle.Filled : RenderStyle.Outlines; break;
            case "lab_path": settings.ShowResearchPath = value; break;
            case "lab_player": settings.ShowPlayerCollider = value; settings.MotionProcess = gameProcess; break;
            case "lab_surface": settings.ShowSupportHud = value; settings.MotionProcess = gameProcess; break;
            case "lab_attempts": settings.ShowMotionPaths = value; settings.MotionProcess = gameProcess; break;
            case "lab_record": settings.RecordResearch = value; settings.ResearchProcess = gameProcess; break;
            default: return false;
        }
        return true;
    }
}
