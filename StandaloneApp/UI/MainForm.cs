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
    private readonly CheckBox _viewerEnabled = new();
    private readonly CheckBox _triggers = new();
    private readonly CheckBox _worldCollision = new();
    private readonly CheckBox _invisibleWalls = new();
    private readonly RadioButton _outlines = new();
    private readonly RadioButton _filled = new();
    private readonly TrackBar _distance = new();
    private readonly NumericUpDown _distanceInput = new();
    private readonly Button _launchButton = new();
    private bool _syncingDistance;

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
        ClientSize = new Size(651, 513);
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
        FormClosed += (_, _) => { _refreshTimer.Stop(); _refreshTimer.Dispose(); _fonts.Dispose(); };
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
            Size = new Size(644, 450),
            Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
            AutoScroll = true,
            BackColor = Canvas
        };
        Controls.Add(scroll);
        var content = new Panel { Location = Point.Empty, Size = new Size(640, 448), BackColor = Canvas };
        scroll.Controls.Add(content);

        AddSection(content, "COLLISION VIEWER", 5, 14, 310);
        var viewer = AddCard(content, 5, 38, 310, 140);
        ConfigureCheck(_viewerEnabled, "Enable Collision Viewer", 11.25f);
        Place(viewer, _viewerEnabled, 8, 8);
        var caption = new Label { Text = "Collision types", AutoSize = true, ForeColor = ForegroundColor, Font = UiFont(10f), Location = new Point(8, 36) };
        viewer.Controls.Add(caption);
        ConfigureCheck(_triggers, "Triggers", 10.5f);
        ConfigureCheck(_worldCollision, "World collision", 10.5f);
        ConfigureCheck(_invisibleWalls, "Invisible walls", 10.5f);
        Place(viewer, _triggers, 20, 58);
        Place(viewer, _worldCollision, 20, 82);
        Place(viewer, _invisibleWalls, 20, 106);
        AddSection(content, "INFO PANEL", 324, 14, 310);
        var info = AddCard(content, 324, 38, 310, 140);
        Place(info, StatusRow("DOOM Eternal", _gameStatus), 8, 6);
        Place(info, StatusRow("DESRU", _desruStatus), 8, 50);
        Place(info, StatusRow("Collision Viewer", _viewerStatus), 8, 94);

        AddSection(content, "RENDER SETTINGS", 5, 192, 310);
        var render = AddCard(content, 5, 216, 310, 178);
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
        render.Controls.Add(new Label
        {
            Text = "Greater view distance may reduce game performance.",
            AutoSize = false,
            Size = new Size(280, 34),
            ForeColor = Color.Gainsboro,
            Font = UiFont(9f),
            Location = new Point(8, 134)
        });

        AddSection(content, "GAME", 324, 192, 310);
        var game = AddCard(content, 324, 216, 310, 178);
        _pathValue.AutoSize = false;
        _pathValue.Size = new Size(286, 36);
        _pathValue.MaximumSize = new Size(286, 36);
        _pathValue.ForeColor = ForegroundColor;
        _pathValue.Font = UiFont(9f);
        _pathValue.Location = new Point(8, 8);
        game.Controls.Add(_pathValue);
        var browse = MakeButton("BROWSE…", 128, 30);
        browse.Location = new Point(8, 53);
        browse.Margin = Padding.Empty;
        browse.Click += (_, _) => BrowseForGame();
        game.Controls.Add(browse);
        _launchButton.Text = "LAUNCH DOOM ETERNAL";
        StyleButton(_launchButton, 284, 32, ControlColor);
        _launchButton.Location = new Point(8, 96);
        _launchButton.Margin = Padding.Empty;
        _launchButton.Click += (_, _) => LaunchGame();
        game.Controls.Add(_launchButton);

        var downloadDesru = MakeButton("Download DESRU", 152, 30);
        downloadDesru.Location = new Point(5, 408);
        downloadDesru.Margin = Padding.Empty;
        downloadDesru.Click += (_, _) => OpenLink("https://github.com/bowsr/DESRU/releases/latest");
        content.Controls.Add(downloadDesru);
        var discord = MakeButton("MDSR Discord", 152, 30);
        discord.Location = new Point(164, 408);
        discord.Margin = Padding.Empty;
        discord.Click += (_, _) => OpenLink("https://discord.gg/dtDa9VZ");
        content.Controls.Add(discord);
        var help = MakeButton("Help", 152, 30);
        help.Location = new Point(323, 408);
        help.Margin = Padding.Empty;
        help.Click += (_, _) => ShowHelp();
        content.Controls.Add(help);
        var exit = MakeButton("Exit", 152, 30);
        exit.Location = new Point(482, 408);
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
        _outlines.Checked = _settings.Style == RenderStyle.Outlines;
        _filled.Checked = _settings.Style == RenderStyle.Filled;
        int distance = Math.Clamp(_settings.ViewDistanceMeters, _distance.Minimum, _distance.Maximum);
        _distance.Value = distance;
        _distanceInput.Value = distance;
        _pathValue.Text = string.IsNullOrWhiteSpace(_settings.GameExecutablePath) ? "No game executable selected" : _settings.GameExecutablePath;
        UpdateViewerControls();
    }

    private void WireSettings()
    {
        _viewerEnabled.CheckedChanged += (_, _) => { _settings.CollisionViewerEnabled = _viewerEnabled.Checked; UpdateViewerControls(); SaveSettings(); };
        _triggers.CheckedChanged += (_, _) => { _settings.ShowTriggers = _triggers.Checked; SaveSettings(); };
        _worldCollision.CheckedChanged += (_, _) => { _settings.ShowWorldCollision = _worldCollision.Checked; SaveSettings(); };
        _invisibleWalls.CheckedChanged += (_, _) => { _settings.ShowInvisibleWalls = _invisibleWalls.Checked; SaveSettings(); };
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
        _pathValue.Text = dialog.FileName;
        SaveSettings();
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

    private void RefreshProcessStatus()
    {
        var snapshot = _processMonitor.ReadSnapshot();
        SetStatus(_gameStatus, snapshot.GameRunning ? "Running" : "Not running", snapshot.GameRunning);
        SetStatus(_desruStatus, snapshot.DesruRunning ? "Running" : "Not running", snapshot.DesruRunning);
        SetStatus(_viewerStatus, _settings.CollisionViewerEnabled ? "Restart game" : "Disabled", _settings.CollisionViewerEnabled, _settings.CollisionViewerEnabled);
        _launchButton.Enabled = !snapshot.GameRunning && !string.IsNullOrWhiteSpace(_settings.GameExecutablePath);
    }

    private void ShowHelp()
    {
        MessageBox.Show(this,
            "DEViL displays DOOM Eternal collision geometry through a Vulkan layer scoped to the selected game executable.\n\n" +
            "Select DOOMEternalx64vk.exe, enable Collision Viewer, choose the geometry and render style, then launch the game here. Settings are saved automatically.\n\n" +
            "DEViL only reads process status. It does not close or modify DOOM Eternal or DESRU. The viewer is loaded on the next game start.",
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
