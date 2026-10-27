using System.Drawing.Drawing2D;
using Devil.Game;

namespace Devil;

internal sealed class MotionPlot : Control
{
    private IReadOnlyList<MotionAttempt> _attempts = [];
    private MotionAttempt? _reference;
    private long _time;
    private int _projection;
    private bool _align;
    internal static Color AttemptColor(MotionAttempt attempt, int index) => new[] { Color.FromArgb(64, 255, 115), Color.FromArgb(255, 77, 102), Color.FromArgb(64, 179, 255), Color.FromArgb(255, 204, 51), Color.FromArgb(204, 115, 255), Color.FromArgb(51, 255, 255) }[MotionStore.ColorIndex(attempt, index)];
    public MotionPlot() { DoubleBuffered = true; BackColor = Color.FromArgb(28, 28, 28); SetStyle(ControlStyles.ResizeRedraw, true); }
    public void Set(IReadOnlyList<MotionAttempt> attempts, MotionAttempt? reference, long time, int projection, bool align)
    { _attempts = attempts; _reference = reference; _time = time; _projection = projection; _align = align; Invalidate(); }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e); var g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias;
        if (_attempts.Count == 0 || _attempts.All(attempt => attempt.Samples.Count == 0))
        { TextRenderer.DrawText(g, "Record attempts, then check them to compare routes.", Font, ClientRectangle, ForeColor, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter); return; }
        PointF Project(MotionVector v) => _projection switch { 1 => new(v.X, v.Z), 2 => new(v.Y, v.Z), _ => new(v.X, v.Y) };
        PointF Point(MotionAttempt attempt, MotionVector v)
        {
            var point = Project(v);
            if (_align && attempt.Samples.Count > 0 && _reference is { Samples.Count: > 0 })
            { var origin = Project(attempt.Samples[0].Position); var reference = Project(_reference.Samples[0].Position); point = new(point.X - origin.X + reference.X, point.Y - origin.Y + reference.Y); }
            return point;
        }
        var points = _attempts.SelectMany(attempt => attempt.Samples.Select(sample => Point(attempt, sample.Position))).ToArray();
        float minX = points.Min(p => p.X), maxX = points.Max(p => p.X), minY = points.Min(p => p.Y), maxY = points.Max(p => p.Y);
        float scale = Math.Min(Math.Max(1, Width - 70) / Math.Max(.5f, maxX - minX), Math.Max(1, Height - 75) / Math.Max(.5f, maxY - minY));
        PointF Screen(PointF p) => new(Width / 2f + (p.X - (minX + maxX) / 2) * scale, Height / 2f - (p.Y - (minY + maxY) / 2) * scale);
        TextRenderer.DrawText(g, $"{new[] { "XY", "XZ", "YZ" }[Math.Clamp(_projection, 0, 2)]} · {(_align ? "Starts aligned for plot only" : "World coordinates")} · Reference: {_reference?.Name}", Font, new Rectangle(10, 5, Math.Max(1, Width - 20), 30), ForeColor, TextFormatFlags.EndEllipsis);
        var state = g.Save(); g.SetClip(new Rectangle(8, 35, Math.Max(1, Width - 16), Math.Max(1, Height - 65)));
        for (int index = 0; index < _attempts.Count; ++index)
        {
            var attempt = _attempts[index]; using var pen = new Pen(AttemptColor(attempt, index), attempt == _reference ? 2.2f : 1.5f);
            int stride = Math.Max(1, attempt.Samples.Count / 6000);
            for (int i = stride; i < attempt.Samples.Count; i += stride)
            {
                var a = attempt.Samples[i - stride]; var b = attempt.Samples[i];
                if (a.Segment != b.Segment || b.Epoch - a.Epoch > 250 * stride || MotionVector.Distance(a.Position, b.Position) > 50) continue;
                g.DrawLine(pen, Screen(Point(attempt, a.Position)), Screen(Point(attempt, b.Position)));
            }
            if (attempt.Samples.Count > 0)
            { var start = Screen(Point(attempt, attempt.Samples[0].Position)); g.DrawRectangle(pen, start.X - 3, start.Y - 3, 6, 6); }
            if (attempt.At(_time) is { } current)
            {
                var p = Screen(Point(attempt, current.Position)); using var fill = new SolidBrush(AttemptColor(attempt, index)); g.FillEllipse(fill, p.X - 5, p.Y - 5, 10, 10);
                if (current.Contact)
                {
                    var contact = Point(attempt, current.ContactPoint); var end = Point(attempt, new(current.ContactPoint.X + current.Normal.X, current.ContactPoint.Y + current.Normal.Y, current.ContactPoint.Z + current.Normal.Z));
                    g.DrawLine(pen, Screen(contact), Screen(end));
                }
            }
        }
        g.Restore(state);
        TextRenderer.DrawText(g, $"{maxX - minX:0.00} × {maxY - minY:0.00} game units · squares: starts · dots: samples at selected time", Font,
            new Rectangle(10, Height - 27, Math.Max(1, Width - 20), 24), Color.Silver, TextFormatFlags.EndEllipsis);
    }
}
