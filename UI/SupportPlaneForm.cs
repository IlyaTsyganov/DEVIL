using System.Drawing.Drawing2D;
using Devil.Configuration;
using Devil.Game;

namespace Devil;

internal sealed class SupportPlaneForm : Form
{
    private readonly ComboBox _source = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 215 };
    private readonly CheckBox _hold = new() { Text = "Hold this sample", AutoSize = true };
    private readonly CheckBox _hud = new() { Text = "Show angles in game HUD", AutoSize = true };
    private readonly Label _status = new() { Dock = DockStyle.Fill, UseMnemonic = false };
    private readonly PlaneProjection[] _views = [new("XY"), new("XZ"), new("YZ")];
    private readonly DevilSettings _settings;
    private readonly Action _save;
    private MotionSample? _live, _recorded, _held;
    private string _liveStatus = "Waiting for live support", _recordedStatus = "Select an attempt and time", _heldStatus = "";
    private bool _loading;
    public SupportPlaneForm(DevilSettings settings, Action save)
    {
        _settings = settings; _save = save;
        Text = "DEViL — Support plane projections & angles"; ClientSize = new Size(1100, 480); MinimumSize = new Size(960, 440);
        StartPosition = FormStartPosition.CenterParent; ShowInTaskbar = false; AutoScaleMode = AutoScaleMode.Font;
        BackColor = Color.FromArgb(35, 35, 35); ForeColor = Color.Gainsboro; Font = new Font("Segoe UI", 10f);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), RowCount = 4, ColumnCount = 1 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 36)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 70));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 120)); Controls.Add(layout);
        var controls = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
        _source.Items.AddRange(["Live / last gameplay sample", "Selected attempt time"]); _source.SelectedIndex = 0;
        _hud.Checked = settings.ShowSupportHud; controls.Controls.AddRange([_source, _hold, _hud]); layout.Controls.Add(controls, 0, 0);
        layout.Controls.Add(_status, 0, 1);
        var projections = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 1, Margin = Padding.Empty };
        for (int i = 0; i < 3; ++i) { projections.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100 / 3f)); _views[i].Dock = DockStyle.Fill; projections.Controls.Add(_views[i], i, 0); }
        layout.Controls.Add(projections, 0, 2);
        layout.Controls.Add(new Label { Dock = DockStyle.Fill, Font = new Font(Font.FontFamily, 9f), Text = "Orange: contact-plane cuts through XY / XZ / YZ. Signed angles start from the first positive axis; cyan is the projected unit normal. A flat floor has no unique XY cut line.\nThe game's ground trace supplies one contact plane. At an edge this does not determine the edge direction or the angle between adjoining faces.\nKeep DEViL running for the in-game HUD. It is hidden in menus/console; the compact angle block appears to the right of the volume inspector." }, 0, 3);
        _source.SelectedIndexChanged += (_, _) => { _hold.Checked = false; DrawSample(); };
        _hold.CheckedChanged += (_, _) =>
        {
            if (_hold.Checked) { _held = _source.SelectedIndex == 0 ? _live : _recorded; _heldStatus = _source.SelectedIndex == 0 ? "Gameplay sample frozen when Hold was enabled" : _recordedStatus; }
            DrawSample();
        };
        _hud.CheckedChanged += (_, _) => { if (!_loading) { _settings.ShowSupportHud = _hud.Checked; _save(); } };
        DrawSample();
    }
    public void SetSources(MotionSample? live, string liveStatus, MotionSample? recorded, string recordedStatus)
    {
        _live = live; _liveStatus = liveStatus; _recorded = recorded; _recordedStatus = recordedStatus;
        _loading = true; _hud.Checked = _settings.ShowSupportHud; _loading = false; DrawSample();
    }
    private void DrawSample()
    {
        var sample = _hold.Checked ? _held : _source.SelectedIndex == 0 ? _live : _recorded;
        string source = _hold.Checked ? "HELD · " + _heldStatus : _source.SelectedIndex == 0 ? _liveStatus : _recordedStatus;
        var plane = SupportPlane.From(sample);
        string Angle(float? value) => value is { } number ? $"{number:+0.00;-0.00;0.00}°" : "unavailable";
        _status.Text = plane is null ? source + "\nNo support plane at this sample. A tangent angle is unavailable."
            : $"{source}\nNormal: ({plane.Normal.X:+0.000;-0.000;0.000}, {plane.Normal.Y:+0.000;-0.000;0.000}, {plane.Normal.Z:+0.000;-0.000;0.000}) · Surface slope against gravity: {Angle(plane.Slope)}\nContact: ({sample!.ContactPoint.X:0.000}, {sample.ContactPoint.Y:0.000}, {sample.ContactPoint.Z:0.000})";
        for (int i = 0; i < _views.Length; ++i) _views[i].SetCut(plane?.Cuts[i]);
    }
}
internal sealed class PlaneProjection(string name) : Control
{
    private SupportPlaneCut? _cut;
    protected override void OnCreateControl() { DoubleBuffered = true; SetStyle(ControlStyles.ResizeRedraw, true); base.OnCreateControl(); }
    public void SetCut(SupportPlaneCut? cut) { _cut = cut; Invalidate(); }
    protected override void OnPaintBackground(PaintEventArgs e) => e.Graphics.Clear(Color.FromArgb(28, 28, 28));
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e); var g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias;
        TextRenderer.DrawText(g, name, Font, new Point(10, 6), ForeColor);
        var center = new PointF(Width / 2f, Math.Max(72, (Height - 76) / 2f + 25));
        float scale = Math.Max(20, Math.Min(Width / 2f - 34, (Height - 105) / 2f));
        using var axes = new Pen(Color.FromArgb(92, 92, 92));
        g.DrawLine(axes, center.X - scale, center.Y, center.X + scale, center.Y); g.DrawLine(axes, center.X, center.Y - scale, center.X, center.Y + scale);
        TextRenderer.DrawText(g, "+" + name[0], Font, new Point((int)(center.X + scale), (int)center.Y), Color.Silver);
        TextRenderer.DrawText(g, "+" + name[1], Font, new Point((int)center.X + 3, (int)(center.Y - scale - 20)), Color.Silver);
        if (_cut is { } cut)
        {
            if (cut.TangentAngle is { } angle)
            {
                float radians = angle * MathF.PI / 180; var offset = new PointF(MathF.Cos(radians) * scale, -MathF.Sin(radians) * scale);
                using var cap = new AdjustableArrowCap(4, 5); using var tangent = new Pen(Color.Orange, 2) { CustomEndCap = cap };
                g.DrawLine(tangent, center.X - offset.X, center.Y - offset.Y, center.X + offset.X, center.Y + offset.Y);
            }
            else { using var hatch = new HatchBrush(HatchStyle.ForwardDiagonal, Color.FromArgb(70, Color.Orange), Color.Transparent); g.FillRectangle(hatch, center.X - scale, center.Y - scale, scale * 2, scale * 2); }
            if (cut.Magnitude > 1e-5f)
            {
                using var cap = new AdjustableArrowCap(4, 5); using var normal = new Pen(Color.Cyan, 2) { CustomEndCap = cap };
                g.DrawLine(normal, center.X, center.Y, center.X + cut.NormalA * scale, center.Y - cut.NormalB * scale);
            }
            using var point = new SolidBrush(Color.White); g.FillEllipse(point, center.X - 3, center.Y - 3, 6, 6);
            string description = cut.TangentAngle is { } value ? $"Cut tangent: {value:+0.00;-0.00;0.00}° from +{name[0]}\nProjected normal: {cut.NormalAngle:+0.00;-0.00;0.00}°" : "Coincident plane\nNo unique tangent line";
            TextRenderer.DrawText(g, description, Font, new Rectangle(8, Height - 62, Width - 16, 60), Color.Gainsboro, TextFormatFlags.WordBreak);
        }
        else TextRenderer.DrawText(g, "No support sample", Font, new Rectangle(8, Height - 55, Width - 16, 45), Color.Silver);
    }
}
