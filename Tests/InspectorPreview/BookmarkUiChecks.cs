using System.Drawing.Imaging;
using System.Reflection;
using System.Text;
using Devil.Game;

internal static class BookmarkUiChecks
{
    private const BindingFlags Private = BindingFlags.NonPublic | BindingFlags.Instance;
    private static T Field<T>(Form form, string name) => (T)form.GetType().GetField(name, Private)!.GetValue(form)!;
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Pump(Func<bool> ready)
    {
        long deadline = Environment.TickCount64 + 6000;
        while (!ready() && Environment.TickCount64 < deadline) { Application.DoEvents(); Thread.Sleep(5); }
        Check(ready(), "Bookmark/save UI did not finish its operation");
    }
    private static IEnumerable<Control> Descendants(Control parent)
    {
        foreach (Control child in parent.Controls) { yield return child; foreach (var nested in Descendants(child)) yield return nested; }
    }
    private static void Render(Form form, string path)
    {
        using var image = new Bitmap(form.Width, form.Height); form.DrawToBitmap(image, new Rectangle(Point.Empty, form.Size)); image.Save(path, ImageFormat.Png);
    }
    private static void Layout(Form form)
    {
        form.Size = form.MinimumSize; Application.DoEvents();
        foreach (var panel in Descendants(form).OfType<FlowLayoutPanel>())
            Check(panel.Controls.Cast<Control>().Where(c => c.Visible).All(c => c.Right <= panel.ClientSize.Width && c.Bottom <= panel.ClientSize.Height), "Point/save toolbar clipped at minimum size");
        foreach (var label in Descendants(form).OfType<Label>().Where(l => l.Dock == DockStyle.Fill && !l.AutoEllipsis))
            Check(label.GetPreferredSize(new Size(label.Width, int.MaxValue)).Height <= label.Height, "Point/save caption clipped: " + label.Text);
    }
    internal static void Run(string parent, string previews)
    {
        string directory = Path.Combine(parent, "bookmarks-ui"); Directory.CreateDirectory(directory); int process = 1234;
        var type = Assembly.Load("DEViL").GetType("Devil.ResearchPointsForm", true)!;
        using var form = (Form)Activator.CreateInstance(type, directory, (Func<int>)(() => process))!;
        form.Opacity = 0; form.Show(); Application.DoEvents(); Field<System.Windows.Forms.Timer>(form, "_timer").Stop();
        void Poll() { var task = (Task)type.GetMethod("Poll", Private)!.Invoke(form, null)!; Pump(() => task.IsCompleted); task.GetAwaiter().GetResult(); }
        void Reply(string token, uint entity, string identity = "500:600", float x = 1)
        {
            File.WriteAllLines(BookmarkObservation.FilePath(directory, process), [
                $"#DEVIL_POINT_V1\t{process}\t{token}\t{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}\tready\tgame/dlc/e4m1_rig/e4m1_rig\tDOOM Eternal 6.66 Rev 3\t{identity}\tknown\tcp_01a",
                FormattableString.Invariant($"P\t0\t0\t0\t{x}\t2\t3\t0\t1\t0\t1\t0\t0\t-1\t0\t0\t1\t-3\t0\t0\t0\t0\t0\t1\t0\t-2"),
                $"E\t{entity}\t3\ttrigger/interact/{entity}\tidTrigger\tenabled\t200"]);
        }
        Field<TextBox>(form, "_name").Text = "Landing pad — before the route trick"; Field<ComboBox>(form, "_category").Text = "Any% Restricted";
        Field<TextBox>(form, "_note").Text = "Hypothesis: skirt the trigger boundary. Keep the prepared checkpoint archive separate from position-only returns.";
        Field<Button>(form, "_capture").PerformClick();
        string token = Field<string>(form, "_pending"); Check(!Field<ListBox>(form, "_points").Enabled, "Point selection allowed while capture pending");
        Reply(new('b', 32), 100); Poll(); Check(Field<string?>(form, "_pending") == token, "Stale token satisfied point capture");
        Reply(token, 100); Poll(); var saved = Field<ResearchBookmark>(form, "_selected");
        Check(saved.Name.StartsWith("Landing pad") && saved.Observation.Body.Z == 0 && saved.Observation.Eye.Z == 3 && saved.Observation.Entities.Count == 1, "Captured point missing metadata/body/eye/selection");
        Field<Button>(form, "_attach").PerformClick(); Reply(Field<string>(form, "_pending"), 101, x: 50); Poll();
        saved = Field<ResearchBookmark>(form, "_selected"); Check(saved.Observation.Entities.Count == 2 && saved.Observation.Eye.X == 1, "Attaching volume changed saved location");
        Field<Button>(form, "_attach").PerformClick(); Reply(Field<string>(form, "_pending"), 102, "700:800"); Poll();
        Check(Field<ResearchBookmark>(form, "_selected").Observation.Entities.Count == 2 && Field<Label>(form, "_status").Text.Contains("original point"), "Attachment crossed map instance");
        Field<TextBox>(form, "_note").Text += " Edited note."; type.GetMethod("SaveEdits", Private)!.Invoke(form, null);
        Check(new BookmarkStore(directory).Load().Single().Note.EndsWith("Edited note."), "Point edit not saved");
        Field<TextBox>(form, "_search").Text = "does not exist"; Check(Field<ListBox>(form, "_points").Items.Count == 0, "Point search failed");
        Field<TextBox>(form, "_search").Clear(); Field<ListBox>(form, "_points").SelectedIndex = 0;
        Render(form, Path.Combine(previews, "research-points-window.png")); Layout(form); Render(form, Path.Combine(previews, "research-points-minimum.png"));
        Field<Button>(form, "_capture").PerformClick(); process = 5678; Poll(); Check(Field<string?>(form, "_pending") is null && Field<Label>(form, "_status").Text.Contains("canceled"), "Process change retained pending point");
        form.Close();
        SaveUi(directory, previews);
        SimpleSaveUiChecks.Run(directory, previews);
        Console.WriteLine("PASS: research-point UI capture/token/PID scope, persisted names/categories/notes, selected volumes, attachment map scope, search and minimum layout; save UI full capture/validation/linking and blocked restore guards.");
    }
    private static void SaveUi(string directory, string previews)
    {
        string remote = Path.Combine(directory, "Steam", "userdata", "10001", "782330", "remote"); Directory.CreateDirectory(remote);
        var location = SteamSaveLocation.Parse(remote); string slot = location.SlotPath("GAME-AUTOSAVE0"); Directory.CreateDirectory(slot);
        foreach (string name in PracticeSaveVault.RequiredFiles) File.WriteAllBytes(Path.Combine(slot, name), SteamSaveContainer.Seal(Encoding.UTF8.GetBytes("fixture " + name), location.Account, name));
        IReadOnlyList<string> running = []; var vault = new PracticeSaveVault(directory, () => running); PracticeSave? linked = null;
        var type = Assembly.Load("DEViL").GetType("Devil.PracticeSaveAdvancedForm", true)!;
        using var form = (Form)Activator.CreateInstance(type, directory, (Action<PracticeSave>)(save => linked = save), vault, null, null)!;
        form.Opacity = 0; form.Show(); Application.DoEvents();
        void Invoke(string name) { var task = (Task)type.GetMethod(name, Private)!.Invoke(form, null)!; Pump(() => task.IsCompleted); task.GetAwaiter().GetResult(); }
        Field<ComboBox>(form, "_remote").Text = remote; Field<TextBox>(form, "_name").Text = "Landing pad practice checkpoint";
        Field<TextBox>(form, "_category").Text = "Any% Restricted"; Field<TextBox>(form, "_map").Text = "UAC Atlantica — user annotation";
        Invoke("Inspect"); Check(Field<Label>(form, "_status").Text.Contains("All four containers"), "Save inspection failed");
        Invoke("CaptureSlot"); Check(Field<DataGridView>(form, "_grid").RowCount == 1 && vault.List().Count == 1, "Save UI did not create full archive");
        Invoke("Verify"); Check(Field<Label>(form, "_status").Text.Contains("Verified 4"), "Save verification status incorrect");
        Invoke("Link"); Check(linked?.Name == "Landing pad practice checkpoint", "Verified archive was not linked");
        Invoke("Restore"); Check(Field<Label>(form, "_status").Text.Contains("Steam Cloud"), "Save UI restore did not require Cloud guard");
        Field<CheckBox>(form, "_cloud").Checked = true; running = ["steam"]; Invoke("Restore");
        Check(Field<Label>(form, "_status").Text.Contains("Close DOOM Eternal and Steam"), "Save UI allowed a running client"); running = [];
        Invoke("CaptureLive"); Check(Field<Label>(form, "_status").Text.Contains("Invalid live checkpoint request"), "Live capture accepted a missing selected game");
        Invoke("RestoreLive"); Check(Field<Label>(form, "_status").Text.Contains("Select the running game"), "Live restore accepted a missing selected game");
        Field<TextBox>(form, "_search").Text = "other category"; Check(Field<DataGridView>(form, "_grid").RowCount == 0, "Practice-save search failed"); Field<TextBox>(form, "_search").Clear();
        Render(form, Path.Combine(previews, "practice-saves-window.png")); Layout(form); Render(form, Path.Combine(previews, "practice-saves-minimum.png"));
        form.Close();
    }
}
