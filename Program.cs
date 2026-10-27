namespace Devil;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        using var form = new MainForm();
        using var commands = new Game.LabCommandServer(form.ReceiveLabCommand);
        Application.Run(form);
        form.StopResearchOnExit();
    }
}
