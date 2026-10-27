using System.Diagnostics;
using System.Drawing.Text;
using System.Runtime.InteropServices;
using Devil.Configuration;
using Devil.Game;

namespace Devil;

internal sealed class MainForm : Form
{
    private static readonly Color Canvas = Color.FromArgb(35, 35, 35);
    private static readonly Color PanelColor = Color.FromArgb(45, 45, 45);
    private static readonly Color ControlColor = Color.FromArgb(70, 70, 70);
    private static readonly Color ForegroundColor = Color.FromArgb(230, 230, 230);
    private static readonly Color AccentColor = Color.FromArgb(190, 34, 34);
    private static FontFamily _uiFamily = FontFamily.GenericSansSerif;
    private static FontFamily _logoFamily = FontFamily.GenericSansSerif;
    private static FontFamily _battleFamily = FontFamily.GenericSansSerif;

    private readonly SettingsStore _settingsStore = new();
    private readonly ProcessMonitor _processMonitor = new();
    private readonly GameLauncher _gameLauncher;
    private readonly VulkanLayerRegistration _layerRegistration;
    private readonly System.Windows.Forms.Timer _refreshTimer = new() { Interval = 1500 };
    private readonly PrivateFontCollection _fonts = new();
    private readonly DevilSettings _settings;

    private readonly Label _gameStatus = new();
    private readonly Label _desruStatus = new();
    private readonly Label _viewerStatus = new();
    private readonly Label _pathValue = new();
    private readonly Label _gameVersion = new();
    private readonly ToolTip _gamePathToolTip = new() { AutoPopDelay = 30000, InitialDelay = 400, ReshowDelay = 100, ShowAlways = true };
    private readonly CheckBox _viewerEnabled = new();
    private readonly CheckBox _triggers = new();
    private readonly CheckBox _worldCollision = new();
    private readonly CheckBox _invisibleWalls = new();
    private readonly CheckBox _inspector = new();
    private readonly CheckBox _disabledVolumes = new();
    private readonly CheckBox _volumeHistory = new();
    private readonly Button _triggerTypes = new();
    private readonly RadioButton _outlines = new();
    private readonly RadioButton _filled = new();
    private readonly TrackBar _distance = new();
    private readonly NumericUpDown _distanceInput = new();
    private readonly Button _launchButton = new();
    private bool _syncingDistance;
    private bool _loadingControls;
    private ActiveEncountersForm? _activeEncounters;
    private EntityGraphForm? _entityGraph;
    private MovementLabForm? _movementLab;
    private LevelStateForm? _levelStates;
    private ResearchPointsForm? _researchPoints;
    private PracticeSaveForm? _practiceSaves;

    [DllImport("user32.dll")]
    private static extern bool ReleaseCapture();

    [DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);

    public MainForm()
    {
        _settings = _settingsStore.Load();
        _gameLauncher = new GameLauncher(_settingsStore);
        _layerRegistration = new VulkanLayerRegistration(_settingsStore);
        LoadThemeFonts();

        Text = "DEViL — DOOM Eternal Visualization & Inspection Layer";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(651, 697);
        FormBorderStyle = FormBorderStyle.None;
        ControlBox = false;
        MaximizeBox = false;
        SizeGripStyle = SizeGripStyle.Hide;
        BackColor = Canvas;
        ForeColor = ForegroundColor;
        Font = UiFont(11.25f);
        AutoScaleMode = AutoScaleMode.Font;
        try
        {
            string icon = Path.Combine(AppContext.BaseDirectory, "Assets", "devil.ico");
            if (File.Exists(icon)) Icon = new Icon(icon);
        }
        catch (IOException) { }

        BuildMainPage();
        LoadSettingsIntoControls();
        WireSettings();
        RefreshProcessStatus();
        _refreshTimer.Tick += (_, _) => RefreshProcessStatus();
        _refreshTimer.Start();
        FormClosed += (_, _) => { _refreshTimer.Stop(); _refreshTimer.Dispose(); _gamePathToolTip.Dispose(); _fonts.Dispose(); };
    }

