using Devil.Game;

namespace Devil;

internal sealed class ResearchPointsForm : Form
{
    private readonly string _directory;
    private readonly Func<int> _gameProcess;
    private readonly BookmarkStore _store;
    private readonly TextBox _search = new() { Width = 440, PlaceholderText = "Search category, map, name, checkpoint or note…" };
    private readonly ListBox _points = new() { Dock = DockStyle.Fill, HorizontalScrollbar = true };
    private readonly TextBox _name = new() { Dock = DockStyle.Fill, MaxLength = 160 };
    private readonly ComboBox _category = new() { Dock = DockStyle.Fill, MaxLength = 160 };
    private readonly TextBox _note = new() { Dock = DockStyle.Fill, Multiline = true, MaxLength = 8192, ScrollBars = ScrollBars.Vertical };
    private readonly TextBox _details = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly Label _status = new() { Dock = DockStyle.Fill, AutoEllipsis = true, UseMnemonic = false };
    private readonly Label _linked = new() { Dock = DockStyle.Fill, UseMnemonic = false, AutoEllipsis = true };
    private readonly Button _capture = MakeButton("Capture new point", 160);
    private readonly Button _attach = MakeButton("Attach recent aim", 175);
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 250 };
    private ResearchBookmark? _selected;
    private IReadOnlyList<ResearchBookmark> _all = [];
    private string? _pending;
    private int _pendingProcess;
    private long _deadline;
    private bool _attaching, _busy, _populating;
    private PracticeSaveForm? _vault;

    public ResearchPointsForm(string directory, Func<int> gameProcess)
    {
        _directory = directory; _gameProcess = gameProcess; _store = new(directory);
        Text = "DEViL — Research points & practice presets"; ClientSize = new(1240, 800); MinimumSize = new(1120, 740);
        StartPosition = FormStartPosition.CenterParent; ShowInTaskbar = false; AutoScaleMode = AutoScaleMode.Font;
        BackColor = Color.FromArgb(35, 35, 35); ForeColor = Color.Gainsboro; Font = new Font("Segoe UI", 10f);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 1, RowCount = 6 };
        foreach (float height in new[] { 40f, 38f }) layout.RowStyles.Add(new(SizeType.Absolute, height));
        layout.RowStyles.Add(new(SizeType.Percent, 100)); layout.RowStyles.Add(new(SizeType.Absolute, 38)); layout.RowStyles.Add(new(SizeType.Absolute, 30)); layout.RowStyles.Add(new(SizeType.Absolute, 90)); Controls.Add(layout);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        _capture.Click += (_, _) => BeginCapture(false); _attach.Click += (_, _) => BeginCapture(true);
        var save = MakeButton("Save edits", 110); save.Click += (_, _) => SaveEdits();
        var import = MakeButton("Import point…", 130); import.Click += (_, _) => ImportPoint();
        var export = MakeButton("Export point…", 130); export.Click += (_, _) => ExportPoint();
        var vault = MakeButton("Practice saves…", 150); vault.Click += (_, _) => OpenVault();
        actions.Controls.AddRange([_capture, _attach, save, import, export, vault]); layout.Controls.Add(actions, 0, 0);
        layout.Controls.Add(_search, 0, 1); _search.TextChanged += (_, _) => Populate(_selected?.Id);
        var split = new SplitContainer { Dock = DockStyle.Fill, Size = new(1200, 500), SplitterDistance = 370, Panel1MinSize = 230, Panel2MinSize = 470, FixedPanel = FixedPanel.Panel1 };
        layout.Controls.Add(split, 0, 2); split.Panel1.Controls.Add(_points);
        var editor = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 6, Padding = new Padding(8, 0, 0, 0) };
        editor.ColumnStyles.Add(new(SizeType.Absolute, 90)); editor.ColumnStyles.Add(new(SizeType.Percent, 100));
        foreach (float height in new[] { 36f, 36f, 36f }) editor.RowStyles.Add(new(SizeType.Absolute, height));
        editor.RowStyles.Add(new(SizeType.Percent, 40)); editor.RowStyles.Add(new(SizeType.Percent, 60)); editor.RowStyles.Add(new(SizeType.Absolute, 26)); split.Panel2.Controls.Add(editor);
        void Row(string label, Control control, int row) { editor.Controls.Add(new Label { Text = label, AutoSize = true, Margin = new Padding(0, 5, 0, 0) }, 0, row); editor.Controls.Add(control, 1, row); }
        Row("Name", _name, 0); Row("Category", _category, 1); Row("Save link", _linked, 2); Row("Hypothesis /\nroute notes", _note, 3);
        editor.Controls.Add(_details, 0, 4); editor.SetColumnSpan(_details, 2);
        editor.Controls.Add(new Label { Text = "Categories and setup notes are user-defined; they do not apply inventory upgrades.", Dock = DockStyle.Fill, Font = new(Font.FontFamily, 8.5f) }, 0, 5); editor.SetColumnSpan(editor.Controls[^1], 2);
        _category.Items.AddRange(["Research", "Any% Restricted", "100% Restricted (master heatblast)"]); _category.Text = "Research"; _name.Text = "New research point";
        var copy = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        foreach (var (text, width, content) in new (string, int, Func<ResearchBookmark, string>)[] {
            ("Copy position / aim", 180, p => p.PositionText()), ("Copy return command", 190, p => p.ReturnCommand()), ("Copy spawn info", 165, p => p.SpawnInfo()) })
        {
            var button = MakeButton(text, width); button.Click += (_, _) => { if (_selected is { } point) { try { Clipboard.SetText(content(point)); _status.Text = "Copied. Return command changes position/aim only; use on the matching map/build. It has not been executed."; } catch (System.Runtime.InteropServices.ExternalException) { _status.Text = "Clipboard is busy; retry."; } } }; copy.Controls.Add(button);
        }
        layout.Controls.Add(copy, 0, 3); layout.Controls.Add(_status, 0, 4);
        layout.Controls.Add(new Label { Dock = DockStyle.Fill, Font = new(Font.FontFamily, 9f), UseMnemonic = false, Text = "A point stores the physical player origin, rendered camera eye/aim, full view-projection matrix, loaded checkpoint, selected volumes and your notes. Keep DEViL open to capture.\nReturn commands use the captured camera eye and yaw/pitch (roll zero); test them in a practice slot. They do not restore enemies, items, cooldowns, scripts or progress.\nPractice saves are separate full checkpoint-file archives, restored offline to an existing slot. A linked save is not guaranteed to match the point's exact moment.\nLoaded checkpoint is spawn-info data, not proof of a new save. Entity IDs/generations belong to the original map instance; camera/body data are a polled observation." }, 0, 5);
        _points.SelectedIndexChanged += (_, _) => { if (!_populating) SelectPoint(_points.SelectedItem as ResearchBookmark); };
        _timer.Tick += async (_, _) => await Poll(); _timer.Start(); FormClosing += (_, _) => _timer.Stop();
        Reload();
    }
    private static Button MakeButton(string text, int width) => new() { Text = text, Size = new(width, 30), FlatStyle = FlatStyle.Flat,
        BackColor = Color.FromArgb(65, 65, 65), ForeColor = Color.Gainsboro, Margin = new(0, 0, 8, 0) };
    private void Reload(string? id = null) { _all = _store.Load(); Populate(id ?? _selected?.Id); }
    private void Populate(string? id)
    {
        _populating = true; _points.BeginUpdate(); _points.Items.Clear();
        string query = _search.Text.Trim(); foreach (var point in _all.Where(p => $"{p.Name} {p.Category} {p.Note} {p.Observation.Map} {p.Observation.Profile} {p.Observation.Checkpoint}".Contains(query, StringComparison.OrdinalIgnoreCase))) _points.Items.Add(point);
        _points.SelectedItem = _points.Items.Cast<ResearchBookmark>().FirstOrDefault(p => p.Id == id); _points.EndUpdate(); _populating = false;
        SelectPoint(_points.SelectedItem as ResearchBookmark);
    }
    private void SelectPoint(ResearchBookmark? point)
    {
        _selected = point; _attach.Enabled = _pending is null && point is not null;
        if (point is null) { _details.Text = "Capture a new point or select a saved one.\r\nEditing a point requires Save edits."; _linked.Text = "No selected point"; return; }
        _name.Text = point.Name; _category.Text = point.Category; _note.Text = point.Note;
        _linked.Text = point.SaveId.Length == 0 ? "None — choose Practice saves… to link an archive" : point.SaveId;
        var o = point.Observation;
        _details.Text = $"{o.Map}\r\n{o.Profile} · {DateTimeOffset.FromUnixTimeMilliseconds(o.Captured).ToLocalTime():yyyy-MM-dd HH:mm:ss.fff}\r\n{point.PositionText().Replace("\n", "\r\n")}\r\nLoaded checkpoint: {(o.CheckpointStatus == "known" ? o.Checkpoint : o.CheckpointStatus)}\r\nOriginal map/world: {o.Identity} · PID {o.Process}\r\nSelected volumes ({o.Entities.Count}):\r\n"
            + string.Join("\r\n", o.Entities.Select(e => $"{e.Name} · {e.Class} · {e.Key} · {e.State} · aim age at capture {e.AimAgeMilliseconds} ms"));
    }
    private void SaveEdits()
    {
        if (_selected is null || _pending is not null) return;
        try { var edited = _selected with { Name = _name.Text.Trim(), Category = _category.Text.Trim(), Note = _note.Text }; _store.Save(edited); Reload(edited.Id); _status.Text = "Research point saved."; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = ex.Message; }
    }
    private void BeginCapture(bool attach)
    {
        int process = _gameProcess(); if (_pending is not null || attach && _selected is null) return;
        if (process <= 0) { _status.Text = "The selected game is not running."; return; }
        if (string.IsNullOrWhiteSpace(_name.Text) || string.IsNullOrWhiteSpace(_category.Text)) { _status.Text = "Enter a name and category first."; return; }
        try
        {
            _pending = Guid.NewGuid().ToString("N"); _pendingProcess = process; _deadline = Environment.TickCount64 + 10000; _attaching = attach;
            BookmarkObservation.Request(_directory, process, _pending); _capture.Enabled = _attach.Enabled = false;
            foreach (var control in new Control[] { _points, _search, _name, _category, _note }) control.Enabled = false;
            _status.Text = "Capturing current physical position and rendered aim…";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _pending = null; _capture.Enabled = true; _attach.Enabled = _selected is not null; _status.Text = ex.Message; }
    }
    private async Task Poll()
    {
        if (_busy || _pending is null || IsDisposed) return; _busy = true;
        try
        {
            if (_gameProcess() != _pendingProcess) { Finish("Game process changed; point capture canceled."); return; }
            if (Environment.TickCount64 >= _deadline) { Finish("Capture timed out. Load the new viewer DLL with a full game restart and keep DEViL open."); return; }
            var observation = await Task.Run(() => BookmarkObservation.Read(_directory, _pendingProcess)); if (IsDisposed) return;
            if (_gameProcess() != _pendingProcess) { Finish("Game process changed; point capture canceled."); return; }
            if (observation?.Token != _pending) return;
            if (!observation.Ready) { Finish("Point capture not ready: " + observation.Phase + ". Stand still in a loaded level and retry."); return; }
            ResearchBookmark point;
            if (_attaching)
            {
                if (_selected is not { } selected || selected.Observation.Map != observation.Map || selected.Observation.Profile != observation.Profile || selected.Observation.Identity != observation.Identity || selected.Observation.Process != observation.Process)
                { Finish("Aimed volumes must belong to the original point's map instance. Capture a new point after a reload."); return; }
                if (observation.Entities.Count == 0) { Finish("No recent aimed volume. Enable the inspector, aim at a volume and attach within 10 seconds."); return; }
                var entities = selected.Observation.Entities.Concat(observation.Entities).DistinctBy(e => e.Key).ToArray();
                if (entities.Length > 32) { Finish("A point can retain up to 32 selected volume identities."); return; }
                point = selected with { Name = _name.Text.Trim(), Category = _category.Text.Trim(), Note = _note.Text, Observation = selected.Observation with { Entities = entities } };
            }
            else point = new("DEVIL_RESEARCH_POINT_V1", Guid.NewGuid().ToString("N"), _name.Text.Trim(), _category.Text.Trim(), _note.Text, observation);
            _store.Save(point); Reload(point.Id); Finish(_attaching ? "Aimed volume attached; saved position retained." : "Research point captured and saved.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { if (!IsDisposed) _status.Text = "Capture/read failed: " + ex.Message; }
        finally { _busy = false; }
    }
    private void Finish(string text) { _pending = null; _capture.Enabled = true; _attach.Enabled = _selected is not null; foreach (var control in new Control[] { _points, _search, _name, _category, _note }) control.Enabled = true; _status.Text = text; }
    private void OpenVault()
    {
        if (_vault is null || _vault.IsDisposed) { _vault = new PracticeSaveForm(_directory, save => { if (_selected is null || _pending is not null) { _status.Text = "Select a point and wait for capture before linking a save archive."; return; } _store.Save(_selected with { SaveId = save.Id }); Reload(_selected.Id); _status.Text = "Archive linked. This is a reference; no save was loaded."; }, context: _selected, gameProcess: _gameProcess); _vault.Show(this); }
        else _vault.Activate();
    }
    private void ImportPoint()
    {
        if (_pending is not null) return; using var dialog = new OpenFileDialog { Filter = "Research point (*.json)|*.json" }; if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try { var point = _store.Import(dialog.FileName); Reload(point.Id); _status.Text = "Point imported. Linked saves must be imported separately; entity IDs remain scoped to the original map instance."; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = ex.Message; }
    }
    private void ExportPoint()
    {
        if (_selected is null) return; using var dialog = new SaveFileDialog { Filter = "Research point (*.json)|*.json", FileName = "devil-research-point.json" }; if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try { BookmarkStore.Export(_selected, dialog.FileName); _status.Text = "Point exported; save files are a separate archive."; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = ex.Message; }
    }
    protected override void Dispose(bool disposing) { if (disposing) _timer.Dispose(); base.Dispose(disposing); }
}
