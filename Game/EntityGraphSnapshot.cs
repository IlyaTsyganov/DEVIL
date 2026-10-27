using System.Globalization;
using System.Text.Json;

namespace Devil.Game;

public sealed record EntityGraphNode(string Key, string Name, string Class, string Label)
{
    public string Display => string.IsNullOrEmpty(Name) ? $"<{Class}> [{Key}]" : Name;
    public override string ToString() => Display;
}
public sealed record EntityGraphEdge(string From, string To, string Field, string Evidence);

public sealed record EntityGraphSnapshot(int Process, DateTimeOffset Captured, string Phase, string Map, string MapIdentity,
    int Unavailable, int Pending, int Limited, string Selected, IReadOnlyDictionary<string, EntityGraphNode> Nodes,
    IReadOnlyList<EntityGraphEdge> Edges)
{
    public bool Ready => Phase == "live";
    public bool IsFresh(DateTimeOffset now) => Captured <= now.AddSeconds(5) && now - Captured <= TimeSpan.FromSeconds(4);
    public IEnumerable<EntityGraphNode> Search(string query, bool linkedOnly)
    {
        if (!Ready) return [];
        var linked = linkedOnly ? Edges.SelectMany(edge => new[] { edge.From, edge.To }).ToHashSet(StringComparer.Ordinal) : null;
        return Nodes.Values.Where(node => (linked is null || linked.Contains(node.Key))
            && $"{node.Name} {node.Class} {node.Label} {node.Key}".Contains(query.Trim(), StringComparison.OrdinalIgnoreCase))
            .OrderBy(node => node.Display, StringComparer.Ordinal);
    }
    public void ExportJson(string path) => File.WriteAllText(path, JsonSerializer.Serialize(new
    {
        Format = "DEVIL_GRAPH_V1", Process, Captured, Map, MapIdentity, Phase, Unavailable, Pending, Limited,
        Meaning = "Edges are observed entity references. They do not prove script execution or causality.",
        Nodes = Nodes.Values, Edges
    }, new JsonSerializerOptions { WriteIndented = true }));

    public static string RequestPath(string directory, int process) => Path.Combine(directory, $"entity-graph-{process}.request");
    public static void Request(string directory, int process)
    {
        if (process <= 0) return;
        Directory.CreateDirectory(directory);
        string path = RequestPath(directory, process), temporary = path + ".tmp";
        File.WriteAllText(temporary, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds().ToString(CultureInfo.InvariantCulture));
        File.Move(temporary, path, overwrite: true);
    }
    public static EntityGraphSnapshot? Read(string directory, int process)
    {
        if (process <= 0) return null;
        string path = Path.Combine(directory, $"entity-graph-{process}.tsv");
        if (!File.Exists(path) || new FileInfo(path).Length > 16_000_000) return null;
        var result = Parse(File.ReadAllLines(path));
        return result?.Process == process ? result : null;
    }
    private static bool KeyValid(string key)
    {
        var parts = key.Split(':');
        return parts.Length == 2 && uint.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out _)
            && uint.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out _);
    }
    public static EntityGraphSnapshot? Parse(string[] lines)
    {
        if (lines.Length is < 1 or > 49153 || lines[0].Length > 2048) return null;
        string[] header = lines[0].Split('\t');
        if (header.Length != 10 || header[0] != "#DEVIL_GRAPH_V1" || !int.TryParse(header[1], out int process) || process <= 0
            || !long.TryParse(header[2], out long epoch) || header[4].Length > 512 || header[5].Length > 80
            || !int.TryParse(header[6], out int unavailable) || unavailable < 0 || !int.TryParse(header[7], out int pending) || pending < 0
            || !int.TryParse(header[8], out int limited) || limited < 0 || (header[9].Length != 0 && !KeyValid(header[9]))) return null;
        DateTimeOffset captured;
        try { captured = DateTimeOffset.FromUnixTimeMilliseconds(epoch); }
        catch (ArgumentOutOfRangeException) { return null; }
        var nodes = new Dictionary<string, EntityGraphNode>(StringComparer.Ordinal);
        var edges = new List<EntityGraphEdge>();
        foreach (string line in lines.Skip(1))
        {
            if (line.Length > 2048) return null;
            var cells = line.Split('\t');
            if (cells.Length != 5 || cells.Any(cell => cell.Length > 512)) return null;
            if (cells[0] == "N")
            {
                if (nodes.Count >= 32768 || !KeyValid(cells[1]) || cells[3].Length == 0
                    || !nodes.TryAdd(cells[1], new(cells[1], cells[2], cells[3], cells[4]))) return null;
            }
            else if (cells[0] == "E")
            {
                if (edges.Count >= 16384 || !KeyValid(cells[1]) || !KeyValid(cells[2]) || cells[3].Length == 0
                    || cells[4] is not ("Entity pointer" or "Managed pointer / map slot checked" or "Managed reference / spawn ID checked")) return null;
                edges.Add(new(cells[1], cells[2], cells[3], cells[4]));
            }
            else return null;
        }
        if (edges.Any(edge => !nodes.ContainsKey(edge.From) || !nodes.ContainsKey(edge.To))
            || (header[9].Length > 0 && !nodes.ContainsKey(header[9])) || (header[3] != "live" && (nodes.Count > 0 || edges.Count > 0))) return null;
        return new(process, captured, header[3], header[4], header[5], unavailable, pending, limited, header[9], nodes, edges);
    }
}
