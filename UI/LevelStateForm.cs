using Devil.Game;
using System.Globalization;
using System.Text;

namespace Devil;

internal sealed class LevelStateForm : Form
{
    private readonly string _directory;
    private readonly Func<int> _gameProcess;
    private readonly Button _captureBefore = Button("Capture BEFORE", 145), _captureAfter = Button("Capture AFTER", 145);
    private readonly TextBox _note = new() { Width = 220, PlaceholderText = "Experiment note", MaxLength = 1024 };
    private readonly TextBox _search = new() { Width = 360, PlaceholderText = "Search object, class, change or ID…" };
    private readonly Label _captures = new() { Dock = DockStyle.Fill, UseMnemonic = false };
    private readonly Label _summary = new() { Dock = DockStyle.Fill, UseMnemonic = false };
    private readonly Label _status = new() { Dock = DockStyle.Fill, UseMnemonic = false, AutoEllipsis = true };
    private readonly CheckBox _all = new() { Text = "Highlight all volume changes (max 256)", AutoSize = true };
    private readonly CheckBox _old = new() { Text = "Before outlines", Checked = true, AutoSize = true };
    private readonly CheckBox _new = new() { Text = "After outlines", Checked = true, AutoSize = true };
    private readonly CheckBox _far = new() { Text = "Ignore view distance for highlights", Checked = true, AutoSize = true };
    private readonly DataGridView _grid = new();
    private readonly TextBox _detail = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 250 };
    private LevelStateSnapshot? _before, _after;
    private IReadOnlyList<LevelStateChange> _changes = [];
    private List<LevelStateChange> _visible = [];
    private string? _pendingToken, _pendingRole;
    private int _pendingProcess;
    private long _deadline, _lastHighlight;
    private bool _busy, _populating, _compatible;

    public LevelStateForm(string directory, Func<int> gameProcess)
    {
        _directory = directory; _gameProcess = gameProcess;
        Text = "DEViL — Level state: before / after"; ClientSize = new Size(1240, 790); MinimumSize = new Size(1100, 700);
        StartPosition = FormStartPosition.CenterParent; ShowInTaskbar = false; AutoScaleMode = AutoScaleMode.Font;
        BackColor = Color.FromArgb(35, 35, 35); ForeColor = Color.Gainsboro; Font = new Font("Segoe UI", 10f);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 1, RowCount = 9 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        foreach (float height in new[] { 38f, 60f, 56f, 36f, 34f }) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 86));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 30)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 64)); Controls.Add(layout);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        actions.Controls.AddRange([_captureBefore, _captureAfter]); _captureBefore.Click += (_, _) => BeginCapture("before"); _captureAfter.Click += (_, _) => BeginCapture("after");
        var save = Button("Save pair…", 120); save.Click += (_, _) => SavePair();
        var open = Button("Open pair…", 120); open.Click += async (_, _) => await OpenPair();
        var export = Button("Export changes…", 140); export.Click += (_, _) => ExportChanges();
        actions.Controls.AddRange([save, open, export, _note]); layout.Controls.Add(actions, 0, 0); layout.Controls.Add(_captures, 0, 1); layout.Controls.Add(_summary, 0, 2);
        var filters = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        filters.Controls.Add(_search); filters.Controls.Add(new Label { Text = "Click a change to highlight its captured volume.", AutoSize = true, Margin = new Padding(10, 6, 0, 0) });
        layout.Controls.Add(filters, 0, 3); _search.TextChanged += (_, _) => Filter();
        var highlighting = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        highlighting.Controls.AddRange([_all, _old, _new, _far]); layout.Controls.Add(highlighting, 0, 4);
        foreach (var check in new[] { _all, _old, _new, _far }) check.CheckedChanged += (_, _) => PublishHighlight(true);
        _grid.Dock = DockStyle.Fill; _grid.VirtualMode = true; _grid.ReadOnly = true; _grid.AllowUserToAddRows = _grid.AllowUserToDeleteRows = false;
        _grid.RowHeadersVisible = false; _grid.MultiSelect = false; _grid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        _grid.BackgroundColor = _grid.DefaultCellStyle.BackColor = Color.FromArgb(45, 45, 45); _grid.DefaultCellStyle.ForeColor = ForeColor;
        _grid.DefaultCellStyle.SelectionBackColor = Color.FromArgb(85, 55, 85);
        foreach (var (title, width) in new[] { ("Category", 95), ("Change", 170), ("Object", 270), ("Class", 220), ("ID / generation", 110), ("Before", 230), ("After", 230), ("Detail", 600) })
            _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = title, Width = width, SortMode = DataGridViewColumnSortMode.NotSortable });
        _grid.Columns[0].Frozen = true;
        _grid.CellValueNeeded += (_, e) =>
        {
            if (e.RowIndex >= _visible.Count) return; var row = _visible[e.RowIndex];
            e.Value = e.ColumnIndex switch { 0 => row.Category, 1 => row.Kind, 2 => row.Name, 3 => row.Class, 4 => row.Key, 5 => row.Before, 6 => row.After, 7 => row.Detail, _ => "" };
        };
        _grid.CellToolTipTextNeeded += (_, e) => { if (e.RowIndex >= 0 && e.RowIndex < _visible.Count) { var row = _visible[e.RowIndex]; e.ToolTipText = $"{row.Name}\n{row.Class} · {row.Key}\nBefore: {row.Before}\nAfter: {row.After}\n{row.Detail}"; } };
        _grid.SelectionChanged += (_, _) => { if (!_populating) { ShowSelection(); PublishHighlight(true); } };
        layout.Controls.Add(_grid, 0, 5); _detail.BackColor = Color.FromArgb(45, 45, 45); _detail.ForeColor = ForeColor;
        layout.Controls.Add(_detail, 0, 6); layout.Controls.Add(_status, 0, 7);
        layout.Controls.Add(new Label { Dock = DockStyle.Fill, Font = new Font(Font.FontFamily, 9f), Text = "Before: dashed cyan; missing identities: red. After: green / disabled pink. These are captured outlines; bounding boxes are used if geometry is unavailable or over budget.\nHighlights override class/path visibility; Collision Viewer must be enabled. No camera movement or game state is restored. Managers/checkpoint names may have no drawable volume.\nLoaded checkpoint is spawn-info data, not proof of a new save. Entity-table completeness does not make this a frame-atomic snapshot; unavailable data stays explicit." }, 0, 8);
        _timer.Tick += async (_, _) => await Poll(); _timer.Start(); Shown += async (_, _) => await Poll();
        FormClosing += (_, _) => { _timer.Stop(); ClearHighlight(); };
        RefreshComparison();
    }
    private static Button Button(string text, int width) => new() { Text = text, Size = new Size(width, 29), FlatStyle = FlatStyle.Flat,
        BackColor = Color.FromArgb(65, 65, 65), ForeColor = Color.Gainsboro, Margin = new Padding(0, 0, 8, 0) };
    private void BeginCapture(string role)
    {
        int process = _gameProcess();
        if (process <= 0) { _status.Text = "The selected game is not running."; return; }
        if (role == "after" && _before?.Ready != true) { _status.Text = "Capture Before first."; return; }
        if (_pendingToken is not null) return;
        try
        {
            string token = Guid.NewGuid().ToString("N"); LevelStateSnapshot.Request(_directory, process, token, role);
            _pendingToken = token; _pendingRole = role; _pendingProcess = process; _deadline = Environment.TickCount64 + 10000;
            _status.Text = $"Capturing {role}…"; UpdateButtons();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = "Capture request failed: " + ex.Message; }
    }
    private async Task Poll()
    {
        if (_busy || IsDisposed) return; _busy = true;
        try
        {
            if (_pendingToken is not null && _pendingRole is { } role)
            {
                if (_gameProcess() != _pendingProcess) { _pendingToken = _pendingRole = null; _status.Text = "Game process changed; capture canceled."; UpdateButtons(); return; }
                if (Environment.TickCount64 >= _deadline) { _pendingToken = _pendingRole = null; _status.Text = "Capture timed out. Load this viewer build through a full game restart and keep DEViL open."; UpdateButtons(); return; }
                var state = await Task.Run(() => LevelStateSnapshot.Read(_directory, _pendingProcess, role));
                if (IsDisposed) return;
                if (_gameProcess() != _pendingProcess) { _pendingToken = _pendingRole = null; _status.Text = "Game process changed; capture canceled."; UpdateButtons(); return; }
                if (state?.Token == _pendingToken)
                {
                    _pendingToken = _pendingRole = null;
                    if (!state.Ready) _status.Text = $"Capture not ready: {state.Phase}. Wait until the level is loaded, then retry.";
                    else
                    {
                        if (role == "before") { _before = state; _after = null; ClearHighlight(); } else _after = state;
                        RefreshComparison(); _status.Text = $"Captured {role}: {state.Volumes.Count} observed volumes, {state.Managers.Count} managers; {state.Unavailable} unavailable reads.";
                    }
                    UpdateButtons();
                }
                else if (Environment.TickCount64 >= _deadline) { _pendingToken = _pendingRole = null; _status.Text = "Capture timed out. Load this viewer build through a full game restart and keep DEViL open."; UpdateButtons(); }
            }
            PublishHighlight(false); ReadHighlightStatus();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { if (!IsDisposed) _status.Text = "Waiting for readable capture data: " + ex.Message; }
        finally { _busy = false; }
    }
    private void UpdateButtons() { _captureBefore.Enabled = _pendingToken is null; _captureAfter.Enabled = _pendingToken is null && _before?.Ready == true; }
    private void RefreshComparison()
    {
        string Description(string role, LevelStateSnapshot? value) => value is null ? role + ": not captured" :
            $"{role}: {DateTimeOffset.FromUnixTimeMilliseconds(value.Captured).ToLocalTime():HH:mm:ss.fff} · {value.Volumes.Count} volumes / {value.Managers.Count} managers · checkpoint {(value.CheckpointStatus == "known" ? value.Checkpoint.Length == 0 ? "<none loaded>" : value.Checkpoint : value.CheckpointStatus)} · unavailable {value.Unavailable}";
        _captures.Text = Description("Before", _before) + "\n" + Description("After", _after);
        _changes = []; _compatible = false;
        if (_before is not null && _after is not null)
        {
            try
            {
                _changes = LevelStateComparison.Compare(_before, _after);
                _compatible = true;
                int Count(string kind) => _changes.Count(row => row.IsVolume && row.Kind == kind);
                int volumes = _changes.Where(row => row.IsVolume).Select(row => row.Key).Distinct().Count();
                _summary.Text = $"Volume rows cover {volumes} identities · added {Count("Added")} · removed {Count("Removed")} · enabled {Count("Enabled")} · disabled {Count("Disabled")} · {_changes.Count} state/availability rows\nKnown updating managers: {_before.Managers.Count(m => m.Updating == true)} → {_after.Managers.Count(m => m.Updating == true)} · sources {_before.ManagersPhase} → {_after.ManagersPhase} · {_before.Map} · {_before.Profile}";
            }
            catch (InvalidOperationException ex) { _summary.Text = ex.Message; ClearHighlight(); }
        }
        else _summary.Text = "Capture Before, perform an action in the game, then capture After. Hidden trigger classes and disabled volumes are still sampled.";
        Filter(); UpdateButtons();
    }
    private void Filter()
    {
        string query = _search.Text.Trim(); var selected = Selected;
        _visible = _changes.Where(row => $"{row.Category} {row.Kind} {row.Name} {row.Class} {row.Key} {row.Before} {row.After} {row.Detail}".Contains(query, StringComparison.OrdinalIgnoreCase)).ToList();
        _populating = true; _grid.RowCount = _visible.Count; _grid.Invalidate();
        int index = selected is null ? 0 : _visible.FindIndex(row => row.Category == selected.Category && row.Key == selected.Key && row.Kind == selected.Kind);
        if (_visible.Count > 0) _grid.CurrentCell = _grid.Rows[Math.Max(0, index)].Cells[0];
        _populating = false; ShowSelection(); PublishHighlight(true);
    }
    private LevelStateChange? Selected => _grid.CurrentCell is { RowIndex: >= 0 } cell && cell.RowIndex < _visible.Count ? _visible[cell.RowIndex] : null;
    private void ShowSelection()
    {
        if (Selected is not { } row) { _detail.Text = "No observed changes match the current search."; return; }
        _detail.Text = $"{row.Category} · {row.Kind} · {row.Name}\r\n{row.Class} · ID/generation {row.Key}\r\n{row.Detail}";
        if (row.IsVolume) _detail.AppendText($"\r\nBefore: {row.BeforeVolume?.Geometry ?? "absent"} at {(row.BeforeVolume is { } a ? LevelStateComparison.Position(a) : "—")} · After: {row.AfterVolume?.Geometry ?? "absent"} at {(row.AfterVolume is { } b ? LevelStateComparison.Position(b) : "—")}");
        else _detail.AppendText("\r\nThis logical state change has no volume outline. Its sampled fields are shown in the table.");
    }
    private void PublishHighlight(bool force)
    {
        if (!force && Environment.TickCount64 - _lastHighlight < 1000) return; _lastHighlight = Environment.TickCount64;
        if (!_compatible || _before is null || _after is null || _gameProcess() != _before.Process) return;
        var keys = (_all.Checked ? _changes.Where(row => row.IsVolume) : Selected is { IsVolume: true } selected ? [selected] : Enumerable.Empty<LevelStateChange>())
            .Select(row => row.Key).Distinct().Take(256).ToArray();
        try
        {
            string path = Path.Combine(_directory, $"state-highlight-{_before.Process}.request"), temporary = path + ".tmp";
            using (var writer = new StreamWriter(temporary, false, new UTF8Encoding(false)))
            {
                writer.WriteLine($"#DEVIL_STATE_HIGHLIGHT_V1\t{_before.Process}\t{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + 3000}\t{_before.Token}\t{_after.Token}\t{(_old.Checked ? 1 : 0)}\t{(_new.Checked ? 1 : 0)}\t{(_far.Checked ? 1 : 0)}");
                foreach (string key in keys) { var values = key.Split(':'); writer.WriteLine($"V\t{values[0]}\t{values[1]}"); }
            }
            File.Move(temporary, path, true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = "Highlight request failed: " + ex.Message; }
    }
    private void ClearHighlight()
    {
        int process = _before?.Process ?? _gameProcess(); if (process <= 0) return;
        try { string path = Path.Combine(_directory, $"state-highlight-{process}.request"); if (File.Exists(path)) File.Delete(path); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
    }
    private void ReadHighlightStatus()
    {
        if (_pendingToken is not null || _before is null || _after is null) return;
        try
        {
            string path = Path.Combine(_directory, $"state-highlight-{_before.Process}.status"); if (!File.Exists(path) || new FileInfo(path).Length > 1024) return;
            string[] cells = File.ReadAllText(path).TrimEnd('\r', '\n').Split('\t');
            if (cells.Length != 7 || cells[0] != "#DEVIL_STATE_HIGHLIGHT_STATUS_V1" || cells[1] != _before.Process.ToString(CultureInfo.InvariantCulture)
                || !long.TryParse(cells[2], out long time) || Math.Abs(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - time) > 3000) return;
            _status.Text = cells[3] switch
            {
                "ready" => $"Captured outlines: {cells[4]} exact / {cells[5]} bounding-only / {cells[6]} without geometry. Viewer must be enabled.",
                "snapshot_not_cached" => "Snapshot outlines are no longer cached in this game. Saved fields remain available; capture a new pair for highlighting.",
                "map_changed" => "Current map/world differs from the captured pair. Highlights are hidden.",
                "off" => "No active volume highlight.", _ => "Highlight: " + cells[3]
            };
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
    }
    private void SavePair()
    {
        if (_pendingToken is not null) { _status.Text = "Wait until the capture completes."; return; }
        if (_before is null || _after is null) { _status.Text = "Capture both states first."; return; }
        using var dialog = new SaveFileDialog { Filter = "Level state pair (*.json)|*.json", FileName = "devil-level-state-pair.json" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try { new LevelStatePair("DEVIL_LEVEL_STATE_PAIR_V1", _note.Text, _before, _after).Save(dialog.FileName); _status.Text = "Snapshot pair saved."; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = "Save failed: " + ex.Message; }
    }
    private async Task OpenPair()
    {
        if (_pendingToken is not null) { _status.Text = "Wait until the capture completes."; return; }
        using var dialog = new OpenFileDialog { Filter = "Level state pair (*.json)|*.json" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            var pair = await Task.Run(() => LevelStatePair.Load(dialog.FileName)); if (IsDisposed) return;
            ClearHighlight(); _before = pair.Before; _after = pair.After; _note.Text = pair.Note; RefreshComparison(); _status.Text = "Pair loaded; outlines require the original native capture cache and map instance.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException) { _status.Text = "Open failed: " + ex.Message; }
    }
    private void ExportChanges()
    {
        if (_before is null || _after is null) return;
        using var dialog = new SaveFileDialog { Filter = "Changes CSV (*.csv)|*.csv", FileName = "devil-state-changes.csv" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            static string Cell(string value) => '"' + value.Replace("\"", "\"\"") + '"';
            using var writer = new StreamWriter(dialog.FileName, false, new UTF8Encoding(true));
            writer.WriteLine("category,change,object,class,id_generation,before,after,detail");
            foreach (var row in _changes) writer.WriteLine(string.Join(',', new[] { row.Category, row.Kind, row.Name, row.Class, row.Key, row.Before, row.After, row.Detail }.Select(Cell)));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = "Export failed: " + ex.Message; }
    }
    protected override void Dispose(bool disposing) { if (disposing) { _timer.Dispose(); ClearHighlight(); } base.Dispose(disposing); }
}
