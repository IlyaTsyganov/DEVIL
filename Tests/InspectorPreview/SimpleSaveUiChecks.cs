using System.Drawing.Imaging;
using System.Reflection;
using System.Text;
using Devil.Game;

internal static class SimpleSaveUiChecks
{
    private const BindingFlags Private = BindingFlags.NonPublic | BindingFlags.Instance;
    private static T Field<T>(Form form, string name) => (T)form.GetType().GetField(name, Private)!.GetValue(form)!;
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Pump(Func<bool> ready)
    {
        long end = Environment.TickCount64 + 6000;
        while (!ready() && Environment.TickCount64 < end) { Application.DoEvents(); Thread.Sleep(5); }
        Check(ready(), "Simple save UI fixture timed out");
    }
    private static void Render(Form form, string file)
    { using var bitmap = new Bitmap(form.Width, form.Height); form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size)); bitmap.Save(file, ImageFormat.Png); }
    internal static void Run(string parent, string previews)
    {
        string directory = Path.Combine(parent, "simple-save-ui"), remote = Path.Combine(directory, "Steam", "userdata", "10001", "782330", "remote");
        Directory.CreateDirectory(remote); var location = SteamSaveLocation.Parse(remote);
        string slot = location.SlotPath(PracticeSaveSource.LiveSlot); Directory.CreateDirectory(slot);
        var originals = PracticeSaveVault.RequiredFiles.ToDictionary(name => name, name => SteamSaveContainer.Seal(Encoding.UTF8.GetBytes("fixture:" + name), location.Account, name));
        foreach (var pair in originals) File.WriteAllBytes(Path.Combine(slot, pair.Key), pair.Value);
        var vault = new PracticeSaveVault(directory, () => ["DOOMEternalx64vk", "steam"]); int process = Environment.ProcessId;
        var type = Assembly.Load("DEViL").GetType("Devil.PracticeSaveForm", true)!;
        using var form = (Form)Activator.CreateInstance(type, directory, null, vault, null, (Func<int>)(() => process))!;
        form.Opacity = 0; form.Show(); Application.DoEvents(); Field<System.Windows.Forms.Timer>(form, "_timer").Stop();
        type.GetMethod("UseSource", Private)!.Invoke(form, [remote]);
        var save = Field<Button>(form, "_save"); var load = Field<Button>(form, "_load"); var grid = Field<DataGridView>(form, "_grid");
        Check(save.Enabled && !load.Enabled, "Empty state enables load or disables save");
        string stem = Path.Combine(directory, $"practice-live-{process}"), request = stem + ".request";
        Task Call(string method) => (Task)type.GetMethod(method, Private)!.Invoke(form, null)!;
        string[] Request(string op) { Pump(() => File.Exists(request) && File.ReadAllText(request).Contains("\t" + op + "\t")); return File.ReadAllText(request).TrimEnd().Split('\t'); }
        void Reply(string token, string phase, string detail = "fixture checkpoint result") => File.WriteAllText(stem + ".tsv",
            $"#DEVIL_LIVE_PRACTICE_V1\t{process}\t{token}\t{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}\t{phase}\t{location.Account}\tDLC1-AUTOSAVE5\tgame/dlc/e4m1_rig/e4m1_rig\tcp_01a\t{detail}");
        var capture = Call("SaveCheckpoint"); var h = Request("archive");
        Check(!save.Enabled && !load.Enabled && !grid.Enabled, "Controls enabled during capture");
        string captureFolder = Path.Combine(directory, "live-practice", process.ToString(), h[2], "slot"); Directory.CreateDirectory(captureFolder);
        foreach (var pair in originals) File.WriteAllBytes(Path.Combine(captureFolder, pair.Key), pair.Value);
        Reply(h[2], "archived"); Pump(() => capture.IsCompleted); capture.GetAwaiter().GetResult();
        var archive = vault.List().Single();
        Check(archive.Name.StartsWith("Сохранение — ") && grid.RowCount == 1 && load.Enabled && grid.CurrentCell?.RowIndex == 0, "Captured save was not named and selected automatically");
        Field<TextBox>(form, "_search").Text = "no matching save"; Check(grid.RowCount == 0 && !load.Enabled, "Empty search left load enabled"); Field<TextBox>(form, "_search").Clear();
        File.Delete(request);
        // Real click must immediately submit restore, with no intermediate confirmation dialog.
        load.PerformClick(); h = Request("restore");
        string bundle = Path.Combine(directory, "live-practice", "restore-pending", process.ToString(), h[2]);
        string[] plan = File.ReadAllLines(Path.Combine(bundle, "plan.tsv"));
        Check(plan.Length == 9 && plan[0].EndsWith("\t" + archive.Id), "Load staged a different archive or incomplete plan");
        Reply(h[2], "loading"); Pump(() => Field<Label>(form, "_status").Text.Contains("100%"));
        Reply(h[2], "reloaded"); Pump(() => load.Enabled);
        Check(Field<Label>(form, "_status").Text.StartsWith("Загружено:"), "One-click load did not finish");
        foreach (var pair in originals) Check(File.ReadAllBytes(Path.Combine(slot, pair.Key)).SequenceEqual(pair.Value), "UI fixture unexpectedly changed target checkpoint");
        Application.DoEvents(); form.Refresh();
        Check(grid.Visible && grid.Height > 100 && grid.Parent!.Visible, "Saved entry list not visible");
        Render(form, Path.Combine(previews, "simple-practice-saves-window.png"));
        form.Size = form.MinimumSize; Application.DoEvents(); Render(form, Path.Combine(previews, "simple-practice-saves-minimum.png"));
        foreach (var label in Descendants(form).OfType<Label>().Where(l => l.Dock == DockStyle.Fill && !l.AutoEllipsis && l.Visible))
            Check(label.GetPreferredSize(new Size(label.Width, int.MaxValue)).Height <= label.Height, "Simple save caption clipped: " + label.Text);
        process = 0; type.GetMethod("RefreshState", Private)!.Invoke(form, null); Check(!save.Enabled && !load.Enabled, "No-game state allowed save/load");
        string Error(string text) => (string)type.GetMethod("FriendlyError", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, [text])!;
        Check(Error("Pause first. Steam Cloud flags: account=0 app=1").StartsWith("Поставьте"), "Cloud diagnostic suffix hid real pause error");
        Check(Error("rollback failed; original checkpoint path retained").Contains("исходные файлы"), "Rollback failure hidden");
        Check(Error("Files installed, but engine/account/Cloud changed before reload.").Contains("Файлы установлены"), "Post-install failure was mistaken for a retryable Cloud guard");
        form.Close();
        Console.WriteLine("PASS: simple saves UI uses production archive/restore protocol with isolated encrypted fixtures; blank name, auto-selection, search, one-click load, progress, disabled controls and minimum layout. User saves untouched.");
    }
    private static IEnumerable<Control> Descendants(Control parent)
    { foreach (Control child in parent.Controls) { yield return child; foreach (var nested in Descendants(child)) yield return nested; } }
}
