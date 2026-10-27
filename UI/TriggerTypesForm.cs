using Devil.Configuration;

namespace Devil;

internal sealed class TriggerTypesForm : Form
{
    private sealed record Choice(string ClassName, string? Prefix = null)
    {
        public string Key => ClassName + "|" + Prefix;
    }
    private readonly DevilSettings _settings;
    private readonly string _discoveryPath, _pathDiscoveryPath;
    private readonly Action _settingsChanged;
    private readonly HashSet<string> _additionalNames = new(StringComparer.Ordinal);
    private readonly HashSet<TriggerPathInfo> _paths = [];
    private readonly TextBox _search = new();
    private readonly TreeView _classes = new();
    private readonly Label _summary = new();
    private readonly HashSet<Choice> _listed = [];
    private readonly HashSet<string> _expanded = new(StringComparer.Ordinal);
    private readonly System.Windows.Forms.Timer _discoveryTimer = new() { Interval = 1500 };
    private bool _populating, _firstPopulation = true;
    private string _hiddenSignature = "";

    public TriggerTypesForm(DevilSettings settings, string discoveryPath, Action settingsChanged)
    {
        _settings = settings; _discoveryPath = discoveryPath; _settingsChanged = settingsChanged;
        _pathDiscoveryPath = Path.Combine(Path.GetDirectoryName(discoveryPath) ?? "", "trigger-paths.tsv");
        _additionalNames.UnionWith((settings.HiddenTriggerClasses ?? []).Where(TriggerClassCatalog.IsValidName));
        foreach (var path in TriggerPathCatalog.Known)
        { _paths.Add(path); _additionalNames.Add(path.ClassName); }
        foreach (var path in (settings.HiddenTriggerPaths ?? []).Where(TriggerPathCatalog.IsValid))
        { _paths.Add(path); _additionalNames.Add(path.ClassName); }
        Text = "DEViL — Trigger types & paths";
        StartPosition = FormStartPosition.CenterParent; ClientSize = new Size(850, 630); MinimumSize = new Size(720, 490);
        BackColor = Color.FromArgb(35, 35, 35); ForeColor = Color.Gainsboro; Font = new Font("Segoe UI", 10f);
        AutoScaleMode = AutoScaleMode.Font; ShowInTaskbar = false; MinimizeBox = false;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(14), ColumnCount = 1, RowCount = 6 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        foreach (float height in new[] { 28f, 32f, 32f }) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 30)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 55));
        Controls.Add(layout);
        layout.Controls.Add(new Label { Text = "Expand a class to filter paths in trigger names and entity definitions.", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 0);
        _search.PlaceholderText = "Search class, description or path (props, interact…)";
        _search.Dock = DockStyle.Fill; _search.BackColor = Color.FromArgb(55, 55, 55); _search.ForeColor = ForeColor;
        _search.AccessibleName = "Search trigger classes and name paths"; layout.Controls.Add(_search, 0, 1);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        var show = MakeButton("Show listed", 125); var hide = MakeButton("Hide listed", 125);
        show.Click += (_, _) => SetListedVisibility(true); hide.Click += (_, _) => SetListedVisibility(false);
        actions.Controls.Add(show); actions.Controls.Add(hide); layout.Controls.Add(actions, 0, 2);
        _classes.Dock = DockStyle.Fill; _classes.BackColor = Color.FromArgb(45, 45, 45); _classes.ForeColor = ForeColor;
        _classes.CheckBoxes = true; _classes.HideSelection = false; _classes.ShowNodeToolTips = true; _classes.FullRowSelect = true;
        _classes.AccessibleName = "Visible trigger classes with name path subtypes";
        layout.Controls.Add(_classes, 0, 3);
        _summary.Dock = DockStyle.Fill; _summary.TextAlign = ContentAlignment.MiddleLeft; layout.Controls.Add(_summary, 0, 4);
        var footer = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = Padding.Empty };
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); footer.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90));
        footer.Controls.Add(new Label { Text = "Resource catalogue + live discoveries. Saved automatically; applies live.\nParent switches override children; choices are retained. Path reading needs meathook.",
            Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, Font = new Font(Font.FontFamily, 9f) }, 0, 0);
        var close = MakeButton("Close", 84); close.DialogResult = DialogResult.OK; close.Anchor = AnchorStyles.Right;
        footer.Controls.Add(close, 1, 0); layout.Controls.Add(footer, 0, 5); CancelButton = close;
        _search.TextChanged += (_, _) => Populate();
        _classes.AfterCheck += (_, e) =>
        {
            if (_populating || e.Node?.Tag is not Choice item) return;
            Apply(item, e.Node.Checked); _settingsChanged(); _hiddenSignature = Signature(); StyleNodes(); UpdateSummary();
        };
        ReadDiscoveredClasses(); Populate();
        _discoveryTimer.Tick += (_, _) => { if (ReadDiscoveredClasses() || _hiddenSignature != Signature()) Populate(); };
        _discoveryTimer.Start();
    }
    private static Button MakeButton(string text, int width) => new()
    {
        Text = text, Size = new Size(width, 28), FlatStyle = FlatStyle.Flat, BackColor = Color.FromArgb(65, 65, 65),
        ForeColor = Color.Gainsboro, Margin = new Padding(0, 0, 8, 0), UseVisualStyleBackColor = false
    };
    private string Signature() => string.Join('|', _settings.HiddenTriggerClasses ?? []) + "\n"
        + string.Join('\n', (_settings.HiddenTriggerPaths ?? []).Select(path => path.ClassName + "|" + path.Prefix));
    private bool ReadDiscoveredClasses()
    {
        bool changed = false;
        try
        {
            var file = new FileInfo(_discoveryPath);
            if (file.Exists && file.Length <= 65536) foreach (string name in File.ReadLines(_discoveryPath).Take(512))
                if (TriggerClassCatalog.IsValidName(name)) changed |= _additionalNames.Add(name);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        foreach (var path in TriggerPathCatalog.Read(_pathDiscoveryPath))
        { changed |= _paths.Add(path); changed |= _additionalNames.Add(path.ClassName); }
        foreach (var path in (_settings.HiddenTriggerPaths ?? []).Where(TriggerPathCatalog.IsValid))
        { changed |= _paths.Add(path); changed |= _additionalNames.Add(path.ClassName); }
        return changed;
    }
    private IEnumerable<TreeNode> AllNodes(TreeNodeCollection nodes)
    {
        foreach (TreeNode node in nodes) { yield return node; foreach (var child in AllNodes(node.Nodes)) yield return child; }
    }
    private void Populate()
    {
        _hiddenSignature = Signature();
        string? selected = (_classes.SelectedNode?.Tag as Choice)?.Key, top = (_classes.TopNode?.Tag as Choice)?.Key;
        foreach (var node in AllNodes(_classes.Nodes))
        {
            string key = ((Choice)node.Tag!).Key;
            if (node.IsExpanded) _expanded.Add(key); else _expanded.Remove(key);
        }
        _populating = true; _classes.BeginUpdate();
        try
        {
            _classes.Nodes.Clear(); _listed.Clear(); string query = _search.Text.Trim();
            foreach (var info in TriggerClassCatalog.Choices(_additionalNames))
            {
                bool classMatch = (info.ClassName + " " + info.Label).Contains(query, StringComparison.OrdinalIgnoreCase);
                var prefixes = _paths.Where(path => path.ClassName == info.ClassName).SelectMany(path => TriggerPathCatalog.Ancestors(path.Prefix))
                    .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
                var matches = prefixes.Where(prefix => classMatch || DisplayPath(prefix).Contains(query, StringComparison.OrdinalIgnoreCase)).ToHashSet(StringComparer.Ordinal);
                if (!classMatch && matches.Count == 0) continue;
                var rootChoice = new Choice(info.ClassName);
                var root = new TreeNode(info.ToString()) { Tag = rootChoice, Checked = _settings.IsTriggerClassVisible(info.ClassName), ToolTipText = info.Label };
                _classes.Nodes.Add(root); if (classMatch) _listed.Add(rootChoice);
                var nodes = new Dictionary<string, TreeNode>(StringComparer.Ordinal);
                foreach (string prefix in prefixes)
                {
                    if (!matches.Contains(prefix) && !matches.Any(match => match.StartsWith(prefix, StringComparison.Ordinal))) continue;
                    var item = new Choice(info.ClassName, prefix);
                    string? parentPrefix = TriggerPathCatalog.Ancestors(prefix).SkipLast(1).LastOrDefault();
                    var parent = parentPrefix is not null && nodes.TryGetValue(parentPrefix, out var existing) ? existing : root;
                    string label = parent == root ? prefix : prefix[parentPrefix!.Length..];
                    var node = new TreeNode(label) { Tag = item, Checked = _settings.IsTriggerPathExplicitlyVisible(info.ClassName, prefix), ToolTipText = info.ClassName + " · " + DisplayPath(prefix) };
                    parent.Nodes.Add(node); nodes[prefix] = node; if (matches.Contains(prefix)) _listed.Add(item);
                }
                if (_firstPopulation && info.ClassName == "idTrigger") { root.Expand(); foreach (TreeNode child in root.Nodes) child.Expand(); }
            }
            foreach (var node in AllNodes(_classes.Nodes))
            {
                string key = ((Choice)node.Tag!).Key;
                if (_expanded.Contains(key) || query.Length > 0) node.Expand();
                if (key == selected) _classes.SelectedNode = node;
            }
            var topNode = AllNodes(_classes.Nodes).FirstOrDefault(node => ((Choice)node.Tag!).Key == top);
            if (topNode is not null) _classes.TopNode = topNode;
            _firstPopulation = false; StyleNodes();
        }
        finally { _classes.EndUpdate(); _populating = false; }
        UpdateSummary();
    }
    private static string DisplayPath(string prefix) => prefix;
    private void Apply(Choice item, bool visible)
    {
        if (item.Prefix is null) _settings.SetTriggerClassVisible(item.ClassName, visible);
        else _settings.SetTriggerPathVisible(item.ClassName, item.Prefix, visible);
    }
    private void StyleNodes()
    {
        foreach (var node in AllNodes(_classes.Nodes)) if (node.Tag is Choice { Prefix: { } prefix } item)
            node.ForeColor = _settings.IsTriggerPathVisible(item.ClassName, prefix) ? ForeColor : Color.Gray;
    }
    private void SetListedVisibility(bool visible)
    {
        if (_listed.Count == 0) return;
        foreach (var item in _listed) Apply(item, visible);
        _settingsChanged(); Populate();
    }
    private void UpdateSummary()
    {
        int classes = _classes.Nodes.Count, paths = AllNodes(_classes.Nodes).Count(node => node.Parent is not null);
        _summary.Text = $"{classes} classes · {paths} folders listed · {(_settings.HiddenTriggerPaths ?? []).Count} folder rules hidden";
    }
    protected override void Dispose(bool disposing) { if (disposing) _discoveryTimer.Dispose(); base.Dispose(disposing); }
}
