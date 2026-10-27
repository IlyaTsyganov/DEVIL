using System.Drawing.Drawing2D;
using Devil.Game;

namespace Devil;

internal sealed class EntityGraphCanvas : Control
{
    private sealed record Target(RectangleF Bounds, string Key, bool Selected);
    private const float NodeWidth = 220, NodeHeight = 42, RowGap = 10, ColumnGap = 72, HeaderHeight = 26;
    private EntityGraphSnapshot? _graph;
    private string? _selected;
    private string[] _incoming = [], _outgoing = [];
    private readonly List<Target> _targets = [];
    private readonly Button _fit = new() { Text = "Fit graph", Size = new Size(84, 26), FlatStyle = FlatStyle.Flat,
        BackColor = Color.FromArgb(55, 55, 55), ForeColor = Color.Gainsboro, UseVisualStyleBackColor = false };
    private readonly ToolTip _tooltip = new();
    private RectangleF _bounds;
    private float _zoom = 1;
    private PointF _offset;
    private bool _autoFit = true, _panning;
    private Point _dragPoint;
    private string? _hovered;
    private RectangleF Viewport => new(12, 36, Math.Max(1, ClientSize.Width - 24), Math.Max(1, ClientSize.Height - 66));
    public event Action<string>? NodeClicked;

