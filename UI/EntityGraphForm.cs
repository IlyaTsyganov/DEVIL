using Devil.Game;

namespace Devil;

internal sealed class EntityGraphForm : Form
{
    private readonly string _directory;
    private readonly Func<int> _gameProcess;
    private readonly TextBox _search = new() { Width = 290, PlaceholderText = "Find entity, class or ID…" };
    private readonly CheckBox _follow = new() { Text = "Follow crosshair", Checked = true, AutoSize = true, Margin = new Padding(12, 5, 0, 0) };
    private readonly CheckBox _linked = new() { Text = "Linked entities only", Checked = true, AutoSize = true, Margin = new Padding(12, 5, 0, 0) };
    private readonly Label _status = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
    private readonly Label _details = new() { Dock = DockStyle.Fill, AutoEllipsis = true, UseMnemonic = false };
    private readonly ListBox _entities = new() { Dock = DockStyle.Fill, HorizontalScrollbar = true, IntegralHeight = false };
    private readonly DataGridView _edges = new();
    private readonly EntityGraphCanvas _canvas = new() { Dock = DockStyle.Fill };
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 1000 };
    private EntityGraphSnapshot? _snapshot;
    private string? _selected;
    private int _process;
    private bool _busy, _populating;

    public EntityGraphForm(string directory, Func<int> gameProcess)
    {
        _directory = directory; _gameProcess = gameProcess;
        Text = "DEViL — Entity links"; ClientSize = new Size(1180, 740); MinimumSize = new Size(1050, 660);
        StartPosition = FormStartPosition.CenterParent; ShowInTaskbar = false;
        BackColor = Color.FromArgb(35, 35, 35); ForeColor = Color.Gainsboro; Font = new Font("Segoe UI", 10f);
        AutoScaleMode = AutoScaleMode.Font;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 1, RowCount = 4 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        foreach (float height in new[] { 38f, 60f }) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 52));
        Controls.Add(layout);
        var toolbar = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        toolbar.Controls.AddRange([_search, _follow, _linked]);
        var copy = new Button { Text = "Copy name", AutoSize = true, Margin = new Padding(12, 0, 0, 0) };
        copy.Click += (_, _) => { if (SelectedNode is { Name.Length: > 0 } node) try { Clipboard.SetText(node.Name); } catch (System.Runtime.InteropServices.ExternalException) { _status.Text = "Clipboard is busy. Try Copy name again."; } };
        var export = new Button { Text = "Export JSON…", AutoSize = true, Margin = new Padding(8, 0, 0, 0) };
        export.Click += (_, _) => Export(); toolbar.Controls.Add(copy); toolbar.Controls.Add(export);
        layout.Controls.Add(toolbar, 0, 0); layout.Controls.Add(_status, 0, 1);
        var split = new SplitContainer { Dock = DockStyle.Fill, Size = new Size(1150, 510), SplitterDistance = 270, Panel1MinSize = 220, Panel2MinSize = 600 };
        _entities.BackColor = Color.FromArgb(45, 45, 45); _entities.ForeColor = ForeColor;
        split.Panel1.Controls.Add(_entities);
        var right = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Margin = Padding.Empty };
        right.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 78)); right.RowStyles.Add(new RowStyle(SizeType.Absolute, 250)); right.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        right.Controls.Add(_details, 0, 0); right.Controls.Add(_canvas, 0, 1);
        _edges.Dock = DockStyle.Fill; _edges.ReadOnly = true; _edges.RowHeadersVisible = false;
        _edges.AllowUserToAddRows = _edges.AllowUserToDeleteRows = false; _edges.MultiSelect = false;
        _edges.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        _edges.BackgroundColor = _edges.DefaultCellStyle.BackColor = Color.FromArgb(45, 45, 45);
        _edges.DefaultCellStyle.ForeColor = ForeColor; _edges.DefaultCellStyle.SelectionBackColor = Color.FromArgb(80, 50, 80);
        foreach (var (label, width) in new[] { ("Direction", 110), ("Related entity", 250), ("Reference field", 210), ("Evidence", 260) })
            _edges.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = label, Width = width, SortMode = DataGridViewColumnSortMode.NotSortable });
        _edges.CellDoubleClick += (_, e) => { if (e.RowIndex >= 0 && _edges.Rows[e.RowIndex].Tag is string key) Choose(key); };
        right.Controls.Add(_edges, 0, 2); split.Panel2.Controls.Add(right); layout.Controls.Add(split, 0, 2);
        layout.Controls.Add(new Label { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft,
            Text = "Wheel: zoom · Hold RMB: move graph · LMB: select a node · Fit graph: show all neighbors. Double-click a table row to explore.\nArrows show observed references, not execution order. Enable the volume inspector to follow a trigger under the crosshair.",
            Font = new Font(Font.FontFamily, 9f) }, 0, 3);
        _search.TextChanged += (_, _) => Populate(); _linked.CheckedChanged += (_, _) => Populate();
        _entities.SelectedIndexChanged += (_, _) => { if (!_populating && _entities.SelectedItem is EntityGraphNode node) Choose(node.Key); };
        _canvas.NodeClicked += Choose;
        _timer.Tick += async (_, _) => await RefreshSnapshot();
        Shown += async (_, _) => { _timer.Start(); await RefreshSnapshot(); };
    }
    private EntityGraphNode? SelectedNode => _selected is not null && _snapshot?.Nodes.TryGetValue(_selected, out var node) == true ? node : null;
    private void Choose(string key)
    {
        _follow.Checked = false; _selected = key; Populate(); DrawSelection();
    }
    private void Populate()
    {
        int top = _entities.TopIndex;
        _populating = true; _entities.BeginUpdate(); _entities.Items.Clear();
        if (_snapshot is { Ready: true }) foreach (var node in _snapshot.Search(_search.Text, _linked.Checked)) _entities.Items.Add(node);
        for (int i = 0; i < _entities.Items.Count; ++i) if (((EntityGraphNode)_entities.Items[i]).Key == _selected) { _entities.SelectedIndex = i; break; }
        if (_entities.Items.Count > 0) _entities.TopIndex = Math.Min(top, _entities.Items.Count - 1);
        _entities.EndUpdate(); _populating = false;
    }
    private void DrawSelection()
    {
        int first = _edges.FirstDisplayedScrollingRowIndex;
        _edges.Rows.Clear();
        var node = SelectedNode;
        _canvas.SetGraph(_snapshot, node?.Key);
        if (node is null) { _details.Text = "Select an entity from the list, or aim at a volume with Follow crosshair enabled."; return; }
        _details.Text = $"{node.Display}\n{node.Class} · ID / generation: {node.Key}\n{node.Label}";
        foreach (var edge in _snapshot!.Edges.Where(edge => edge.From == node.Key || edge.To == node.Key))
        {
            bool outgoing = edge.From == node.Key; string related = outgoing ? edge.To : edge.From;
            int row = _edges.Rows.Add(outgoing ? "Outgoing →" : "← Incoming", _snapshot.Nodes[related].Display, edge.Field, edge.Evidence);
            _edges.Rows[row].Tag = related;
            foreach (DataGridViewCell cell in _edges.Rows[row].Cells) cell.ToolTipText = $"{_snapshot.Nodes[edge.From].Display} → {_snapshot.Nodes[edge.To].Display}\n{edge.Field}\n{edge.Evidence}";
        }
        if (first >= 0 && first < _edges.RowCount) _edges.FirstDisplayedScrollingRowIndex = first;
    }
    private async Task RefreshSnapshot()
    {
        if (_busy || IsDisposed) return; _busy = true;
        try
        {
            int process = _gameProcess();
            if (process != _process) { _process = process; _snapshot = null; _selected = null; Populate(); DrawSelection(); }
            if (process <= 0) { _status.Text = "The selected game is not running. Launch it through DEViL."; return; }
            EntityGraphSnapshot.Request(_directory, process);
            var snapshot = await Task.Run(() => EntityGraphSnapshot.Read(_directory, process));
            if (IsDisposed) return;
            if (_gameProcess() != process) { _snapshot = null; _selected = null; Populate(); DrawSelection(); _status.Text = "Game process changed. Waiting for new data…"; return; }
            if (_snapshot?.MapIdentity != snapshot?.MapIdentity || _snapshot?.Map != snapshot?.Map) _selected = null;
            _snapshot = snapshot;
            if (snapshot is { Ready: true })
            {
                if (_follow.Checked && snapshot.IsFresh(DateTimeOffset.UtcNow) && snapshot.Selected.Length > 0) _selected = snapshot.Selected;
                if (_selected is not null && !snapshot.Nodes.ContainsKey(_selected)) _selected = null;
            }
            else _selected = null;
            Populate(); DrawSelection();
            bool fresh = snapshot?.IsFresh(DateTimeOffset.UtcNow) == true;
            _status.ForeColor = fresh ? ForeColor : Color.FromArgb(245, 190, 90);
            _canvas.Enabled = _edges.Enabled = fresh && snapshot?.Ready == true;
            _entities.ForeColor = fresh ? ForeColor : Color.Gray;
            _status.Text = snapshot is null ? "Waiting for the updated viewer layer. Enable Collision Viewer, then restart the game through DEViL."
                : $"{(fresh ? snapshot.Phase switch { "live" => "Observed references", "metadata_pending" => "Resolving entity metadata…", "loading" => "Loading / no active level", "waiting_meathook" => "Waiting for compatible meathook / game-frame callback", "metadata_unavailable" => "Entity metadata unavailable", _ => snapshot.Phase } : "Stale snapshot — last captured references")}" +
                  $" · {snapshot.Nodes.Count} entities · {snapshot.Edges.Count} links · {snapshot.Pending} pending types · {snapshot.Unavailable} unavailable · {snapshot.Limited} scan limits\n{snapshot.Map}";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            if (!IsDisposed) { _snapshot = null; _selected = null; Populate(); DrawSelection(); _status.Text = "Waiting for a readable graph snapshot…"; }
        }
        finally { _busy = false; }
    }
    private void Export()
    {
        if (_snapshot is not { Ready: true } snapshot) return;
        using var dialog = new SaveFileDialog { Filter = "Entity graph JSON (*.json)|*.json", FileName = "devil-entity-links.json" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try { snapshot.ExportJson(dialog.FileName); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { MessageBox.Show(this, ex.Message, "Export failed"); }
    }
    protected override void Dispose(bool disposing) { if (disposing) _timer.Dispose(); base.Dispose(disposing); }
}
