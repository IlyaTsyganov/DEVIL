using Devil.Game;

namespace Devil;

internal sealed class PracticeSaveAdvancedForm : Form
{
    private readonly PracticeSaveVault _vault;
    private readonly string _directory;
    private readonly Func<int> _gameProcess;
    private readonly Action<PracticeSave>? _link;
    private readonly ComboBox _remote = new() { Width = 740, DropDownStyle = ComboBoxStyle.DropDown };
    private readonly ComboBox _slot = new() { Width = 200, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox _name = new() { Width = 270, PlaceholderText = "Practice save name", MaxLength = 160 };
    private readonly TextBox _category = new() { Width = 230, Text = "Research", PlaceholderText = "Category / route", MaxLength = 160 };
    private readonly TextBox _map = new() { Width = 290, PlaceholderText = "Map tag (optional; your annotation)", MaxLength = 512 };
    private readonly TextBox _search = new() { Width = 360, PlaceholderText = "Search saved presets…" };
    private readonly CheckBox _cloud = new() { Text = "I have disabled Steam Cloud for DOOM Eternal", AutoSize = true };
    private readonly TextBox _details = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly Label _status = new() { Dock = DockStyle.Fill, AutoEllipsis = true, UseMnemonic = false };
    private readonly DataGridView _grid = new();
    private readonly List<Button> _buttons = [];
    private IReadOnlyList<PracticeSave> _all = [];
    private List<PracticeSave> _visible = [];
    private bool _busy, _populating;

    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal string SelectedRemote { get => _remote.Text; set => _remote.Text = value; }
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal string SelectedSlot { get => _slot.Text; set => _slot.SelectedItem = value; }
    public PracticeSaveAdvancedForm(string directory, Action<PracticeSave>? link = null, PracticeSaveVault? vault = null, ResearchBookmark? context = null, Func<int>? gameProcess = null)
    {
        _vault = vault ?? new(directory); _link = link; _directory = directory; _gameProcess = gameProcess ?? (() => 0);
        Text = "DEViL — Practice saves: full slots, validation & recovery"; ClientSize = new(1240, 820); MinimumSize = new(1160, 760);
        StartPosition = FormStartPosition.CenterParent; ShowInTaskbar = false; AutoScaleMode = AutoScaleMode.Font;
        BackColor = Color.FromArgb(35, 35, 35); ForeColor = Color.Gainsboro; Font = new Font("Segoe UI", 10f);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 1, RowCount = 10 };
        foreach (float height in new[] { 40f, 38f, 38f, 38f, 36f, 38f, 140f }) layout.RowStyles.Add(new(SizeType.Absolute, height));
        layout.RowStyles.Add(new(SizeType.Percent, 100)); layout.RowStyles.Add(new(SizeType.Absolute, 32)); layout.RowStyles.Add(new(SizeType.Absolute, 100)); Controls.Add(layout);
        FlowLayoutPanel Bar(int row) { var bar = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty }; layout.Controls.Add(bar, 0, row); return bar; }
        Button Button(string text, int width, Action action) { var button = new Button { Text = text, Size = new(width, 30), FlatStyle = FlatStyle.Flat, BackColor = Color.FromArgb(65, 65, 65), ForeColor = ForeColor, Margin = new(0, 0, 8, 0) }; button.Click += (_, _) => action(); _buttons.Add(button); return button; }
        var location = Bar(0); location.Controls.Add(new Label { Text = "Steam saves", AutoSize = true, Margin = new(0, 6, 10, 0) }); location.Controls.Add(_remote);
        location.Controls.Add(Button("Browse…", 90, BrowseRemote)); location.Controls.Add(Button("Find", 65, Discover));
        var actions = Bar(1); _slot.Items.AddRange(PracticeSaveVault.Slots); _slot.SelectedIndex = 0; actions.Controls.Add(_slot);
        actions.Controls.Add(Button("Inspect slot", 120, () => _ = Inspect()));
        actions.Controls.Add(Button("Archive full slot", 160, () => _ = CaptureSlot()));
        actions.Controls.Add(Button("Recover backup pair…", 200, () => _ = Recover()));
        actions.Controls.Add(new Label { Text = "Offline actions: close game + Steam.", AutoSize = true, Margin = new(8, 6, 0, 0) });
        var tags = Bar(2); tags.Controls.AddRange([_name, _category, _map]);
        var archives = Bar(3); archives.Controls.Add(Button("Import folder…", 140, () => _ = Import())); archives.Controls.Add(Button("Export folder…", 140, () => _ = Export()));
        archives.Controls.Add(Button("Verify archive", 135, () => _ = Verify())); archives.Controls.Add(Button("Review restore…", 160, () => _ = Restore()));
        if (_link is not null) archives.Controls.Add(Button("Link to research point", 205, () => _ = Link()));
        var filtering = Bar(4); filtering.Controls.Add(_search); filtering.Controls.Add(_cloud); _cloud.Margin = new(18, 3, 0, 0);
        var live = Bar(5); live.Controls.Add(Button("Archive with game open", 210, () => _ = CaptureLive()));
        live.Controls.Add(Button("Test current checkpoint reload…", 250, () => _ = TestReload()));
        live.Controls.Add(Button("Restore archive + reload…", 230, () => _ = RestoreLive()));
        live.Controls.Add(new Label { Text = "6.66 Rev 3 · TAG1 slot 3 · pause first", AutoSize = true, Margin = new(4, 5, 0, 0) });
        layout.Controls.Add(_details, 0, 6); _details.BackColor = Color.FromArgb(45, 45, 45); _details.ForeColor = ForeColor;
        _grid.Dock = DockStyle.Fill; _grid.ReadOnly = true; _grid.AllowUserToAddRows = _grid.AllowUserToDeleteRows = false;
        _grid.MultiSelect = false; _grid.RowHeadersVisible = false; _grid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        _grid.BackgroundColor = _grid.DefaultCellStyle.BackColor = Color.FromArgb(45, 45, 45); _grid.DefaultCellStyle.ForeColor = ForeColor; _grid.DefaultCellStyle.SelectionBackColor = Color.FromArgb(85, 55, 85);
        foreach (var (text, width) in new[] { ("Category", 200), ("Name", 275), ("Original slot", 165), ("Map tag", 250), ("Archived", 170), ("Source", 190) })
            _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = text, Width = width, SortMode = DataGridViewColumnSortMode.NotSortable });
        _grid.SelectionChanged += (_, _) => { if (!_populating) ShowSelected(); };
        layout.Controls.Add(_grid, 0, 7); layout.Controls.Add(_status, 0, 8);
        layout.Controls.Add(new Label { Dock = DockStyle.Fill, Font = new(Font.FontFamily, 9f), UseMnemonic = false, Text = "Archives retain the whole checkpoint slot, including both -BACKUP containers. Authentication cannot prove that scripts/progression are logically consistent.\nLive operations require pause and effective Cloud OFF (globally or per game); they are restricted to TAG1 slot 3 on 6.66 Rev 3.\nRestore archive + reload writes a verified live archive through Steam storage, retains all four originals, verifies disk/cache bytes and requests engine loading.\nAt 100% loading, press Space in game and pause again. Offline Review restore / recovery keep their closed-process guards; account ownership is never changed.\nArchives are completed disk checkpoints, not arbitrary RAM states. PROFILE and other slots are not replaced; Steam maintains its own file metadata." }, 0, 9);
        if (context is not null) { _name.Text = context.Name; _category.Text = context.Category; _map.Text = context.Observation.Map; }
        _search.TextChanged += (_, _) => Populate(); Discover(); Reload();
    }
    private PracticeSave? Selected => _grid.CurrentCell is { RowIndex: >= 0 } cell && cell.RowIndex < _visible.Count ? _visible[cell.RowIndex] : null;
    private void Discover()
    {
        string previous = _remote.Text; _remote.Items.Clear(); foreach (string path in SteamSaveLocation.Discover()) _remote.Items.Add(path);
        if (previous.Length > 0) _remote.Text = previous; else if (_remote.Items.Count == 1) _remote.SelectedIndex = 0;
    }
    private void BrowseRemote()
    {
        using var dialog = new FolderBrowserDialog { Description = "Choose Steam/userdata/<account>/782330/remote", UseDescriptionForTitle = true };
        if (dialog.ShowDialog(this) == DialogResult.OK) _remote.Text = dialog.SelectedPath;
    }
    private void Reload(string? id = null) { _all = _vault.List(); Populate(id); }
    private void Populate(string? id = null)
    {
        id ??= Selected?.Id; string query = _search.Text.Trim(); _visible = _all.Where(s => $"{s.Name} {s.Category} {s.Map} {s.SourceSlot}".Contains(query, StringComparison.OrdinalIgnoreCase)).ToList();
        _populating = true; _grid.Rows.Clear();
        foreach (var item in _visible) _grid.Rows.Add(item.Category, item.Name, item.SourceSlot, item.Map, DateTimeOffset.FromUnixTimeMilliseconds(item.Captured).ToLocalTime().ToString("yyyy-MM-dd HH:mm"), item.FromBackupPair ? "Recovered -BACKUP pair" : "Full checkpoint slot");
        int index = _visible.FindIndex(s => s.Id == id); if (index >= 0) _grid.CurrentCell = _grid.Rows[index].Cells[0];
        _populating = false; ShowSelected();
    }
    private void ShowSelected()
    {
        if (Selected is not { } value) return;
        _details.Text = $"{value.Name} · {value.Category}\r\nSource {value.SourceSlot} · Steam account {value.Account}\r\nMap tag: {value.Map} · Build annotation: {value.Build}\r\nArchive {value.Id} · {value.Files.Count} files (listing is metadata-only; Verify checks hashes and AES-GCM)\r\n"
            + (value.FromBackupPair ? "Recovered from the older internal backup pair. Latest primary progress may be lost.\r\n" : "")
            + string.Join("\r\n", value.Files.Select(f => $"{f.Name}: {f.Length:N0} bytes · SHA256 {f.Sha256}"));
    }
    private async Task Run(Func<Task> action)
    {
        if (_busy) return; _busy = true; foreach (var button in _buttons) button.Enabled = false;
        try { await action(); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Security.Cryptography.CryptographicException or System.ComponentModel.Win32Exception or InvalidOperationException)
        { if (!IsDisposed) { _status.Text = ex.Message; _details.Text = ex.Message; } }
        finally { _busy = false; if (!IsDisposed) foreach (var button in _buttons) button.Enabled = true; }
    }
    private Task Inspect() => Run(async () =>
    {
        string remote = _remote.Text, slot = _slot.Text; _status.Text = "Reading encrypted slot containers…";
        var health = await Task.Run(() => _vault.Inspect(remote, slot)); if (IsDisposed) return;
        _details.Text = health.ToString().Replace("\n", "\r\n"); _status.Text = health.Authenticated ? "All four containers authenticated; this is not a semantic game-state validation." : health.CanRecover ? "Current pair failed/missing; both backups authenticate. Close game/Steam and recheck before preparing recovery." : "Missing/failed containers: close game/Steam and recheck. Wrong account/format or changing files can also cause failure.";
    });
    private Task CaptureSlot() => Run(async () =>
    {
        string remote = _remote.Text, slot = _slot.Text, name = _name.Text.Trim(), category = _category.Text.Trim(), map = _map.Text.Trim(); _status.Text = "Archiving full slot…";
        var saved = await Task.Run(() => _vault.Capture(remote, slot, name, category, map)); if (IsDisposed) return; Reload(saved.Id); _status.Text = "Full slot archived and verified, including both -BACKUP containers.";
    });
    private Task CaptureLive() => Run(async () =>
    {
        string remote = _remote.Text, slot = _slot.Text, name = _name.Text.Trim(), category = _category.Text.Trim(), map = _map.Text.Trim(); bool cloud = _cloud.Checked;
        if (name.Length == 0 || category.Length == 0) throw new IOException("Enter a name and category first.");
        _status.Text = "Waiting for a paused, idle engine checkpoint…";
        var capture = await LivePracticeBridge.Execute(_directory, _gameProcess, "archive", remote, slot, cloud);
        var saved = await Task.Run(() => _vault.ImportLiveCheckpoint(_directory, capture, remote, name, category, map));
        if (IsDisposed) return; Reload(saved.Id); _status.Text = "Live checkpoint copy archived; all four containers authenticated. Live slot was only read.";
    });
    private Task TestReload() => Run(async () =>
    {
        string remote = _remote.Text, slot = _slot.Text; bool cloud = _cloud.Checked;
        _ = LivePracticeBridge.RequestText(_gameProcess(), Guid.NewGuid().ToString("N"), DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), "reload", SteamSaveLocation.Parse(remote), slot, cloud);
        var health = await Task.Run(() => _vault.Inspect(remote, slot)); if (!health.Authenticated) throw new IOException("All four current checkpoint containers must authenticate before the reload experiment.");
        if (IsDisposed) return;
        if (MessageBox.Show(this, "Load the CURRENT primary checkpoint of TAG1 slot 3 through the engine?\nMovement and progress since the last checkpoint will be lost.\nNo archive files will be installed; this tests the engine loader only.\nKeep the game paused until the request starts. At 100% loading press Space, then pause again.", "Test current checkpoint reload", MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK) return;
        _status.Text = "Waiting for checkpoint loading. At 100% press Space in game, then pause.";
        var result = await LivePracticeBridge.Execute(_directory, _gameProcess, "reload", remote, slot, cloud, p => { if (!IsDisposed && p.Phase == "loading") _status.Text = "Loading checkpoint: at 100% press Space in game, then pause."; });
        if (IsDisposed) return; _status.Text = result.Detail;
        _details.Text = $"{result.Detail}\r\nMap after load: {result.Map}\r\nLoaded checkpoint: {result.Checkpoint}\r\nSteam account: {result.Account} · {result.Slot}\r\nThis verifies an observed engine reload, not archive restoration or arbitrary RAM savestates.";
    });
    private Task RestoreLive() => Run(async () =>
    {
        if (Selected is not { } item) return; int process = _gameProcess(); string remote = _remote.Text, slot = _slot.Text; bool cloud = _cloud.Checked;
        var plan = await Task.Run(() => _vault.PlanLiveRestore(process, item.Id, remote, slot, cloud)); if (IsDisposed) return;
        if (MessageBox.Show(this, plan.Description + "\n\nInstall this archive and reload the checkpoint?", "Review live training restore", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) != DialogResult.OK) return;
        await Task.Run(() => _vault.StageLiveRestore(_directory, plan)); if (IsDisposed) return;
        _details.Text = plan.Description; _status.Text = "Waiting for paused engine and Steam storage checks…";
        var result = await LivePracticeBridge.ExecuteRestore(_directory, _gameProcess, plan, cloud, p =>
        {
            if (!IsDisposed && p.Phase == "loading") { _status.Text = "Archive installed; loading checkpoint. At 100% press Space in game, then pause."; _details.Text = p.Detail + "\r\n" + plan.Description; }
        });
        if (IsDisposed) return; _status.Text = "Live restore and engine map transition completed. Check the restored checkpoint in game.";
        _details.Text = $"{result.Detail}\r\nLoaded map: {result.Map}\r\nLoaded checkpoint: {result.Checkpoint}\r\nOriginal four-file checkpoint: {plan.Original}";
    });
    private Task Recover() => Run(async () =>
    {
        string remote = _remote.Text, slot = _slot.Text, name = _name.Text.Trim(), category = _category.Text.Trim();
        _vault.RequireClosed(); var health = await Task.Run(() => _vault.Inspect(remote, slot)); if (IsDisposed) return;
        if (!health.CanRecover) throw new IOException("Recovery needs a missing/failed primary pair and BOTH authenticated -BACKUP containers.");
        if (MessageBox.Show(this, "Create a separate practice archive from the two older -BACKUP files?\nLatest checkpoint progress may be lost. The live slot will not be changed; raw originals will be retained as recovery evidence.", "Prepare recovered archive", MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK) return;
        var saved = await Task.Run(() => _vault.RecoverBackupPair(remote, slot, name, category)); if (IsDisposed) return; Reload(saved.Id); _status.Text = "Recovered archive prepared; live slot unchanged. Review restore separately when ready.";
    });
    private Task Verify() => Run(async () =>
    {
        if (Selected is not { } item) return; _status.Text = "Checking every file hash and encrypted container…";
        var verified = await Task.Run(() => _vault.Verify(item.Id)); if (IsDisposed) return; _status.Text = $"Verified {verified.Files.Count} archived files and all four authenticated containers. Script/progression consistency is not established.";
    });
    private Task Link() => Run(async () =>
    {
        if (Selected is not { } item || _link is null) return;
        var verified = await Task.Run(() => _vault.Verify(item.Id)); if (!IsDisposed) { _link(verified); _status.Text = "Archive reference linked; no save was loaded."; }
    });
    private Task Restore() => Run(async () =>
    {
        if (Selected is not { } item) return; string remote = _remote.Text, slot = _slot.Text; bool cloud = _cloud.Checked;
        var plan = await Task.Run(() => _vault.PlanRestore(item.Id, remote, slot, cloud)); if (IsDisposed) return;
        if (MessageBox.Show(this, plan.Description + "\n\nCommit this replacement?", "Review practice-save restore", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) != DialogResult.OK) return;
        var result = await Task.Run(() => _vault.Restore(plan)); if (IsDisposed) return; _cloud.Checked = false; _status.Text = result.Message; _details.Text = result.Message + "\r\nOriginal slot: " + result.OriginalSlot + "\r\nRecovery evidence: " + result.Evidence;
    });
    private Task Import() => Run(async () =>
    {
        using var dialog = new FolderBrowserDialog { Description = "Select an exported DEViL practice-save folder containing manifest.json and slot/", UseDescriptionForTitle = true };
        if (dialog.ShowDialog(this) != DialogResult.OK) return; string path = dialog.SelectedPath; string account = SteamSaveLocation.Parse(_remote.Text).Account;
        var saved = await Task.Run(() => _vault.Import(path, account)); if (IsDisposed) return; Reload(saved.Id); _status.Text = "Same-account archive imported and verified. Account ownership is never changed.";
    });
    private Task Export() => Run(async () =>
    {
        if (Selected is not { } item) return; using var dialog = new FolderBrowserDialog { Description = "Choose a parent folder; a new archive-ID subfolder will be created", UseDescriptionForTitle = true };
        if (dialog.ShowDialog(this) != DialogResult.OK) return; string path = Path.Combine(dialog.SelectedPath, "DEViL-practice-" + item.Id);
        await Task.Run(() => _vault.Export(item.Id, path)); if (!IsDisposed) _status.Text = "Archive exported to: " + path;
    });
}
