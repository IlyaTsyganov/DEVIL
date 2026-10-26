namespace Devil.Game;

internal static class LightingFixPolicy
{
    internal const string EnvironmentVariable = "DEVIL_LIGHTING_FIX";

    internal static bool NeedsFix(string gamePath) => GameExecutableInfo.Read(gamePath)?.NeedsLightingFix == true;
}