    public EntityGraphCanvas()
    {
        DoubleBuffered = true; BackColor = Color.FromArgb(28, 28, 28); TabStop = true;
        SetStyle(ControlStyles.Selectable | ControlStyles.ResizeRedraw, true);
        AccessibleName = "Entity reference graph";
        AccessibleDescription = "Wheel to zoom, hold right mouse button to pan, left-click to select. Fit graph shows all neighbors.";
        Controls.Add(_fit); _fit.Click += (_, _) => FitGraph();
    }
    public void SetGraph(EntityGraphSnapshot? graph, string? selected)
    {
        bool newSelection = _selected != selected || _graph?.Process != graph?.Process
            || _graph?.Map != graph?.Map || _graph?.MapIdentity != graph?.MapIdentity;
        _graph = graph; _selected = selected;
        string[] incoming = Neighbors(true), outgoing = Neighbors(false);
        bool changed = newSelection || !_incoming.SequenceEqual(incoming) || !_outgoing.SequenceEqual(outgoing);
        _incoming = incoming; _outgoing = outgoing;
        if (changed)
        {
            _targets.Clear(); _tooltip.SetToolTip(this, null); _hovered = null;
            if (graph is not null && selected is not null && graph.Nodes.ContainsKey(selected))
            {
                float rows = Math.Max(1, Math.Max(incoming.Length, outgoing.Length));
                float height = rows * (NodeHeight + RowGap) - RowGap;
                _bounds = new(0, 0, 3 * NodeWidth + 2 * ColumnGap, HeaderHeight + height);
                float middle = HeaderHeight + height / 2;
                void Side(string[] keys, float x)
                {
                    float top = middle - (keys.Length * (NodeHeight + RowGap) - RowGap) / 2;
                    for (int i = 0; i < keys.Length; ++i)
                        _targets.Add(new(new(x, top + i * (NodeHeight + RowGap), NodeWidth, NodeHeight), keys[i], false));
                }
                Side(incoming, 0); Side(outgoing, 2 * (NodeWidth + ColumnGap));
                _targets.Add(new(new(NodeWidth + ColumnGap, middle - NodeHeight / 2, NodeWidth, NodeHeight), selected, true));
            }
            else _bounds = RectangleF.Empty;
        }
        if (newSelection) { _autoFit = true; StopPanning(); }
        if (_autoFit) UpdateFit();
        _fit.Enabled = _targets.Count > 0;
        Invalidate();
    }
    private string[] Neighbors(bool incoming) => _graph is null || _selected is null ? [] : _graph.Edges
        .Where(edge => incoming ? edge.To == _selected : edge.From == _selected)
        .Select(edge => incoming ? edge.From : edge.To).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
    public void FitGraph() { _autoFit = true; UpdateFit(); Invalidate(); }
    private float FitScale => _bounds.IsEmpty ? 1 : Math.Min(1, Math.Min(Viewport.Width / _bounds.Width, Viewport.Height / _bounds.Height));
    private void UpdateFit()
    {
        _zoom = FitScale;
        var view = Viewport;
        _offset = new(view.Left + (view.Width - _bounds.Width * _zoom) / 2, view.Top + (view.Height - _bounds.Height * _zoom) / 2);
    }
    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        if (_fit is null) return;
        _fit.Location = new(Math.Max(0, ClientSize.Width - _fit.Width - 8), 4);
        if (_autoFit) UpdateFit();
        Invalidate();
    }
    private PointF World(Point point) => new((point.X - _offset.X) / _zoom, (point.Y - _offset.Y) / _zoom);
    private Target? Hit(Point point)
    {
        if (!Viewport.Contains(point)) return null;
        var world = World(point);
        return _targets.LastOrDefault(target => target.Bounds.Contains(world));
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        var g = e.Graphics;
        TextRenderer.DrawText(g, "Wheel: zoom · RMB: drag · LMB: select", Font, new Rectangle(8, 5, Math.Max(1, ClientSize.Width - 108), 25), Color.Silver,
            TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
        if (_graph is null || _targets.Count == 0) return;
        string status = _incoming.Length + _outgoing.Length == 0 ? "No references found within the scanned fields."
            : $"{_incoming.Length} incoming · {_outgoing.Length} outgoing";
        TextRenderer.DrawText(g, $"Zoom: {_zoom * 100:0.#}% · {status}", Font,
            new Rectangle(8, ClientSize.Height - 25, Math.Max(1, ClientSize.Width - 16), 24), Color.Silver,
            TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
        var state = g.Save();
        try
        {
            g.SetClip(Viewport); g.TranslateTransform(_offset.X, _offset.Y); g.ScaleTransform(_zoom, _zoom);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using var arrow = new AdjustableArrowCap(4, 5);
            using var pen = new Pen(Color.FromArgb(70, 190, 210), 1.5f) { CustomEndCap = arrow };
            var center = _targets[^1].Bounds;
            foreach (var target in _targets.Where(target => !target.Selected))
            {
                var rect = target.Bounds; bool incoming = rect.Left < center.Left;
                g.DrawLine(pen, incoming ? rect.Right : center.Right, incoming ? rect.Top + rect.Height / 2 : center.Top + center.Height / 2,
                    incoming ? center.Left : rect.Left, incoming ? center.Top + center.Height / 2 : rect.Top + rect.Height / 2);
            }
            using var caption = new SolidBrush(Color.Silver);
            g.DrawString($"INCOMING ({_incoming.Length})", Font, caption, new PointF(0, 0));
            g.DrawString($"OUTGOING ({_outgoing.Length})", Font, caption, new PointF(2 * (NodeWidth + ColumnGap), 0));
            using var format = new StringFormat { LineAlignment = StringAlignment.Center, Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap };
            using var text = new SolidBrush(ForeColor);
            using var nodeFill = new SolidBrush(Color.FromArgb(42, 65, 74));
            using var focusFill = new SolidBrush(Color.FromArgb(104, 76, 25));
            using var nodeBorder = new Pen(Color.FromArgb(70, 190, 210));
            using var focusBorder = new Pen(Color.Gold);
            foreach (var target in _targets)
            {
                var rect = target.Bounds;
                // Cull offscreen boxes; retain world bounds for transformed hit testing.
                var screen = new RectangleF(_offset.X + rect.X * _zoom, _offset.Y + rect.Y * _zoom, rect.Width * _zoom, rect.Height * _zoom);
                if (!screen.IntersectsWith(Viewport)) continue;
                g.FillRectangle(target.Selected ? focusFill : nodeFill, rect);
                g.DrawRectangle(target.Selected ? focusBorder : nodeBorder, rect.X, rect.Y, rect.Width, rect.Height);
                g.DrawString(_graph.Nodes[target.Key].Display, Font, text, RectangleF.Inflate(rect, -6, -3), format);
            }
        }
        finally { g.Restore(state); }
    }
    protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); if (ContainsFocus || FindForm()?.ContainsFocus == true) Focus(); }
    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        if (_targets.Count == 0 || !Viewport.Contains(e.Location) || _panning) return;
        var anchor = World(e.Location);
        float zoom = Math.Clamp(_zoom * (float)Math.Pow(1.2, e.Delta / 120d), Math.Max(.00001f, FitScale / 4), 4);
        _zoom = zoom; _offset = new(e.X - anchor.X * zoom, e.Y - anchor.Y * zoom); _autoFit = false;
        if (e is HandledMouseEventArgs handled) handled.Handled = true;
        _tooltip.SetToolTip(this, null); _hovered = null; Invalidate();
    }
    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e); Focus();
        if (e.Button != MouseButtons.Right || _targets.Count == 0 || !Viewport.Contains(e.Location)) return;
        _panning = true; _autoFit = false; _dragPoint = e.Location; Capture = true; Cursor = Cursors.SizeAll;
        _tooltip.SetToolTip(this, null); _hovered = null;
    }
    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (_panning)
        {
            _offset = new(_offset.X + e.X - _dragPoint.X, _offset.Y + e.Y - _dragPoint.Y); _dragPoint = e.Location;
            Invalidate(); return;
        }
        var target = Hit(e.Location); Cursor = target is null ? Cursors.Default : Cursors.Hand;
        if (_hovered == target?.Key) return;
        _hovered = target?.Key;
        var node = target is not null ? _graph?.Nodes[target.Key] : null;
        _tooltip.SetToolTip(this, node is null ? null : $"{node.Display}\n{node.Class} · {node.Key}");
    }
    protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); if (e.Button == MouseButtons.Right) StopPanning(); }
    protected override void OnMouseCaptureChanged(EventArgs e) { base.OnMouseCaptureChanged(e); if (!Capture) { _panning = false; Cursor = Cursors.Default; } }
    private void StopPanning() { _panning = false; Capture = false; Cursor = Cursors.Default; }
    protected override void OnMouseClick(MouseEventArgs e)
    {
        base.OnMouseClick(e);
        if (e.Button == MouseButtons.Left && !_panning && Hit(e.Location) is { } target) NodeClicked?.Invoke(target.Key);
    }
    protected override void Dispose(bool disposing) { if (disposing) _tooltip.Dispose(); base.Dispose(disposing); }
}
