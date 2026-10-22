using System.Diagnostics;

namespace Devil.Game;

public sealed record ProcessSnapshot(bool GameRunning, int GameCount, bool DesruRunning, int DesruCount,
    DateTime CheckedAt);

/// <summary>
/// Detects the game and DESRU by process metadata only. It never requests memory
/// access, writes to a process, or starts and stops another process.
/// </summary>
public sealed class ProcessMonitor
{
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
        int gameCount = CountProcesses(GameProcessNames);
        int desruCount = CountProcesses(DesruProcessNames);
        return new ProcessSnapshot(gameCount > 0, gameCount, desruCount > 0, desruCount, DateTime.Now);
    }

    public bool IsGameRunning => CountProcesses(GameProcessNames) > 0;

    private static int CountProcesses(IEnumerable<string> names)
    {
        int count = 0;
        foreach (string name in names)
        {
            try
            {
                Process[] processes = Process.GetProcessesByName(name);
                count += processes.Length;
                foreach (Process process in processes) process.Dispose();
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
