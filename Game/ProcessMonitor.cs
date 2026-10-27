using System.Diagnostics;

namespace Devil.Game;

public sealed record ProcessSnapshot(bool GameRunning, int GameCount, bool DesruRunning, int DesruCount,
    DateTime CheckedAt)
{
    public bool GameStartedBeforeViewer { get; init; }
    public bool ViewerVersionChecked { get; init; }
    public bool ViewerUpdateRequired { get; init; }
    public string[] LoadedViewerLibraries { get; init; } = [];
}

/// <summary>
/// Detects the game, DESRU and loaded viewer build using read-only process/module
/// metadata. It never writes to, starts or stops another process.
/// </summary>
public sealed class ProcessMonitor
{
    private readonly DateTime _viewerStartedAtUtc = GetViewerStartTimeUtc();

    private static DateTime GetViewerStartTimeUtc()
    {
        using Process viewer = Process.GetCurrentProcess();
        return viewer.StartTime.ToUniversalTime();
    }

    private static readonly string[] GameProcessNames =
    [
        "DOOMEternalx64vk",
        "DOOMEternalx64",
        "DOOMEternal"
    ];

    private static readonly string[] DesruProcessNames =
    [
        "DESRU",
        "DESpeedrunUtil"
    ];

    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    public ProcessSnapshot ReadSnapshot(string? expectedViewerLibrary = null)
    {
        bool gameStartedBeforeViewer = false;
        int inspected = 0;
        bool outdated = false;
        var loadedViewers = new List<string>();
        int gameCount = CountProcesses(GameProcessNames, process =>
        {
            if (process.StartTime.ToUniversalTime() < _viewerStartedAtUtc)
                gameStartedBeforeViewer = true;
            if (string.IsNullOrEmpty(expectedViewerLibrary)) return;
            var names = process.Modules.Cast<ProcessModule>().Select(module => module.ModuleName)
                .Where(IsViewerLibrary).ToArray();
            loadedViewers.AddRange(names);
            ++inspected;
            outdated |= !UsesExpectedViewer(names, expectedViewerLibrary);
        });
        int desruCount = CountProcesses(DesruProcessNames);
        return new ProcessSnapshot(gameCount > 0, gameCount, desruCount > 0, desruCount, DateTime.Now)
        {
            GameStartedBeforeViewer = gameStartedBeforeViewer,
            ViewerVersionChecked = gameCount > 0 && inspected == gameCount,
            ViewerUpdateRequired = outdated,
            LoadedViewerLibraries = loadedViewers.ToArray()
        };
    }

    private static bool IsViewerLibrary(string name) =>
        name.StartsWith("DoomCollisionLayer", StringComparison.OrdinalIgnoreCase) &&
        name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase);

    internal static bool UsesExpectedViewer(IEnumerable<string> loadedModules, string expectedLibrary)
    {
        string[] viewers = loadedModules.Where(IsViewerLibrary).ToArray();
        return viewers.Length == 1 && viewers[0].Equals(expectedLibrary, StringComparison.OrdinalIgnoreCase);
    }

    public bool IsGameRunning => CountProcesses(GameProcessNames) > 0;

    private static int CountProcesses(IEnumerable<string> names, Action<Process>? inspect = null)
    {
        int count = 0;
        foreach (string name in names)
        {
            try
            {
                Process[] processes = Process.GetProcessesByName(name);
                count += processes.Length;
                foreach (Process process in processes)
                {
                    using (process)
                    {
                        try { inspect?.Invoke(process); }
                        catch (InvalidOperationException) { } // Exited during inspection.
                        catch (System.ComponentModel.Win32Exception) { } // Metadata unavailable.
                    }
                }
            }
            catch (InvalidOperationException)
            {
                // A process can exit while the snapshot is being collected.
            }
            catch (System.ComponentModel.Win32Exception)
            {
                // A process may not be visible in the current security context.
            }
        }
        return count;
    }
}
