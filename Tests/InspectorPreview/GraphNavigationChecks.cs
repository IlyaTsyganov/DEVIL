using System.Collections;
using System.Drawing.Imaging;
using System.Reflection;
using Devil.Game;

internal static class GraphNavigationChecks
{
    private const BindingFlags Private = BindingFlags.NonPublic | BindingFlags.Instance;
    private static object Field(Control canvas, string name) => canvas.GetType().GetField(name, Private)!.GetValue(canvas)!;
    private static float Zoom(Control canvas) => (float)Field(canvas, "_zoom");
    private static PointF Offset(Control canvas) => (PointF)Field(canvas, "_offset");
    private static IEnumerable<(RectangleF Bounds, string Key)> Targets(Control canvas) => ((IEnumerable)Field(canvas, "_targets")).Cast<object>()
        .Select(target => ((RectangleF)target.GetType().GetProperty("Bounds")!.GetValue(target)!, (string)target.GetType().GetProperty("Key")!.GetValue(target)!));
    private static PointF World(Control canvas, Point screen) => new((screen.X - Offset(canvas).X) / Zoom(canvas), (screen.Y - Offset(canvas).Y) / Zoom(canvas));
    internal static Point NodePoint(Control canvas, string key)
    {
        var box = Targets(canvas).First(target => target.Key == key).Bounds;
        return Point.Round(new PointF(Offset(canvas).X + (box.X + box.Width / 2) * Zoom(canvas), Offset(canvas).Y + (box.Y + box.Height / 2) * Zoom(canvas)));
    }
    internal static void Mouse(Control canvas, string method, MouseButtons button, Point point, int delta = 0) => canvas.GetType().GetMethod(method, Private)!
        .Invoke(canvas, [new MouseEventArgs(button, 1, point.X, point.Y, delta)]);
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static bool Near(float a, float b) => Math.Abs(a - b) < .01f;
    private static void AllFit(Control canvas)
    {
        var view = (RectangleF)canvas.GetType().GetProperty("Viewport", Private)!.GetValue(canvas)!;
        foreach (var target in Targets(canvas))
        {
            var box = target.Bounds; var offset = Offset(canvas); float zoom = Zoom(canvas);
            Check(box.Left * zoom + offset.X >= view.Left - .01f && box.Right * zoom + offset.X <= view.Right + .01f
                && box.Top * zoom + offset.Y >= view.Top - .01f && box.Bottom * zoom + offset.Y <= view.Bottom + .01f,
                "Auto fit clipped a graph neighbor");
        }
    }
    internal static void Run(string previewDirectory, string? capturePath = null)
    {
        var type = Assembly.Load("DEViL").GetType("Devil.EntityGraphCanvas", true)!;
        using var canvas = (Control)Activator.CreateInstance(type)!;
        using var host = new Form { ClientSize = new Size(820, 310), Opacity = 0, ShowInTaskbar = false };
        canvas.Dock = DockStyle.Fill; canvas.Font = new Font("Segoe UI", 10f); canvas.ForeColor = Color.Gainsboro;
        host.Controls.Add(canvas); host.Show(); Application.DoEvents();
        const string root = "100:3";
        var nodes = new Dictionary<string, EntityGraphNode> { [root] = new(root, "arena_manager", "idEncounterManager", "") };
        var edges = new List<EntityGraphEdge>();
        for (int i = 0; i < 28; ++i)
        {
            string key = (200 + i) + ":3";
            nodes[key] = new(key, (i < 11 ? "incoming_trigger_" : "outgoing_target_") + (i < 11 ? i + 1 : i - 10), "idTrigger", "");
            edges.Add(new(i < 11 ? key : root, i < 11 ? root : key, "target", "Entity pointer"));
        }
        edges.Add(edges[0] with { Field = "anotherField" });
        var snapshot = new EntityGraphSnapshot(1234, DateTimeOffset.UtcNow, "live", "test/map", "1000:2000", 0, 0, 0, root, nodes, edges);
        void Set(EntityGraphSnapshot? graph, string? key) => type.GetMethod("SetGraph")!.Invoke(canvas, [graph, key]);
        void Fit() => canvas.Controls.OfType<Button>().Single(button => button.Text == "Fit graph").PerformClick();
        Set(snapshot, root);
        Check(Targets(canvas).Count() == 29 && Targets(canvas).Select(target => target.Key).Distinct().Count() == 29,
            "Graph dropped neighbors beyond four or duplicated a neighbor with multiple fields");
        Check(Zoom(canvas) < 1, "Dense graph was not reduced to fit the field"); AllFit(canvas);
        using (var bitmap = new Bitmap(canvas.Width, canvas.Height))
        {
            canvas.DrawToBitmap(bitmap, canvas.ClientRectangle);
            bitmap.Save(Path.Combine(previewDirectory, "entity-graph-auto-fit.png"), ImageFormat.Png);
        }
        string? clicked = null;
        type.GetEvent("NodeClicked")!.AddEventHandler(canvas, (Action<string>)(key => clicked = key));
        var last = NodePoint(canvas, "227:3"); Mouse(canvas, "OnMouseClick", MouseButtons.Left, last);
        Check(clicked == "227:3", "Auto-fit hit testing missed the last outgoing node");
        clicked = null; Mouse(canvas, "OnMouseClick", MouseButtons.Right, last);
        Check(clicked is null, "RMB must not select a vertex");
        var anchor = NodePoint(canvas, root); var world = World(canvas, anchor); float before = Zoom(canvas);
        Mouse(canvas, "OnMouseWheel", MouseButtons.None, anchor, 240);
        Check(Zoom(canvas) > before && Near(World(canvas, anchor).X, world.X) && Near(World(canvas, anchor).Y, world.Y),
            "Wheel zoom did not keep the point under the cursor stationary");
        before = Zoom(canvas); Mouse(canvas, "OnMouseWheel", MouseButtons.None, anchor, -120);
        Check(Zoom(canvas) < before, "Reverse wheel did not zoom out");
        // Duplicate snapshots must not reset a view that the user has moved/zoomed.
        before = Zoom(canvas); var offset = Offset(canvas);
        Set(snapshot with { Captured = snapshot.Captured.AddSeconds(1) }, root);
        Check(Zoom(canvas) == before && Offset(canvas) == offset, "Periodic refresh reset manual zoom or position");
        Mouse(canvas, "OnMouseDown", MouseButtons.Right, anchor);
        Check(canvas.Capture, "RMB drag did not capture the pointer");
        Mouse(canvas, "OnMouseMove", MouseButtons.Right, new(anchor.X + 90, anchor.Y - 18));
        Check(Near(Offset(canvas).X, offset.X + 90) && Near(Offset(canvas).Y, offset.Y - 18), "RMB drag did not move the graph by the pointer delta");
        Mouse(canvas, "OnMouseUp", MouseButtons.Right, anchor);
        Check(!canvas.Capture && clicked is null, "RMB release did not release capture or changed selection");
        Mouse(canvas, "OnMouseClick", MouseButtons.Left, NodePoint(canvas, root));
        Check(clicked == root, "LMB hit testing failed after combined pan and zoom");
        clicked = null; Mouse(canvas, "OnMouseClick", MouseButtons.Left, new(1, 1));
        Check(clicked is null, "A click outside the graph viewport selected a node");
        offset = Offset(canvas); Set(snapshot, root);
        Check(Offset(canvas) == offset && Zoom(canvas) == before, "Periodic refresh reset manual panning");
        Fit(); AllFit(canvas);
        before = Zoom(canvas); host.ClientSize = new(940, 500); Application.DoEvents();
        Check(Zoom(canvas) > before, "Auto-fit did not respond to a larger viewport"); AllFit(canvas);
        anchor = NodePoint(canvas, root); Mouse(canvas, "OnMouseWheel", MouseButtons.None, anchor, 360);
        using (var bitmap = new Bitmap(canvas.Width, canvas.Height))
        {
            canvas.DrawToBitmap(bitmap, canvas.ClientRectangle);
            bitmap.Save(Path.Combine(previewDirectory, "entity-graph-zoom.png"), ImageFormat.Png);
        }
        Set(snapshot with { MapIdentity = "3000:4000" }, root); AllFit(canvas);
        Check((bool)Field(canvas, "_autoFit"), "Changing map identity did not reset the view");
        Set(snapshot, "227:3"); Check(Targets(canvas).Count() == 2, "Selecting a different root retained the old neighborhood"); AllFit(canvas);
        Set(null, null); Check(!Targets(canvas).Any(), "Missing graph retained stale hit targets");
        if (capturePath is not null)
        {
            var captured = EntityGraphSnapshot.Parse(File.ReadAllLines(capturePath)) ?? throw new Exception("Captured graph could not be parsed");
            string busiest = captured.Edges.SelectMany(edge => new[] { edge.From, edge.To }).GroupBy(key => key)
                .OrderByDescending(group => group.Count()).First().Key;
            int incoming = captured.Edges.Where(edge => edge.To == busiest).Select(edge => edge.From).Distinct().Count();
            int outgoing = captured.Edges.Where(edge => edge.From == busiest).Select(edge => edge.To).Distinct().Count();
            Set(captured, busiest); Check(Targets(canvas).Count() == 1 + incoming + outgoing, "Captured graph lost displayed neighbors"); AllFit(canvas);
            using var bitmap = new Bitmap(canvas.Width, canvas.Height);
            canvas.DrawToBitmap(bitmap, canvas.ClientRectangle);
            bitmap.Save(Path.Combine(previewDirectory, "entity-graph-captured-fit.png"), ImageFormat.Png);
            Console.WriteLine($"PASS: captured graph ({captured.Nodes.Count} entities / {captured.Edges.Count} references), busy root fits {incoming} incoming / {outgoing} outgoing neighbors.");
        }
        host.Close();
    }
}
