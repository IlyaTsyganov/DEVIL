using System.Drawing.Imaging;
using System.Reflection;
using Devil.Configuration;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
        try { Run(args); return 0; }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
    private static void Run(string[] args)
    {
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        var type = Assembly.Load("DEViL").GetType("Devil.MainForm", true)!;
        using var form = (Form)Activator.CreateInstance(type)!;
        form.Opacity = 0;
        form.ShowInTaskbar = false;
        form.Show();
        Application.DoEvents();
        IEnumerable<Control> Descendants(Control parent)
        {
            foreach (Control child in parent.Controls)
            {
                yield return child;
                foreach (Control nested in Descendants(child)) yield return nested;
            }
        }
        var legend = Descendants(form).OfType<Label>().Single(label => label.Text.StartsWith("Disabled: pale outline"));
        if (legend.GetPreferredSize(new Size(legend.Width, int.MaxValue)).Height > legend.Height)
            throw new Exception("Volume state legend is clipped");
        foreach (string fieldName in new[] { "_inspector", "_disabledVolumes", "_volumeHistory", "_triggerTypes" })
        {
        var inspector = (Control)type.GetField(fieldName, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(form)!;
        if (inspector.Bottom >= inspector.Parent!.ClientSize.Height)
            throw new Exception("Inspector checkbox exceeds its card");
        foreach (Control sibling in inspector.Parent.Controls)
        {
            if (sibling != inspector && sibling.Visible && inspector.Bounds.IntersectsWith(sibling.Bounds))
                throw new Exception("Inspector checkbox overlaps another control: " + sibling.Text);
        }
        }
        var movementButton = Descendants(form).OfType<Button>().Single(button => button.Text == "Movement lab…");
        if (movementButton.Parent!.Controls.Cast<Control>().Any(other => other != movementButton && other.Visible && movementButton.Bounds.IntersectsWith(other.Bounds)))
            throw new Exception("Movement lab entry overlaps another main-window control");
        var stateButton = Descendants(form).OfType<Button>().Single(button => button.Text == "Level state: before / after…");
        if (stateButton.Parent!.Controls.Cast<Control>().Any(other => other != stateButton && other.Visible && stateButton.Bounds.IntersectsWith(other.Bounds)))
            throw new Exception("Level-state entry overlaps another main-window control");
        var pointsButton = Descendants(form).OfType<Button>().Single(button => button.Text == "Research points…");
        if (pointsButton.Parent!.Controls.Cast<Control>().Any(other => other != pointsButton && other.Visible && pointsButton.Bounds.IntersectsWith(other.Bounds)))
            throw new Exception("Research-points entry overlaps another main-window control");
        var savesButton = Descendants(form).OfType<Button>().Single(button => button.Text == "Сохранения…");
        if (savesButton.Parent!.Controls.Cast<Control>().Any(other => other != savesButton && other.Visible && savesButton.Bounds.IntersectsWith(other.Bounds)))
            throw new Exception("Saves entry overlaps another main-window control");
        using var bitmap = new Bitmap(form.Width, form.Height);
        form.DrawToBitmap(bitmap, new Rectangle(0, 0, bitmap.Width, bitmap.Height));
        bitmap.Save(args[0], ImageFormat.Png);
        string isolated = Path.Combine(Path.GetTempPath(), "DEViL-TriggerUi-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(isolated);
        try
        {
            string discovery = Path.Combine(isolated, "trigger-classes.txt");
            File.WriteAllText(discovery, "idTrigger_NewClass\ninvalid\nidTrigger_NewClass\n");
            var settings = new DevilSettings();
            settings.SetTriggerClassVisible("idEncounterTrigger_Commit", false);
            settings.SetTriggerPathVisible("idTrigger", "trigger/props/", false);
            File.WriteAllText(Path.Combine(isolated, "trigger-paths.tsv"), "#DEVIL_TRIGGER_PATHS_V1\nidTrigger\ttrigger/props/\nidTrigger\ttrigger/props/small/\nidTrigger\ttrigger/interact/\nidTrigger\t@no_path\nidTrigger_Teleporter\ttrigger/props/\ninvalid\tbad/\n");
            int writes = 0;
            var filterType = Assembly.Load("DEViL").GetType("Devil.TriggerTypesForm", true)!;
            using var filter = (Form)Activator.CreateInstance(filterType, settings, discovery, (Action)(() => ++writes))!;
            filter.Opacity = 0; filter.Show(); Application.DoEvents();
            var list = Descendants(filter).OfType<TreeView>().Single();
            var search = Descendants(filter).OfType<TextBox>().Single();
            IEnumerable<TreeNode> Nodes(TreeNodeCollection nodes)
            {
                foreach (TreeNode node in nodes) { yield return node; foreach (var child in Nodes(node.Nodes)) yield return child; }
            }
            TreeNode Root(string name) => list.Nodes.Cast<TreeNode>().Single(node => node.Text.StartsWith(name + " —"));
            TreeNode PathNode(string typeName, string path) => Nodes(list.Nodes).Single(node => node.ToolTipText == typeName + " · " + path);
            if (list.Nodes.Count != TriggerClassCatalog.Known.Count + 1 || writes != 0)
                throw new Exception("Discovery population lost a class or changed settings");
            if (Nodes(list.Nodes).Any(node => node.Text.Contains("No directory") || node.ToolTipText.Contains("@no_path")))
                throw new Exception("Retired no-directory bucket is still displayed");
            if (!PathNode("idTrigger_Hurt", "trigger/ai/").Checked || !PathNode("idTrigger", "trigger/lootdrop/").Checked
                || !PathNode("idTrigger_GorillaBar", "interact/vault/").Checked || !PathNode("idTrigger", "trigger/props/weapons/").Checked)
                throw new Exception("Resource categories absent from current map discovery were not loaded");
            var commitNode = Root("idEncounterTrigger_Commit");
            if (commitNode.Checked) throw new Exception("Saved hidden class was not restored in UI");
            commitNode.Checked = true;
            if (!settings.IsTriggerClassVisible("idEncounterTrigger_Commit") || writes != 1)
                throw new Exception("Checkbox did not apply its preference immediately");
            if (PathNode("idTrigger", "trigger/props/").Checked || !PathNode("idTrigger", "trigger/interact/").Checked)
                throw new Exception("Saved path subtype states were not restored independently");
            Root("idTrigger").Checked = false; Root("idTrigger").Checked = true;
            if (PathNode("idTrigger", "trigger/props/").Checked || !settings.IsTriggerPathVisible("idTrigger", "trigger/interact/"))
                throw new Exception("Parent class toggle lost its path choices");
            PathNode("idTrigger", "trigger/interact/").Checked = false;
            if (settings.IsTriggerPathVisible("idTrigger", "trigger/interact/") || !settings.IsTriggerPathVisible("idTrigger_Teleporter", "trigger/props/"))
                throw new Exception("Path checkbox changed a different class");
            PathNode("idTrigger", "trigger/interact/").Checked = true;
            int beforeBulk = writes;
            search.Text = "trigger/interact/";
            if (list.Nodes.Count != 1 || Nodes(list.Nodes).Any(node => node.ToolTipText.Contains("props/"))) throw new Exception("Path search leaked sibling folders");
            Descendants(filter).OfType<Button>().Single(button => button.Text == "Hide listed").PerformClick();
            if (!settings.IsTriggerClassVisible("idTrigger") || settings.IsTriggerPathVisible("idTrigger", "trigger/interact/") || writes != beforeBulk + 1)
                throw new Exception("Path search bulk hide disabled its parent class or saved multiple times");
            Descendants(filter).OfType<Button>().Single(button => button.Text == "Show listed").PerformClick();
            search.Text = "Teleporter";
            if (list.Nodes.Count != 2) throw new Exception("Search did not narrow exact classes");
            beforeBulk = writes;
            Descendants(filter).OfType<Button>().Single(button => button.Text == "Hide listed").PerformClick();
            if (settings.IsTriggerClassVisible("idTrigger_Teleporter") || settings.IsTriggerClassVisible("idTrigger_Teleporter_Fade")
                || !settings.IsTriggerClassVisible("idTrigger") || writes != beforeBulk + 1)
                throw new Exception("Bulk hide modified classes outside the search or saved more than once");
            search.Text = "no matching class";
            Descendants(filter).OfType<Button>().Single(button => button.Text == "Hide listed").PerformClick();
            if (writes != beforeBulk + 1) throw new Exception("Empty search changed preferences");
            search.Text = "";
            File.AppendAllText(discovery, "idEncounterTrigger_NewClass\n");
            filterType.GetMethod("ReadDiscoveredClasses", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(filter, null);
            filterType.GetMethod("Populate", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(filter, null);
            if (list.Nodes.Count != TriggerClassCatalog.Known.Count + 2 || settings.IsTriggerClassVisible("idTrigger_Teleporter"))
                throw new Exception("Live discovery reset the current filter");
            File.AppendAllText(Path.Combine(isolated, "trigger-paths.tsv"), "idTrigger\ttrigger/new_group/\n");
            filterType.GetMethod("ReadDiscoveredClasses", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(filter, null);
            filterType.GetMethod("Populate", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(filter, null);
            if (!PathNode("idTrigger", "trigger/new_group/").Checked || PathNode("idTrigger", "trigger/props/").Checked)
                throw new Exception("Live path discovery lost saved choices or hid a new subtype");
            using var filterBitmap = new Bitmap(filter.Width, filter.Height);
            filter.DrawToBitmap(filterBitmap, new Rectangle(0, 0, filterBitmap.Width, filterBitmap.Height));
            filterBitmap.Save(Path.Combine(Path.GetDirectoryName(args[0])!, "trigger-types-window.png"), ImageFormat.Png);
            filter.Close();
            var researchType = Assembly.Load("DEViL").GetType("Devil.ResearchForm", true)!;
            using var research = (Form)Activator.CreateInstance(researchType, settings, isolated, (Action)(() => ++writes), (Func<int>)(() => 1234))!;
            research.Opacity = 0; research.Show(); Application.DoEvents();
            Descendants(research).OfType<Button>().Single(button => button.Text == "Start recording").PerformClick();
            if (!settings.RecordResearch || settings.ResearchProcess != 1234) throw new Exception("Research UI did not start a manual recording");
            Descendants(research).OfType<Button>().Single(button => button.Text == "Stop recording").PerformClick();
            if (settings.RecordResearch) throw new Exception("Research UI did not stop recording");
            Descendants(research).OfType<CheckBox>().Single(check => check.Text == "Show path in game").Checked = false;
            if (settings.ShowResearchPath) throw new Exception("Path checkbox did not update its independent setting");
            Descendants(research).OfType<Button>().Single(button => button.Text == "New session").PerformClick();
            if (settings.ResearchReset == 0) throw new Exception("New-session button did not reset the timeline");
            using var researchBitmap = new Bitmap(research.Width, research.Height);
            research.DrawToBitmap(researchBitmap, new Rectangle(0, 0, researchBitmap.Width, researchBitmap.Height));
            researchBitmap.Save(Path.Combine(Path.GetDirectoryName(args[0])!, "research-window.png"), ImageFormat.Png);
            research.Close();
            string[] encounterLines = [$"#DEVIL_ENCOUNTERS_V1\t1234\t{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}\tlive\tgame/dlc/e4m1_rig/e4m1_rig\t0",
                "id\tgeneration\tmanager\tlabel\tthinkFlags\tenabled\tpaused\tspawnPaused\twaitingExit\tscript\tcommands",
                "100\t3\tstarting_platform_encounter_manager_1\tUAC Atlantica / Combat 1 Landing Pad 1\t1\t1\t0\t0\t0\t0\t1",
                "101\t3\tunlisted_manager\t\t0\t1\t0\t0\t0\t-1\t0"];
            string encounterFile = Path.Combine(isolated, "active-encounters-1234.tsv"); File.WriteAllLines(encounterFile, encounterLines);
            var encountersType = Assembly.Load("DEViL").GetType("Devil.ActiveEncountersForm", true)!;
            using var encounters = (Form)Activator.CreateInstance(encountersType, settings, isolated, (Action)(() => ++writes), (Func<int>)(() => 1234))!;
            encounters.Opacity = 0; encounters.Show();
            var grid = Descendants(encounters).OfType<DataGridView>().Single();
            var deadline = DateTime.UtcNow.AddSeconds(3);
            while (grid.RowCount == 0 && DateTime.UtcNow < deadline) { Application.DoEvents(); Thread.Sleep(5); }
            ((System.Windows.Forms.Timer)encountersType.GetField("_timer", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(encounters)!).Stop();
            if (grid.RowCount != 1 || !grid.Rows[0].Cells[0].Value!.ToString()!.Contains("Landing Pad")) throw new Exception("Active encounter window did not show the reference name");
            Descendants(encounters).OfType<CheckBox>().Single(check => check.Text == "Show inactive managers").Checked = true;
            if (grid.RowCount != 2) throw new Exception("Inactive-manager checkbox did not reveal inactive rows");
            var encounterSearch = Descendants(encounters).OfType<TextBox>().Single(); encounterSearch.Text = "unlisted";
            if (grid.RowCount != 1 || grid.Rows[0].Cells[0].Value!.ToString() != "No name in reference") throw new Exception("Technical-name search or unknown-name fallback failed");
            encounterSearch.Clear();
            Descendants(encounters).OfType<CheckBox>().Single(check => check.Text == "Show in-game HUD").Checked = true;
            if (!settings.ShowActiveEncountersHud) throw new Exception("Encounter HUD checkbox did not persist its state");
            using var encounterBitmap = new Bitmap(encounters.Width, encounters.Height);
            encounters.DrawToBitmap(encounterBitmap, new Rectangle(0, 0, encounterBitmap.Width, encounterBitmap.Height));
            encounterBitmap.Save(Path.Combine(Path.GetDirectoryName(args[0])!, "active-encounters-window.png"), ImageFormat.Png);
            encounters.Close();
            string[] graphLines = [$"#DEVIL_GRAPH_V1\t1234\t{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}\tlive\tgame/dlc/e4m1_rig/e4m1_rig\t500:600\t0\t0\t0\t100:3",
                "N\t100:3\tarena_trigger\tidEncounterTrigger_Commit\t", "N\t101:3\tstarting_platform_encounter_manager_1\tidEncounterManager\tEncounter: UAC Atlantica / Combat 1 Landing Pad 1",
                "N\t102:3\tgate_entity\tidEntity\t", "N\t103:3\tunlinked_entity\tidEntity\t",
                "E\t100:3\t101:3\towners[0]\tManaged pointer / map slot checked", "E\t101:3\t102:3\ttarget\tEntity pointer"];
            string graphFile = Path.Combine(isolated, "entity-graph-1234.tsv"); File.WriteAllLines(graphFile, graphLines);
            var graphType = Assembly.Load("DEViL").GetType("Devil.EntityGraphForm", true)!;
            using var graphForm = (Form)Activator.CreateInstance(graphType, isolated, (Func<int>)(() => 1234))!;
            graphForm.Opacity = 0; graphForm.Show();
            var graphGrid = Descendants(graphForm).OfType<DataGridView>().Single();
            deadline = DateTime.UtcNow.AddSeconds(3);
            while (graphGrid.RowCount == 0 && DateTime.UtcNow < deadline) { Application.DoEvents(); Thread.Sleep(5); }
            ((System.Windows.Forms.Timer)graphType.GetField("_timer", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(graphForm)!).Stop();
            if (graphGrid.RowCount != 1 || graphGrid.Rows[0].Cells[2].Value?.ToString() != "owners[0]") throw new Exception("Crosshair graph did not show exact owner evidence");
            var nodeList = Descendants(graphForm).OfType<ListBox>().Single();
            int managerIndex = nodeList.Items.Cast<Devil.Game.EntityGraphNode>().ToList().FindIndex(node => node.Key == "101:3");
            nodeList.SelectedIndex = managerIndex;
            if (graphGrid.RowCount != 2 || Descendants(graphForm).OfType<CheckBox>().Single(check => check.Text == "Follow crosshair").Checked)
                throw new Exception("Selecting a manager did not navigate incoming/outgoing links or freeze following");
            var graphCanvas = Descendants(graphForm).Single(control => control.GetType().Name == "EntityGraphCanvas");
            var gatePoint = GraphNavigationChecks.NodePoint(graphCanvas, "102:3");
            GraphNavigationChecks.Mouse(graphCanvas, "OnMouseClick", MouseButtons.Right, gatePoint);
            if (graphType.GetField("_selected", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(graphForm) as string != "101:3")
                throw new Exception("RMB selected a graph node");
            GraphNavigationChecks.Mouse(graphCanvas, "OnMouseClick", MouseButtons.Left, gatePoint);
            if (graphType.GetField("_selected", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(graphForm) as string != "102:3"
                || graphGrid.RowCount != 1 || nodeList.SelectedItem is not Devil.Game.EntityGraphNode { Key: "102:3" })
                throw new Exception("LMB on graph did not select the entity and update its links/list");
            nodeList.SelectedIndex = managerIndex;
            GraphNavigationChecks.Run(Path.GetDirectoryName(args[0])!, args.Length > 1 ? args[1] : null);
            using var graphBitmap = new Bitmap(graphForm.Width, graphForm.Height);
            graphForm.DrawToBitmap(graphBitmap, new Rectangle(0, 0, graphBitmap.Width, graphBitmap.Height));
            graphBitmap.Save(Path.Combine(Path.GetDirectoryName(args[0])!, "entity-graph-window.png"), ImageFormat.Png);
            Descendants(graphForm).OfType<CheckBox>().Single(check => check.Text == "Linked entities only").Checked = false;
            if (nodeList.Items.Count != 4) throw new Exception("Graph filter did not reveal isolated entities");
            Descendants(graphForm).OfType<TextBox>().Single().Text = "gate_entity";
            if (nodeList.Items.Count != 1) throw new Exception("Graph search failed");
            graphForm.ClientSize = new Size(1050, 660); Application.DoEvents();
            if (Descendants(graphForm).OfType<FlowLayoutPanel>().Single().Controls.Cast<Control>().Any(control => control.Right > control.Parent!.ClientSize.Width))
                throw new Exception("Graph toolbar is clipped at minimum size");
            // A new map may reuse every numeric ID. It must not retain the previous selection.
            File.WriteAllLines(graphFile, graphLines.Select(line => line.Replace("500:600", "700:800")).Where(line => !line.StartsWith("E\t")));
            graphType.GetMethod("RefreshSnapshot", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(graphForm, null);
            deadline = DateTime.UtcNow.AddSeconds(2);
            while (graphType.GetField("_busy", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(graphForm) is true && DateTime.UtcNow < deadline) { Application.DoEvents(); Thread.Sleep(5); }
            if (graphGrid.RowCount != 0 || graphType.GetField("_selected", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(graphForm) is not null)
                throw new Exception("Map change retained a previous graph selection");
            graphForm.Close();
            MovementUiChecks.Run(isolated, Path.GetDirectoryName(args[0])!);
            LevelStateUiChecks.Run(isolated, Path.GetDirectoryName(args[0])!);
            BookmarkUiChecks.Run(isolated, Path.GetDirectoryName(args[0])!);
        }
        finally { Directory.Delete(isolated, recursive: true); }
        Console.WriteLine("PASS: main layout, trigger filters, encounters and entity graph navigation; all neighbors fit, cursor zoom/RMB pan/LMB selection, view persistence and map reset. Previews saved.");
        form.Close();
    }
}
