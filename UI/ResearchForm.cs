using Devil.Configuration;
using Devil.Game;
using System.Globalization;

namespace Devil;

internal sealed class ResearchForm : Form
{
    private readonly DevilSettings _settings;
    private readonly string _directory;
    private readonly Action _save;
    private readonly Func<int> _gameProcess;
    private readonly Button _record = Button("Start recording", 145);
    private readonly CheckBox _showPath = new() { Text = "Show path in game", AutoSize = true };
    private readonly TextBox _marker = new() { Width = 190, PlaceholderText = "Marker / checkpoint note", MaxLength = 160 };
    private readonly TextBox _search = new() { Width = 280, PlaceholderText = "Search event, class, map or note…" };
    private readonly CheckBox _samples = new() { Text = "Include position samples", AutoSize = true };
    private readonly Label _status = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
    private readonly DataGridView _grid = new();
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 1000 };
    private ResearchLog? _log;
    private List<ResearchEvent> _visible = [];
    private bool _loading, _busy;

    public ResearchForm(DevilSettings settings, string directory, Action save, Func<int> gameProcess)
    {
        _settings = settings; _directory = directory; _save = save; _gameProcess = gameProcess;
        Text = "DEViL — Research timeline & player path";
        StartPosition = FormStartPosition.CenterParent; ClientSize = new Size(1120, 650); MinimumSize = new Size(920, 500);
        BackColor = Color.FromArgb(35, 35, 35); ForeColor = Color.Gainsboro; Font = new Font("Segoe UI", 10f);
        AutoScaleMode = AutoScaleMode.Font; ShowInTaskbar = false;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 1, RowCount = 6 };
        foreach (float height in new[] { 38f, 38f, 34f }) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); Controls.Add(layout);
        var controls = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        _record.Click += (_, _) => ToggleRecording(); controls.Controls.Add(_record);
        var reset = Button("New session", 125);
        reset.Click += (_, _) => { _settings.ResearchReset = DateTime.UtcNow.Ticks; _save(); }; controls.Controls.Add(reset);
        _showPath.Checked = settings.ShowResearchPath;
        _showPath.CheckedChanged += (_, _) => { if (!_loading) { _settings.ShowResearchPath = _showPath.Checked; _save(); } };
        controls.Controls.Add(_showPath);
        var export = Button("Export CSV…", 130); export.Click += (_, _) => Export(); controls.Controls.Add(export);
        layout.Controls.Add(controls, 0, 0);
        var markers = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        markers.Controls.Add(_marker); var addMarker = Button("Add marker", 120); markers.Controls.Add(addMarker);
        addMarker.Click += (_, _) =>
        {
            if (!_settings.RecordResearch || _settings.ResearchProcess != _gameProcess()) return;
            _settings.ResearchMarkerLabel = SettingsStore.CleanMarker(_marker.Text);
            _settings.ResearchMarker = DateTime.UtcNow.Ticks; _save(); _marker.Clear();
        };
        markers.Controls.Add(new Label { Text = "Markers are notes; volume state changes are observations, not proof of activation.", AutoSize = true, Margin = new Padding(8, 5, 0, 0), Font = new Font(Font.FontFamily, 9f) });
        layout.Controls.Add(markers, 0, 1);
        var filters = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        filters.Controls.Add(_search); filters.Controls.Add(_samples); layout.Controls.Add(filters, 0, 2);
        _search.TextChanged += (_, _) => Filter(); _samples.CheckedChanged += (_, _) => Filter();
        _grid.Dock = DockStyle.Fill; _grid.ReadOnly = true; _grid.AllowUserToAddRows = _grid.AllowUserToDeleteRows = false;
        _grid.RowHeadersVisible = false; _grid.VirtualMode = true; _grid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        _grid.BackgroundColor = Color.FromArgb(45, 45, 45); _grid.DefaultCellStyle.BackColor = Color.FromArgb(45, 45, 45);
        _grid.DefaultCellStyle.ForeColor = ForeColor; _grid.DefaultCellStyle.SelectionBackColor = Color.FromArgb(90, 50, 80);
        foreach (var column in new[] { ("Time", 115), ("Event", 165), ("Class", 240), ("ID / gen", 85), ("X", 80), ("Y", 80), ("Z", 80), ("Detail", 340), ("Map", 240) })
            _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = column.Item1, Width = column.Item2, SortMode = DataGridViewColumnSortMode.NotSortable });
        _grid.CellValueNeeded += (_, e) =>
        {
            if (e.RowIndex >= _visible.Count) return;
            var row = _visible[e.RowIndex];
            e.Value = e.ColumnIndex switch { 0 => row.Time, 1 => row.Kind, 2 => row.Class, 3 => row.Identity,
                4 => row.X?.ToString("F3", CultureInfo.InvariantCulture), 5 => row.Y?.ToString("F3", CultureInfo.InvariantCulture),
                6 => row.Z?.ToString("F3", CultureInfo.InvariantCulture), 7 => row.Detail, 8 => row.Map, _ => "" };
        };
        layout.Controls.Add(_grid, 0, 3); layout.Controls.Add(_status, 0, 4);
        layout.Controls.Add(new Label { Text = "Path: blue → pink. Stop keeps the line; New session clears it. Menus and large jumps break the line. Viewer and distance settings apply.", AutoSize = true, Font = new Font(Font.FontFamily, 9f), Anchor = AnchorStyles.Left }, 0, 5);
        _timer.Tick += async (_, _) => await RefreshLog(); _timer.Start(); Shown += async (_, _) => await RefreshLog();
        UpdateControls();
    }

    private static Button Button(string text, int width) => new() { Text = text, Size = new Size(width, 29), FlatStyle = FlatStyle.Flat,
        BackColor = Color.FromArgb(65, 65, 65), ForeColor = Color.Gainsboro, Margin = new Padding(0, 0, 10, 0) };
    private void ToggleRecording()
    {
        int game = _gameProcess();
        if (game == 0) { MessageBox.Show(this, "Launch the selected game through DEViL first.", "Research recording"); return; }
        _settings.RecordResearch = !(_settings.RecordResearch && _settings.ResearchProcess == game);
        _settings.ResearchProcess = game; _save(); UpdateControls();
    }
    private void UpdateControls()
    {
        _record.Text = _settings.RecordResearch && _settings.ResearchProcess == _gameProcess() ? "Stop recording" : "Start recording";
        _loading = true; _showPath.Checked = _settings.ShowResearchPath; _loading = false;
    }
    private async Task RefreshLog()
    {
        if (_busy || IsDisposed) return; _busy = true;
        try
        {
            UpdateControls(); var next = await Task.Run(() => ResearchLog.ReadLatest(_directory));
            if (IsDisposed) return;
            if (next is null) { _status.Text = "No session yet. Start recording after loading the updated viewer DLL."; return; }
            _log = next; Filter();
            string state = _log.Process != _gameProcess() ? "Previous game process" : _log.Recording ? "Recording / waiting for gameplay" : "Stopped";
            _status.Text = $"Session {_log.Session} · {state} · {_log.Rows.Count} recent rows · {_log.Omitted} older rows in full journal";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { if (!IsDisposed) _status.Text = "Waiting for a readable session snapshot…"; }
        finally { _busy = false; }
    }
    private void Filter()
    {
        string query = _search.Text.Trim();
        _visible = (_log?.Rows ?? []).Where(row => (_samples.Checked || row.Kind != "position")
            && string.Join(' ', row.Kind, row.Class, row.Map, row.Detail).Contains(query, StringComparison.OrdinalIgnoreCase)).ToList();
        int first = _grid.FirstDisplayedScrollingRowIndex;
        _grid.RowCount = _visible.Count; _grid.Invalidate();
        if (first >= 0 && first < _grid.RowCount) _grid.FirstDisplayedScrollingRowIndex = first;
    }
    private void Export()
    {
        if (_log is null) return;
        using var dialog = new SaveFileDialog { Filter = "CSV (*.csv)|*.csv", FileName = $"DEViL-{_log.Session}.csv", AddExtension = true };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try { _log.ExportFullCsv(_directory, dialog.FileName); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { MessageBox.Show(this, ex.Message, "Export failed"); }
    }
    protected override void Dispose(bool disposing) { if (disposing) _timer.Dispose(); base.Dispose(disposing); }
}