    private void BuildMainPage()
    {
        var titleBar = new Panel { Location = Point.Empty, Size = new Size(651, 63), BackColor = Canvas };
        titleBar.MouseDown += BeginWindowDrag;
        var title = new Label
        {
            Text = "DOOM ETERNAL VISUALIZATION\n& INSPECTION LAYER",
            AutoSize = false,
            UseMnemonic = false,
            BackColor = Color.Transparent,
            Font = BattleFont(15f),
            ForeColor = AccentColor,
            Location = new Point(13, 3),
            Size = new Size(560, 52),
            TextAlign = ContentAlignment.MiddleLeft
        };
        title.MouseDown += BeginWindowDrag;
        titleBar.Controls.Add(title);
        var logo = new PictureBox
        {
            Location = new Point(590, 6),
            Size = new Size(48, 48),
            SizeMode = PictureBoxSizeMode.Zoom,
            BackColor = Color.Transparent,
            Image = Icon?.ToBitmap()
        };
        logo.MouseDown += BeginWindowDrag;
        titleBar.Controls.Add(logo);
        var separator = new Panel { BackColor = Color.FromArgb(85, 85, 85), Location = new Point(33, 59), Size = new Size(570, 2) };
        separator.MouseDown += BeginWindowDrag;
        titleBar.Controls.Add(separator);
        Controls.Add(titleBar);

        // DESRU's main window uses a fixed two-column grid inside one scrolling panel.
        var scroll = new Panel
        {
            Location = new Point(6, 63),
            Size = new Size(644, 634),
            Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
            AutoScroll = true,
            BackColor = Canvas
        };
        Controls.Add(scroll);
        var content = new Panel { Location = Point.Empty, Size = new Size(640, 632), BackColor = Canvas };
        scroll.Controls.Add(content);

        AddSection(content, "COLLISION VIEWER", 5, 14, 310);
        var viewer = AddCard(content, 5, 38, 310, 204);
        ConfigureCheck(_viewerEnabled, "Enable Collision Viewer", 11.25f);
        Place(viewer, _viewerEnabled, 8, 8);
        var caption = new Label { Text = "Collision types", AutoSize = true, ForeColor = ForegroundColor, Font = UiFont(10f), Location = new Point(8, 36) };
        viewer.Controls.Add(caption);
        ConfigureCheck(_triggers, "Triggers", 10.5f);
        ConfigureCheck(_worldCollision, "World collision", 10.5f);
        ConfigureCheck(_invisibleWalls, "Invisible walls", 10.5f);
        Place(viewer, _triggers, 20, 58);
        _triggerTypes.Text = "Trigger types…";
        StyleButton(_triggerTypes, 166, 26, ControlColor);
        Place(viewer, _triggerTypes, 128, 57);
        _triggerTypes.Click += (_, _) =>
        {
            using var dialog = new TriggerTypesForm(_settings, _settingsStore.TriggerClassesPath, SaveSettings);
            dialog.ShowDialog(this);
        };
        _gamePathToolTip.SetToolTip(_triggerTypes, "Show or hide each trigger class independently. Search by class or description; changes are saved and applied live.");
        Place(viewer, _worldCollision, 20, 86);
        Place(viewer, _invisibleWalls, 20, 114);
        ConfigureCheck(_disabledVolumes, "Show disabled volumes", 10f);
        ConfigureCheck(_volumeHistory, "Show recent changes", 10f);
        Place(viewer, _disabledVolumes, 8, 142);
        Place(viewer, _volumeHistory, 8, 170);
        _gamePathToolTip.SetToolTip(_disabledVolumes, "Show inactive volumes as pale outlines. When current geometry is unavailable, a previously observed shape is marked as a snapshot in the inspector.");
        _gamePathToolTip.SetToolTip(_volumeHistory, "Flash enabled/disabled changes for 2 seconds. Show removed or unreadable volumes as dashed snapshots for up to 15 seconds.");
        AddSection(content, "INFO PANEL", 324, 14, 310);
        var info = AddCard(content, 324, 38, 310, 204);
        Place(info, StatusRow("DOOM Eternal", _gameStatus), 8, 6);
        Place(info, StatusRow("DESRU", _desruStatus), 8, 50);
        Place(info, StatusRow("Collision Viewer", _viewerStatus), 8, 94);
        info.Controls.Add(new Label { Text = "Disabled: pale outline\nChanges: green / pink flash\nRemoved / unknown: dashed (15s)",
            AutoSize = false, Size = new Size(286, 56), Font = UiFont(8.5f), ForeColor = Color.Gainsboro, Location = new Point(8, 140) });

        AddSection(content, "RENDER SETTINGS", 5, 256, 310);
        var render = AddCard(content, 5, 280, 310, 178);
        ConfigureRadio(_outlines, "Outlines");
        ConfigureRadio(_filled, "Filled");
        Place(render, _outlines, 8, 8);
        Place(render, _filled, 112, 8);
        render.Controls.Add(new Label { Text = "VIEW DISTANCE", AutoSize = true, ForeColor = ForegroundColor, Font = UiBoldFont(9f), Location = new Point(8, 43) });
        _distance.Minimum = 20;
        _distance.Maximum = 200;
        _distance.TickFrequency = 20;
        _distance.SmallChange = 5;
        _distance.LargeChange = 20;
        _distance.BackColor = PanelColor;
        _distance.Size = new Size(218, 45);
        _distance.Location = new Point(5, 60);
        render.Controls.Add(_distance);
        _distanceInput.Minimum = _distance.Minimum;
        _distanceInput.Maximum = _distance.Maximum;
        _distanceInput.Increment = 1;
        _distanceInput.Size = new Size(59, 24);
        _distanceInput.Location = new Point(230, 67);
        _distanceInput.Font = UiFont(9.5f);
        _distanceInput.BackColor = ControlColor;
        _distanceInput.ForeColor = ForegroundColor;
        render.Controls.Add(_distanceInput);
        render.Controls.Add(new Label { Text = "20 m", AutoSize = true, ForeColor = ForegroundColor, Font = UiFont(9f), Location = new Point(8, 108) });
        render.Controls.Add(new Label { Text = "200 m", AutoSize = true, ForeColor = ForegroundColor, Font = UiFont(9f), Location = new Point(240, 108) });
        ConfigureCheck(_inspector, "Volume inspector (meathook)", 10f);
        Place(render, _inspector, 8, 130);
        _gamePathToolTip.SetToolTip(_inspector, "Aim at a visible trigger or invisible wall. Entity details use the game's native debug HUD. Requires a compatible meathook with mh_api_get. Greater view distance may reduce performance.");
        render.Controls.Add(new Label { Text = "Native game HUD; requires meathook API.", AutoSize = true,
            ForeColor = Color.Gainsboro, Font = UiFont(8f), Location = new Point(8, 156) });

        AddSection(content, "GAME", 324, 256, 310);
        var game = AddCard(content, 324, 280, 310, 178);
        _pathValue.AutoSize = false;
        _pathValue.Size = new Size(286, 24);
        _pathValue.MaximumSize = new Size(286, 24);
        _pathValue.AutoEllipsis = true;
        _pathValue.UseMnemonic = false;
        _pathValue.TextAlign = ContentAlignment.MiddleLeft;
        _pathValue.ForeColor = ForegroundColor;
        _pathValue.Font = UiFont(9f);
        _pathValue.Location = new Point(8, 8);
        game.Controls.Add(_pathValue);
        var browse = MakeButton("BROWSE…", 128, 30);
        browse.Location = new Point(8, 53);
        browse.Margin = Padding.Empty;
        browse.Click += (_, _) => BrowseForGame();
        game.Controls.Add(browse);
        _gameVersion.AutoSize = false;
        _gameVersion.Size = new Size(286, 24);
        _gameVersion.Font = UiBoldFont(9.5f);
        _gameVersion.ForeColor = ForegroundColor;
        _gameVersion.Location = new Point(8, 96);
        _gameVersion.TextAlign = ContentAlignment.MiddleLeft;
        game.Controls.Add(_gameVersion);
        _launchButton.Text = "LAUNCH DOOM ETERNAL";
        StyleButton(_launchButton, 284, 32, ControlColor);
        _launchButton.Location = new Point(8, 138);
        _launchButton.Margin = Padding.Empty;
        _launchButton.Click += (_, _) => LaunchGame();
        game.Controls.Add(_launchButton);

        var research = MakeButton("Research timeline & path…", 310, 30);
        research.UseMnemonic = false;
        research.Location = new Point(5, 472);
        research.Click += (_, _) =>
        {
            using var dialog = new ResearchForm(_settings, _settingsStore.SettingsDirectory, SaveSettings, SelectedGameProcess);
            dialog.ShowDialog(this);
        };
        content.Controls.Add(research);
        var encounters = MakeButton("Active encounters…", 152, 30);
        encounters.Location = new Point(325, 472);
        encounters.Click += (_, _) =>
        {
            if (_activeEncounters is null || _activeEncounters.IsDisposed)
            {
                _activeEncounters = new ActiveEncountersForm(_settings, _settingsStore.SettingsDirectory, SaveSettings, SelectedGameProcess);
                _activeEncounters.Show(this);
            }
            else _activeEncounters.Activate();
        };
        content.Controls.Add(encounters);
        var graph = MakeButton("Entity links…", 152, 30);
        graph.Location = new Point(483, 472);
        graph.Click += (_, _) =>
        {
            if (_entityGraph is null || _entityGraph.IsDisposed)
            {
                _entityGraph = new EntityGraphForm(_settingsStore.SettingsDirectory, SelectedGameProcess);
                _entityGraph.Show(this);
            }
            else _entityGraph.Activate();
        };
        content.Controls.Add(graph);
        var movement = MakeButton("Movement lab…", 310, 30);
        movement.Location = new Point(5, 512);
        movement.Click += (_, _) =>
        {
            if (_movementLab is null || _movementLab.IsDisposed)
            { _movementLab = new MovementLabForm(_settings, _settingsStore.SettingsDirectory, SaveSettings, SelectedGameProcess); _movementLab.Show(this); }
            else _movementLab.Activate();
        };
        content.Controls.Add(movement);
        var states = MakeButton("Level state: before / after…", 310, 30);
        states.Location = new Point(325, 512);
        states.Click += (_, _) =>
        {
            if (_levelStates is null || _levelStates.IsDisposed) { _levelStates = new LevelStateForm(_settingsStore.SettingsDirectory, SelectedGameProcess); _levelStates.Show(this); }
            else _levelStates.Activate();
        };
        content.Controls.Add(states);
        var points = MakeButton("Research points…", 310, 30);
        points.Location = new Point(5, 552);
        points.Click += (_, _) =>
        {
            if (_researchPoints is null || _researchPoints.IsDisposed) { _researchPoints = new ResearchPointsForm(_settingsStore.SettingsDirectory, SelectedGameProcess); _researchPoints.Show(this); }
            else _researchPoints.Activate();
        };
        content.Controls.Add(points);
        var saves = MakeButton("Сохранения…", 310, 30);
        saves.Location = new Point(325, 552);
        saves.Click += (_, _) =>
        {
            if (_practiceSaves is null || _practiceSaves.IsDisposed) { _practiceSaves = new PracticeSaveForm(_settingsStore.SettingsDirectory, gameProcess: SelectedGameProcess); _practiceSaves.Show(this); }
            else _practiceSaves.Activate();
        };
        content.Controls.Add(saves);
        var downloadDesru = MakeButton("Download DESRU", 152, 30);
        downloadDesru.Location = new Point(5, 592);
        downloadDesru.Margin = Padding.Empty;
        downloadDesru.Click += (_, _) => OpenLink("https://github.com/bowsr/DESRU/releases/latest");
        content.Controls.Add(downloadDesru);
        var discord = MakeButton("MDSR Discord", 152, 30);
        discord.Location = new Point(164, 592);
        discord.Margin = Padding.Empty;
        discord.Click += (_, _) => OpenLink("https://discord.gg/dtDa9VZ");
        content.Controls.Add(discord);
        var help = MakeButton("Help", 152, 30);
        help.Location = new Point(323, 592);
        help.Margin = Padding.Empty;
        help.Click += (_, _) => ShowHelp();
        content.Controls.Add(help);
        var exit = MakeButton("Exit", 152, 30);
        exit.Location = new Point(482, 592);
        exit.Margin = Padding.Empty;
        exit.Click += (_, _) => Close();
        content.Controls.Add(exit);
    }

