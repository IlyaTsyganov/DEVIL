using System.Diagnostics;

namespace Devil.Game;

public sealed record ProcessSnapshot(bool GameRunning, int GameCount, bool DesruRunning, int DesruCount,
    DateTime CheckedAt)
{
    public bool GameStartedBeforeViewer { get; init; }
}

/// <summary>
/// Detects the game and DESRU by process metadata only. It never requests memory
/// access, writes to a process, or starts and stops another process.
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

    public ProcessSnapshot ReadSnapshot()
    {
        bool gameStartedBeforeViewer = false;
        int gameCount = CountProcesses(GameProcessNames, process =>
        {
            if (process.StartTime.ToUniversalTime() < _viewerStartedAtUtc)
                gameStartedBeforeViewer = true;
        });
        int desruCount = CountProcesses(DesruProcessNames);
        return new ProcessSnapshot(gameCount > 0, gameCount, desruCount > 0, desruCount, DateTime.Now)
        {
            GameStartedBeforeViewer = gameStartedBeforeViewer
        };
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
