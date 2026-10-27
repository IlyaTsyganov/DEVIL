using Devil.Configuration;
using Devil.Game;
using System.Globalization;

namespace Devil;

internal sealed class MovementLabForm : Form
{
    private readonly DevilSettings _settings;
    private readonly string _directory;
    private readonly Action _save;
    private readonly Func<int> _gameProcess;
    private readonly MotionStore _store;
    private readonly CheckedListBox _attempts = new() { Dock = DockStyle.Fill, CheckOnClick = true, IntegralHeight = false, HorizontalScrollbar = true };
    private readonly CheckBox _collider = new() { Text = "Show player collider", AutoSize = true };
    private readonly CheckBox _paths = new() { Text = "Show compared paths in game", AutoSize = true };
    private readonly CheckBox _follow = new() { Text = "Follow aimed volume", AutoSize = true };
    private readonly CheckBox _latest = new() { Text = "Latest sample", AutoSize = true };
    private readonly CheckBox _align = new() { Text = "Align starts (plot)", AutoSize = true };
    private readonly ComboBox _projection = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 66 };
    private readonly ComboBox _outcome = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 105 };
    private readonly TextBox _name = new() { Width = 180, MaxLength = 80, Text = "Attempt 1" };
    private readonly Button _record = Button("Start attempt", 130);
    private readonly Label _telemetry = new() { Dock = DockStyle.Fill, UseMnemonic = false };
    private readonly Label _status = new() { Dock = DockStyle.Fill, UseMnemonic = false, AutoEllipsis = true };
    private readonly Label _timeLabel = new() { AutoSize = true, Margin = new Padding(10, 6, 0, 0) };
    private readonly TrackBar _time = new() { Dock = DockStyle.Fill, Minimum = 0, Maximum = 1, TickStyle = TickStyle.None, SmallChange = 50, LargeChange = 500 };
    private readonly MotionPlot _plot = new() { Dock = DockStyle.Fill };
    private readonly DataGridView _comparison = new();
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 250 };
    private readonly List<MotionAttempt> _saved;
    private MotionSnapshot? _snapshot;
    private MotionAttempt? _active;
    private bool _busy, _loading;
    private int _process;
    private long _recordFrom, _lastSequence, _lastSave, _lastOverlay;
    private ulong _sourceSegment, _recordSegment;
    private string _recordIdentity = "";
    private SupportPlaneForm? _surface;

    public MovementLabForm(DevilSettings settings, string directory, Action save, Func<int> gameProcess)
    {
        _settings = settings; _directory = directory; _save = save; _gameProcess = gameProcess; _store = new(directory); _saved = _store.Load().ToList();
        Text = "DEViL — Player collider & attempt comparison"; ClientSize = new Size(1220, 790); MinimumSize = new Size(1100, 720);
        StartPosition = FormStartPosition.CenterParent; ShowInTaskbar = false; AutoScaleMode = AutoScaleMode.Font;
        BackColor = Color.FromArgb(35, 35, 35); ForeColor = Color.Gainsboro; Font = new Font("Segoe UI", 10f);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 1, RowCount = 8 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        foreach (float height in new[] { 35f, 38f, 110f }) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 150)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 32)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 64));
        Controls.Add(layout);
        var switches = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        _collider.Checked = settings.ShowPlayerCollider; _paths.Checked = settings.ShowMotionPaths; _follow.Checked = settings.MotionFollowTarget;
        switches.Controls.AddRange([_collider, _paths, _follow]); layout.Controls.Add(switches, 0, 0);
        _collider.CheckedChanged += (_, _) => { if (!_loading) { _settings.ShowPlayerCollider = _collider.Checked; SaveSwitches(); } };
        _paths.CheckedChanged += (_, _) => { if (!_loading) { _settings.ShowMotionPaths = _paths.Checked; SaveSwitches(); } };
        _follow.CheckedChanged += (_, _) => { if (!_loading) { _settings.MotionFollowTarget = _follow.Checked; SaveSwitches(); } };
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        actions.Controls.Add(_name); actions.Controls.Add(_record); _record.Click += (_, _) => { if (_active is null) StartAttempt(); else StopAttempt("Attempt saved."); };
        actions.Controls.Add(new Label { Text = "Result:", AutoSize = true, Margin = new Padding(4, 6, 0, 0) });
        _outcome.Items.AddRange(Enum.GetNames<AttemptOutcome>()); _outcome.SelectedIndex = 0;
        actions.Controls.Add(_outcome); var export = Button("Export CSV…", 125); export.Click += (_, _) => Export(); actions.Controls.Add(export);
        var surface = Button("Surface projections…", 190);
        surface.Click += (_, _) =>
        {
            if (_surface is null || _surface.IsDisposed) { _surface = new SupportPlaneForm(_settings, SaveSwitches); _surface.Show(this); }
            else _surface.Activate();
            UpdateComparison();
        };
        actions.Controls.Add(surface);
        _outcome.SelectedIndexChanged += (_, _) =>
        {
            if (_loading || _attempts.SelectedItem is not MotionAttempt attempt) return;
            attempt.Outcome = (AttemptOutcome)_outcome.SelectedIndex;
            if (attempt.Samples.Count > 0) TrySave(attempt);
            PopulateAttempts(); UpdateComparison(); PublishOverlay(true);
        };
        layout.Controls.Add(actions, 0, 1); layout.Controls.Add(_telemetry, 0, 2);
        var split = new SplitContainer { Dock = DockStyle.Fill, Size = new Size(1180, 300), SplitterDistance = 265, Panel1MinSize = 210, Panel2MinSize = 500 };
        _attempts.BackColor = Color.FromArgb(45, 45, 45); _attempts.ForeColor = ForeColor;
        split.Panel1.Controls.Add(_attempts); split.Panel2.Controls.Add(_plot); layout.Controls.Add(split, 0, 3);
        _attempts.SelectedIndexChanged += (_, _) =>
        {
            if (_loading) return; _loading = true;
            if (_attempts.SelectedItem is MotionAttempt attempt) _outcome.SelectedIndex = (int)attempt.Outcome;
            _loading = false; UpdateComparison();
        };
        _attempts.ItemCheck += (_, e) =>
        {
            if (_loading) return;
            if (e.NewValue == CheckState.Checked && _attempts.CheckedItems.Count >= 6) { e.NewValue = CheckState.Unchecked; _status.Text = "Compare up to six attempts at once."; return; }
            BeginInvoke((Action)(() => { if (!IsDisposed) { UpdateComparison(); PublishOverlay(true); } }));
        };
        var scrub = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, Margin = Padding.Empty };
        scrub.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); scrub.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 510));
        scrub.Controls.Add(_time, 0, 0);
        var views = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        _projection.Items.AddRange(["XY", "XZ", "YZ"]); _projection.SelectedIndex = 0;
        views.Controls.AddRange([_projection, _align, _latest, _timeLabel]); scrub.Controls.Add(views, 1, 0); layout.Controls.Add(scrub, 0, 4);
        _time.ValueChanged += (_, _) => UpdateComparison(); _time.Scroll += (_, _) => _latest.Checked = false;
        _projection.SelectedIndexChanged += (_, _) => UpdateComparison(); _align.CheckedChanged += (_, _) => UpdateComparison();
        _comparison.Dock = DockStyle.Fill; _comparison.ReadOnly = true; _comparison.AllowUserToAddRows = _comparison.AllowUserToDeleteRows = false;
        _comparison.RowHeadersVisible = false; _comparison.BackgroundColor = _comparison.DefaultCellStyle.BackColor = Color.FromArgb(45, 45, 45);
        _comparison.DefaultCellStyle.ForeColor = ForeColor;
        foreach (var (label, width) in new[] { ("Attempt", 175), ("Sample t (s)", 100), ("Speed", 80), ("XY / planar", 100), ("Z", 90), ("Δ Z", 80), ("Δ position", 95), ("Δ velocity", 95), ("Support", 80), ("Slope °", 80), ("Target ID/gen", 110), ("Origin → boundary", 140), ("Skin gap", 95), ("Body W/D/H", 150), ("Shape", 70), ("Support point", 210), ("Support normal", 210) })
            _comparison.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = label, Width = width, SortMode = DataGridViewColumnSortMode.NotSortable });
        _comparison.Columns[0].Frozen = _comparison.Columns[1].Frozen = true;
        layout.Controls.Add(_comparison, 0, 5); layout.Controls.Add(_status, 0, 6);
        layout.Controls.Add(new Label { Dock = DockStyle.Fill, Font = new Font(Font.FontFamily, 9f), Text = "Check attempts to overlay. Highlight one as the reference; only matching maps/builds are compared. Time starts at the first sample; matches use the nearest sample within 75 ms.\n20 Hz target sampling; pauses, missing samples and large jumps break paths. Results are your notes. Support points/normals come from the game's ground trace, not a complete contact manifold." }, 0, 7);
        PopulateAttempts(); _timer.Tick += async (_, _) => await RefreshMotion(); Shown += async (_, _) => { _timer.Start(); await RefreshMotion(); };
        FormClosing += (_, _) => { StopAttempt("Attempt saved."); _timer.Stop(); };
    }
    private static Button Button(string text, int width) => new() { Text = text, Size = new Size(width, 29), FlatStyle = FlatStyle.Flat,
        BackColor = Color.FromArgb(65, 65, 65), ForeColor = Color.Gainsboro, Margin = new Padding(6, 0, 6, 0) };
    private void SaveSwitches() { _settings.MotionProcess = _gameProcess(); _save(); }
    private List<MotionAttempt> CheckedAttempts() => _attempts.CheckedItems.Cast<MotionAttempt>().ToList();
    private void PopulateAttempts()
    {
        var checkedIds = CheckedAttempts().Select(attempt => attempt.Id).ToHashSet(); string? selected = (_attempts.SelectedItem as MotionAttempt)?.Id;
        _loading = true; _attempts.BeginUpdate(); _attempts.Items.Clear();
        foreach (var attempt in _saved) _attempts.Items.Add(attempt, checkedIds.Contains(attempt.Id) || attempt == _active);
        if (selected is not null) _attempts.SelectedItem = _saved.FirstOrDefault(attempt => attempt.Id == selected);
        _attempts.EndUpdate(); _loading = false;
    }
    private void StartAttempt()
    {
        int process = _gameProcess(); long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (_snapshot is null || process <= 0 || _snapshot.Process != process || !_snapshot.Fresh(now) || _snapshot.Map.Length == 0 || _snapshot.Phase is "metadata_unavailable" or "read_error")
        { _status.Text = "Wait for current player data. Load the new viewer layer and a level first."; return; }
        _active = new MotionAttempt { Name = string.IsNullOrWhiteSpace(_name.Text) ? $"Attempt {_saved.Count + 1}" : _name.Text.Trim(), Process = process, Map = _snapshot.Map, Profile = _snapshot.Profile };
        _recordFrom = now; _lastSequence = _snapshot.Samples.LastOrDefault()?.Sequence ?? 0; _recordSegment = _sourceSegment = 0; _recordIdentity = _snapshot.Identity;
        _saved.Insert(0, _active); PopulateAttempts(); _attempts.SelectedItem = _active;
        _loading = true; _outcome.SelectedIndex = 0; _loading = false; _latest.Checked = true; _record.Text = "Stop attempt";
        _status.Text = "Recording; waiting for the next gameplay sample…";
    }
    private void StopAttempt(string reason)
    {
        if (_active is not { } attempt) return;
        _active = null; attempt.Finished = true;
        bool saved = attempt.Samples.Count == 0 || TrySave(attempt);
        if (attempt.Samples.Count == 0) _saved.Remove(attempt);
        if (saved) while (_saved.Count > 64 || _saved.Sum(item => item.Samples.Count) > 200_000) _saved.RemoveAt(_saved.Count - 1);
        _record.Text = "Start attempt"; _latest.Checked = false; _time.Value = 0; _name.Text = $"Attempt {_saved.Count + 1}";
        PopulateAttempts(); UpdateComparison(); PublishOverlay(true); _status.Text = saved ? reason : "Save failed; attempt remains in this window and can be exported.";
    }
    private bool TrySave(MotionAttempt attempt)
    {
        try { _store.Save(attempt); return true; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = "Could not save attempt: " + ex.Message; return false; }
    }
    private async Task RefreshMotion()
    {
        if (_busy || IsDisposed) return; _busy = true;
        try
        {
            _loading = true; _collider.Checked = _settings.ShowPlayerCollider; _paths.Checked = _settings.ShowMotionPaths; _follow.Checked = _settings.MotionFollowTarget; _loading = false;
            int process = _gameProcess();
            if (process != _process) { StopAttempt("Game process changed; attempt ended."); _process = process; _snapshot = null; _settings.MotionProcess = process; _save(); }
            if (process <= 0) { _telemetry.Text = "The selected game is not running. Saved attempts remain available below."; UpdateComparison(); return; }
            MotionSnapshot.Request(_directory, process); var next = await Task.Run(() => MotionSnapshot.Read(_directory, process));
            if (IsDisposed || _gameProcess() != process) return;
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            if (next is null || !next.Fresh(now)) { _snapshot = next; _telemetry.Text = "Waiting for movement data. Enable Collision Viewer and restart the game once through this build."; UpdateComparison(); return; }
            _snapshot = next;
            if (_active is { } active)
            {
                if (next.Map != active.Map || next.Profile != active.Profile || next.Identity != _recordIdentity) StopAttempt("Level changed or reloaded; attempt ended.");
                else foreach (var sample in next.Samples.Where(sample => sample.Sequence > _lastSequence && sample.Epoch >= _recordFrom))
                {
                    if (active.Samples.Count >= 24000 || (active.Started > 0 && sample.Epoch - active.Started > 7_200_000)) { StopAttempt("Attempt limit reached; recording saved."); break; }
                    var previous = active.Samples.LastOrDefault();
                    if (previous is null) active.Started = sample.Epoch;
                    if (previous is null || sample.Segment != _sourceSegment || sample.Sequence != previous.Sequence + 1 || sample.Epoch - previous.Epoch > 250 || MotionVector.Distance(sample.Position, previous.Position) > 50) ++_recordSegment;
                    _sourceSegment = sample.Segment; active.Samples.Add(sample with { Segment = _recordSegment }); _lastSequence = sample.Sequence;
                }
                if (_active is not null && active.Samples.Count > 0 && now - _lastSave >= 4000) { _lastSave = now; TrySave(active); }
            }
            var current = next.Samples.LastOrDefault();
            string N(float? value) => value?.ToString("0.000", CultureInfo.InvariantCulture) ?? "—";
            _telemetry.Text = current is null ? $"{next.Phase} · {next.Profile}\nNo valid player sample yet. Compatible meathook metadata is required."
                : $"{next.Phase} · {next.Profile} · {next.Map}\nPosition: {N(current.Position.X)}, {N(current.Position.Y)}, {N(current.Position.Z)} · Velocity: {N(current.Velocity.X)}, {N(current.Velocity.Y)}, {N(current.Velocity.Z)} · Speed: {N(current.Speed)} units/s\nBody: {N(current.Maximum.X - current.Minimum.X)} × {N(current.Maximum.Y - current.Minimum.Y)} × {N(current.Maximum.Z - current.Minimum.Z)} · Shape {current.Shape} · {(current.Collider ? "rounded convex hull" : "bounds only — shape unsupported")}\nSupport: {(current.Contact ? $"point {N(current.ContactPoint.X)}, {N(current.ContactPoint.Y)}, {N(current.ContactPoint.Z)} · normal {N(current.Normal.X)}, {N(current.Normal.Y)}, {N(current.Normal.Z)} · slope {N(current.Slope)}°" : "no current ground-trace contact")}\nTarget: {(current.OriginDistance is null ? "aim at a volume with the inspector; uncheck Follow aimed volume to hold it" : $"ID {current.TargetId}/{current.TargetGeneration} · origin → boundary {N(current.OriginDistance)} · skin gap {N(current.SkinGap)}")}";
            if (_active is { } recording) _status.Text = $"Recording {recording.Name} · {recording.Samples.Count} samples · {recording.Duration / 1000f:0.0}s";
            UpdateComparison(); PublishOverlay(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { if (!IsDisposed) _status.Text = "Waiting for readable movement data: " + ex.Message; }
        finally { _busy = false; }
    }
    private void UpdateComparison()
    {
        var selected = CheckedAttempts(); var reference = _attempts.SelectedItem as MotionAttempt ?? selected.FirstOrDefault();
        var compatible = selected.Where(attempt => reference is not null && attempt.Map == reference.Map && attempt.Profile == reference.Profile).ToList();
        int maximum = (int)Math.Min(7_200_000, Math.Max(1, compatible.Select(attempt => attempt.Duration).DefaultIfEmpty(1).Max()));
        if (_time.Maximum != maximum) _time.Maximum = maximum;
        if (_latest.Checked && _active is { } active) _time.Value = (int)Math.Min(maximum, active.Duration);
        _timeLabel.Text = $"t = {_time.Value / 1000f:0.00}s";
        var baseline = reference?.At(_time.Value); _comparison.Rows.Clear();
        string N(float? value) => value?.ToString("0.000", CultureInfo.InvariantCulture) ?? "—";
        foreach (var attempt in compatible)
        {
            var sample = attempt.At(_time.Value);
            if (sample is null) { _comparison.Rows.Add(attempt.Name, "No sample / gap"); continue; }
            int row = _comparison.Rows.Add(attempt.Name, ((sample.Epoch - attempt.Started) / 1000f).ToString("0.000"), N(sample.Speed), N(sample.HorizontalSpeed), N(sample.Position.Z), baseline is null ? "—" : N(sample.Position.Z - baseline.Position.Z),
                baseline is null ? "—" : N(MotionVector.Distance(sample.Position, baseline.Position)), baseline is null ? "—" : N(MotionVector.Distance(sample.Velocity, baseline.Velocity)),
                sample.Contact ? "Yes" : "No", N(sample.Slope), sample.OriginDistance is null ? "—" : $"{sample.TargetId}/{sample.TargetGeneration}", N(sample.OriginDistance), N(sample.SkinGap), $"{N(sample.Maximum.X - sample.Minimum.X)} / {N(sample.Maximum.Y - sample.Minimum.Y)} / {N(sample.Maximum.Z - sample.Minimum.Z)}", sample.Shape,
                sample.Contact ? $"{N(sample.ContactPoint.X)}, {N(sample.ContactPoint.Y)}, {N(sample.ContactPoint.Z)}" : "—", sample.Contact ? $"{N(sample.Normal.X)}, {N(sample.Normal.Y)}, {N(sample.Normal.Z)}" : "—");
            _comparison.Rows[row].Cells[0].Style.ForeColor = MotionPlot.AttemptColor(attempt, compatible.IndexOf(attempt));
        }
        _plot.Set(compatible, reference, _time.Value, _projection.SelectedIndex, _align.Checked);
        if (_surface is { IsDisposed: false })
        {
            var live = _snapshot?.Samples.LastOrDefault(); long age = live is null ? 0 : Math.Max(0, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - live.Epoch);
            string source = _snapshot is null ? "Waiting for movement data" : $"{(_snapshot.Fresh(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()) ? _snapshot.Phase : "STALE capture")} · last gameplay sample {age / 1000f:0.0}s ago";
            _surface.SetSources(live, source, baseline, reference is null ? "Select a reference attempt" : baseline is null ? $"{reference.Name} · no sample at requested time" : $"{reference.Name} · sample t = {(baseline.Epoch - reference.Started) / 1000f:0.000}s (requested {_time.Value / 1000f:0.000}s)");
        }
    }
    private void PublishOverlay(bool force)
    {
        long now = Environment.TickCount64;
        if (!force && now - _lastOverlay < 1000) return; _lastOverlay = now;
        try { MotionStore.Overlay(_directory, _process, CheckedAttempts(), _snapshot); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = "Could not publish compared paths: " + ex.Message; }
    }
    private void Export()
    {
        if (_attempts.SelectedItem is not MotionAttempt { Samples.Count: > 0 } attempt) return;
        using var dialog = new SaveFileDialog { Filter = "CSV (*.csv)|*.csv", FileName = "devil-attempt.csv" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try { MotionStore.ExportCsv(attempt, dialog.FileName); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { _status.Text = "Export failed: " + ex.Message; }
    }
    protected override void Dispose(bool disposing) { if (disposing) { StopAttempt("Attempt saved."); _timer.Dispose(); } base.Dispose(disposing); }
}