    private void LoadSettingsIntoControls()
    {
        _viewerEnabled.Checked = _settings.CollisionViewerEnabled;
        _triggers.Checked = _settings.ShowTriggers;
        _worldCollision.Checked = _settings.ShowWorldCollision;
        _invisibleWalls.Checked = _settings.ShowInvisibleWalls;
        _inspector.Checked = _settings.InspectVolumes;
        _disabledVolumes.Checked = _settings.ShowDisabledVolumes;
        _volumeHistory.Checked = _settings.ShowVolumeHistory;
        _outlines.Checked = _settings.Style == RenderStyle.Outlines;
        _filled.Checked = _settings.Style == RenderStyle.Filled;
        int distance = Math.Clamp(_settings.ViewDistanceMeters, _distance.Minimum, _distance.Maximum);
        _distance.Value = distance;
        _distanceInput.Value = distance;
        UpdateGameSelection();
        UpdateViewerControls();
    }

    private void WireSettings()
    {
        _viewerEnabled.CheckedChanged += (_, _) => { _settings.CollisionViewerEnabled = _viewerEnabled.Checked; UpdateViewerControls(); SaveSettings(); };
        _triggers.CheckedChanged += (_, _) => { _settings.ShowTriggers = _triggers.Checked; SaveSettings(); };
        _worldCollision.CheckedChanged += (_, _) => { _settings.ShowWorldCollision = _worldCollision.Checked; SaveSettings(); };
        _invisibleWalls.CheckedChanged += (_, _) => { _settings.ShowInvisibleWalls = _invisibleWalls.Checked; SaveSettings(); };
        _inspector.CheckedChanged += (_, _) => { _settings.InspectVolumes = _inspector.Checked; SaveSettings(); };
        _disabledVolumes.CheckedChanged += (_, _) => { _settings.ShowDisabledVolumes = _disabledVolumes.Checked; SaveSettings(); };
        _volumeHistory.CheckedChanged += (_, _) => { _settings.ShowVolumeHistory = _volumeHistory.Checked; SaveSettings(); };
        _outlines.CheckedChanged += (_, _) => { if (_outlines.Checked) { _settings.Style = RenderStyle.Outlines; SaveSettings(); } };
        _filled.CheckedChanged += (_, _) => { if (_filled.Checked) { _settings.Style = RenderStyle.Filled; SaveSettings(); } };
        _distance.ValueChanged += (_, _) =>
        {
            if (_syncingDistance) return;
            _syncingDistance = true;
            _distanceInput.Value = _distance.Value;
            _syncingDistance = false;
            _settings.ViewDistanceMeters = _distance.Value;
            SaveSettings();
        };
        _distanceInput.ValueChanged += (_, _) =>
        {
            if (_syncingDistance) return;
            _syncingDistance = true;
            _distance.Value = decimal.ToInt32(_distanceInput.Value);
            _syncingDistance = false;
            _settings.ViewDistanceMeters = _distance.Value;
            SaveSettings();
        };
    }

    private void BrowseForGame()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Select DOOM Eternal executable",
            Filter = "DOOM Eternal executable (DOOMEternalx64vk.exe)|DOOMEternalx64vk.exe|Executable files (*.exe)|*.exe",
            CheckFileExists = true
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        _settings.GameExecutablePath = dialog.FileName;
        UpdateGameSelection();
        SaveSettings();
    }

    private void UpdateGameSelection()
    {
        string path = _settings.GameExecutablePath;
        bool selected = !string.IsNullOrWhiteSpace(path);
        _pathValue.Text = selected ? path : "No game executable selected";
        _gamePathToolTip.SetToolTip(_pathValue, selected ? path : null);
        if (!selected)
        {
            _gameVersion.Text = "Version: —";
            return;
        }
        try
        {
            var info = GameExecutableInfo.Read(path);
            _gameVersion.Text = "Version: " + (info?.Version ?? "Unknown");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Security.SecurityException)
        {
            _gameVersion.Text = "Version: Unavailable";
        }
    }

    private void LaunchGame()
    {
        try
        {
            var process = _gameLauncher.Start(_settings);
            process.Dispose();
            RefreshProcessStatus();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            MessageBox.Show(this, ex.Message, "Unable to launch DOOM Eternal", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void SaveSettings()
    {
        if (_loadingControls) return;
        try
        {
            _settingsStore.Save(_settings);
            if (_settings.CollisionViewerEnabled && File.Exists(_settings.GameExecutablePath))
                _layerRegistration.EnableForGame(_settings.GameExecutablePath);
            else
                _layerRegistration.Disable();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException or System.Security.SecurityException)
        {
            MessageBox.Show(this, "Settings could not be applied: " + ex.Message, "DEViL Settings", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
    }

    private int SelectedGameProcess()
    {
        int selected = 0;
        foreach (Process process in Process.GetProcessesByName("DOOMEternalx64vk"))
        {
            using (process)
            {
                try
                {
                    if (!string.Equals(process.MainModule?.FileName, _settings.GameExecutablePath, StringComparison.OrdinalIgnoreCase)) continue;
                    if (selected != 0) return 0;
                    selected = process.Id;
                }
                catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception) { }
            }
        }
        return selected;
    }

    internal void ReceiveLabCommand(int process, string[] args)
    {
        if (!IsHandleCreated || IsDisposed) return;
        BeginInvoke(() =>
        {
            if (IsDisposed || SelectedGameProcess() != process) return;
            if (!LabCommands.Apply(_settings, args, process, DateTime.UtcNow.Ticks)) return;
            _loadingControls = true;
            try { LoadSettingsIntoControls(); }
            finally { _loadingControls = false; }
            SaveSettings();
        });
    }

    internal void StopResearchOnExit()
    {
        if (!_settings.RecordResearch) return;
        _settings.RecordResearch = false;
        try { _settingsStore.Save(_settings); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
    }

    private void RefreshProcessStatus()
    {
        string? expectedViewer = null;
        try { expectedViewer = _layerRegistration.ExpectedRuntimeLibraryName; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Security.SecurityException) { }
        var snapshot = _processMonitor.ReadSnapshot(expectedViewer);
        SetStatus(_gameStatus, snapshot.GameRunning ? "Running" : "Not running", snapshot.GameRunning);
        SetStatus(_desruStatus, snapshot.DesruRunning ? "Running" : "Not running", snapshot.DesruRunning);
        bool viewerEnabled = _settings.CollisionViewerEnabled;
        bool restartRequired = viewerEnabled && snapshot.GameRunning &&
            (snapshot.ViewerUpdateRequired || (!snapshot.ViewerVersionChecked && snapshot.GameStartedBeforeViewer));
        string viewerStatus = !viewerEnabled ? "Disabled"
            : restartRequired ? (snapshot.ViewerUpdateRequired ? "Update pending" : "Restart game")
            : snapshot.GameRunning ? "Running" : "Enabled";
        SetStatus(_viewerStatus, viewerStatus, viewerEnabled, restartRequired);
        _gamePathToolTip.SetToolTip(_viewerStatus, snapshot.ViewerUpdateRequired
            ? $"The running game has not loaded this viewer build. Expected: {expectedViewer}. Loaded: {string.Join(", ", snapshot.LoadedViewerLibraries.DefaultIfEmpty("none"))}. Fully exit the game and launch it again through DEViL."
            : snapshot.ViewerVersionChecked ? $"Loaded viewer: {string.Join(", ", snapshot.LoadedViewerLibraries)}" : null);
        _launchButton.Enabled = !snapshot.GameRunning && !string.IsNullOrWhiteSpace(_settings.GameExecutablePath);
    }

    private void ShowHelp()
    {
        MessageBox.Show(this,
            "DEViL displays DOOM Eternal collision geometry through a Vulkan layer scoped to the selected game executable.\n\n" +
            "Select DOOMEternalx64vk.exe, enable Collision Viewer, choose the geometry and render style, then launch the game here. Settings are saved automatically.\n\n" +
            "Volume inspector: aim at a displayed trigger or invisible wall. The selected volume turns yellow. Name, class, ID and collision state are shown by the game's native debug HUD through meathook (mh_api_get required). Encounter labels use confirmed names from the supplied reference document.\n\n" +
            "Trigger types: expand a class to filter folders in instance names or entity definitions. The resource catalogue includes props, interact, lootdrop, ai and more; new paths are discovered live. Parent switches retain child choices. Path filtering needs compatible meathook.\n\n" +
            "Active encounters: open the live list and enable Show in-game HUD, or use lab_encounters_show 1. Use lab_encounters_show 0 to hide it and lab_encounters_list to print the list. lab_commands_list lists state cvars and action commands.\n\n" +
            "Entity links: all incoming/outgoing neighbors fit automatically. Wheel zooms, hold RMB to move the graph, LMB selects a node, and Fit graph restores the overall view. Enable the volume inspector and Follow crosshair to choose a volume in-game. Export JSON saves the capture. References do not prove script execution.\n\n" +
            "Movement lab: the collider pose follows rendered frames using cached geometry; attempts still record at about 20 Hz. Compare up to six named attempts and speed/height/support at a selected time. Surface projections shows XY/XZ/YZ tangent-plane cuts and angles, with Hold and an optional in-game HUD. Aim with the inspector to measure a volume boundary.\n\n" +
            "Level state: capture BEFORE, perform an action in the game, then capture AFTER. Select a volume change to show its captured outline, or highlight all. Save a pair as JSON or export changes as CSV. Unavailable reads are not deletions; loaded checkpoint names do not confirm a new save. Both captures must belong to the same loaded map instance. Keep DEViL open.\n\n" +
            "Сохранения: поставьте игру на паузу, нажмите «Сохранить», затем выберите запись и нажмите «Загрузить». Сохраняется последний автосейв игры. Загрузка без перезапуска поддерживается для 6.66 Rev 3, TAG1, слота 3 при выключенном Steam Cloud. Смешивание аккаунтов запрещено. Офлайн-архивы и диагностика: Дополнительно → Инструменты. Research points сохраняет наблюдения и заметки отдельно.\n\n" +
            "Запуск игры из DEVIL требует действующей блокировки исходящего трафика выбранного EXE в брандмауэре Windows. Включите блокировку через DESRU.\n\n" +
            "Research: record/stop the player's path, add notes and export the journal. The viewer layer loads on the next full game start through DEViL.",
            "DEViL Help", MessageBoxButtons.OK, MessageBoxIcon.Information);
    }

    private void OpenLink(string url)
    {
        try
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }
        catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            MessageBox.Show(this, ex.Message, "Unable to open link", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void UpdateViewerControls()
    {
        bool enabled = _viewerEnabled.Checked;
        _triggers.Enabled = _worldCollision.Enabled = _invisibleWalls.Enabled = enabled;
        _outlines.Enabled = _filled.Enabled = _distance.Enabled = _distanceInput.Enabled = enabled;
        _inspector.Enabled = enabled;
        _disabledVolumes.Enabled = _volumeHistory.Enabled = enabled;
        _triggerTypes.Enabled = enabled;
        RefreshProcessStatus();
    }

    private void BeginWindowDrag(object? sender, MouseEventArgs e)
    {
        if (e.Button != MouseButtons.Left) return;
        ReleaseCapture();
        SendMessage(Handle, 0x00A1, new IntPtr(2), IntPtr.Zero);
    }

    private static void SetStatus(Label label, string text, bool active, bool restartRequired = false)
    {
        label.Text = text;
        label.ForeColor = restartRequired ? Color.FromArgb(225, 70, 70) : active ? Color.FromArgb(122, 204, 126) : Color.FromArgb(185, 185, 185);
        label.Font = UiBoldFont(9.5f);
        label.Dock = DockStyle.Fill;
        label.TextAlign = ContentAlignment.MiddleRight;
        label.Margin = new Padding(2, 0, 2, 0);
    }

    private static Control StatusRow(string title, Label status)
    {
        var row = new TableLayoutPanel { ColumnCount = 2, AutoSize = false, Size = new Size(286, 38), BackColor = PanelColor, Margin = Padding.Empty };
        row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 58));
        row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 42));
        row.Controls.Add(new Label { Text = title, AutoSize = false, Dock = DockStyle.Fill, ForeColor = ForegroundColor, Font = UiBoldFont(9.5f), TextAlign = ContentAlignment.MiddleLeft, Margin = new Padding(2, 0, 0, 0) }, 0, 0);
        row.Controls.Add(status, 1, 0);
        return row;
    }

    private static Panel MakeCard()
    {
        var card = new Panel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Top, BackColor = PanelColor, Padding = new Padding(18), Margin = new Padding(0, 0, 0, 14), BorderStyle = BorderStyle.FixedSingle };
        return card;
    }

    private static Label SectionTitle(string text) => new() { Text = text, AutoSize = true, Dock = DockStyle.Top, ForeColor = ForegroundColor, Font = UiBoldFont(12f), Margin = new Padding(0, 0, 0, 12) };
    private static Label Subtitle(string text) => new() { Text = text, AutoSize = true, Dock = DockStyle.Top, ForeColor = Color.FromArgb(195, 195, 195), Font = UiBoldFont(9f), Margin = new Padding(0, 8, 0, 3) };

    private static void ConfigureCheck(CheckBox check, string text, float size)
    {
        check.Text = text;
        check.AutoSize = true;
        check.ForeColor = ForegroundColor;
        check.BackColor = PanelColor;
        check.Font = UiFont(size);
        check.Margin = new Padding(0, 1, 24, 2);
    }

    private static Button MakeButton(string text, int width, int height)
    {
        var button = new Button { Text = text };
        StyleButton(button, width, height, ControlColor);
        return button;
    }

    private static void StyleButton(Button button, int width, int height, Color backColor)
    {
        button.Size = new Size(width, height);
        button.BackColor = backColor;
        button.ForeColor = ForegroundColor;
        button.Font = UiBoldFont(9.5f);
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderColor = Color.FromArgb(95, 95, 95);
        button.FlatAppearance.MouseOverBackColor = Color.FromArgb(90, 90, 90);
        button.Cursor = Cursors.Hand;
        button.Margin = new Padding(8, 2, 0, 2);
    }

    private void LoadThemeFonts()
    {
        string directory = Path.Combine(AppContext.BaseDirectory, "Assets", "fonts");
        foreach (string file in new[] { "EternalUi2Regular.ttf", "EternalUi2Bold.ttf", "EternalLogo.ttf", "EternalBattleBold-DEViL.ttf" })
        {
            string path = Path.Combine(directory, file);
            if (File.Exists(path)) _fonts.AddFontFile(path);
        }
        foreach (FontFamily family in _fonts.Families)
        {
            if (family.Name.Equals("Eternal UI 2", StringComparison.OrdinalIgnoreCase)) _uiFamily = family;
            if (family.Name.Equals("Eternal Logo", StringComparison.OrdinalIgnoreCase)) _logoFamily = family;
            if (family.Name.Equals("Eternal Battle", StringComparison.OrdinalIgnoreCase)) _battleFamily = family;
        }
    }

    private static Font UiFont(float size) => new(_uiFamily, size + 0.75f, FontStyle.Regular, GraphicsUnit.Point);
    private static Font UiBoldFont(float size) => new(_uiFamily, size + 0.75f, FontStyle.Bold, GraphicsUnit.Point);
    private static Font LogoFont(float size) => new(_logoFamily, size + 0.75f, FontStyle.Bold, GraphicsUnit.Point);
    private static Font BattleFont(float size) => new(_battleFamily, size, FontStyle.Bold, GraphicsUnit.Point);

    private static Panel AddCard(Control parent, int x, int y, int width, int height)
    {
        var panel = new Panel
        {
            Location = new Point(x, y),
            Size = new Size(width, height),
            BackColor = PanelColor,
            BorderStyle = BorderStyle.FixedSingle
        };
        parent.Controls.Add(panel);
        return panel;
    }

    private static void AddSection(Control parent, string text, int x, int y, int width)
    {
        var label = new Label
        {
            Text = text,
            AutoSize = false,
            Location = new Point(x, y),
            Size = new Size(width, 22),
            Font = LogoFont(14f),
            ForeColor = ForegroundColor,
            BackColor = Canvas
        };
        parent.Controls.Add(label);
    }

    private static void Place(Control parent, Control child, int x, int y)
    {
        child.Location = new Point(x, y);
        parent.Controls.Add(child);
    }

    private static void ConfigureRadio(RadioButton radio, string text)
    {
        radio.Text = text;
        radio.AutoSize = true;
        radio.ForeColor = ForegroundColor;
        radio.BackColor = PanelColor;
        radio.Font = UiFont(10.5f);
        radio.Margin = Padding.Empty;
    }
}
