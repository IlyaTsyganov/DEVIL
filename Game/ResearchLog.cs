using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace Devil.Game;

public sealed record ResearchEvent(long Sequence, long Milliseconds, string Kind, string Map, uint Id,
    uint Generation, string Class, float? X, float? Y, float? Z, string Detail)
{
    public string Time => TimeSpan.FromMilliseconds(Milliseconds).ToString(@"hh\:mm\:ss\.fff");
    public string Identity => string.IsNullOrEmpty(Class) ? "" : $"{Id} / {Generation}";
}

public sealed record ResearchLog(int Process, bool Recording, long Omitted, string Session, IReadOnlyList<ResearchEvent> Rows)
{
    public static ResearchLog? ReadLatest(string settingsDirectory)
    {
        string pointer = Path.Combine(settingsDirectory, "research-current.txt");
        if (!File.Exists(pointer) || new FileInfo(pointer).Length > 128) return null;
        string name = File.ReadAllText(pointer).Trim();
        if (!Regex.IsMatch(name, @"\A[0-9]+-[0-9]+\.tsv\z", RegexOptions.CultureInvariant)) return null;
        string file = Path.Combine(settingsDirectory, "research", name);
        if (!File.Exists(file) || new FileInfo(file).Length > 20_000_000) return null;
        return Parse(File.ReadAllLines(file));
    }

    public static ResearchLog? Parse(string[] lines)
    {
        if (lines.Length < 2) return null;
        string[] header = lines[0].Split('\t');
        if (header.Length != 5 || header[0] != "#DEVIL_RESEARCH_V1" || !int.TryParse(header[1], out int process)
            || header[2] is not ("0" or "1") || !long.TryParse(header[3], out long omitted)) return null;
        var rows = new List<ResearchEvent>();
        foreach (string line in lines.Skip(2).Take(16000))
        {
            string[] cells = line.Split('\t');
            if (cells.Length != 11 || !long.TryParse(cells[0], out long sequence) || !long.TryParse(cells[1], out long time)
                || time < 0 || time > 604_800_000 || !uint.TryParse(cells[4], out uint id) || !uint.TryParse(cells[5], out uint generation)) continue;
            float? Number(string value) => float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float number)
                && float.IsFinite(number) ? number : null;
            rows.Add(new(sequence, time, cells[2], cells[3], id, generation, cells[6], Number(cells[7]), Number(cells[8]), Number(cells[9]), cells[10]));
        }
        return new(process, header[2] == "1", omitted, header[4], rows);
    }

    public void ExportCsv(string path)
        => WriteCsv(Rows, path);

    public void ExportFullCsv(string settingsDirectory, string path)
    {
        if (!Regex.IsMatch(Session, @"\A[0-9]+-[0-9]+\z", RegexOptions.CultureInvariant)) throw new IOException("Invalid session ID");
        string full = Path.Combine(settingsDirectory, "research", Session + ".full.tsv");
        if (Path.GetFullPath(full).Equals(Path.GetFullPath(path), StringComparison.OrdinalIgnoreCase)) throw new IOException("Choose a CSV destination outside the source journal.");
        if (!File.Exists(full)) { ExportCsv(path); return; }
        IEnumerable<ResearchEvent> ReadRows()
        {
            using var reader = new StreamReader(new FileStream(full, FileMode.Open, FileAccess.Read, FileShare.ReadWrite));
            string header = reader.ReadLine() ?? ""; string columns = reader.ReadLine() ?? "";
            while (reader.ReadLine() is { } line)
            {
                if (line.Length > 8192) continue;
                var parsed = Parse([header, columns, line]);
                if (parsed?.Rows.Count == 1) yield return parsed.Rows[0];
            }
        }
        WriteCsv(ReadRows(), path);
    }

    private static void WriteCsv(IEnumerable<ResearchEvent> rows, string path)
    {
        static string Cell(string value) => "\"" + value.Replace("\"", "\"\"") + "\"";
        using var writer = new StreamWriter(path, false, new UTF8Encoding(true));
        writer.WriteLine("sequence,elapsed_ms,event,map,id,generation,class,x,y,z,detail");
        foreach (var row in rows)
            writer.WriteLine(string.Join(',', new[] { row.Sequence.ToString(CultureInfo.InvariantCulture), row.Milliseconds.ToString(CultureInfo.InvariantCulture),
                Cell(row.Kind), Cell(row.Map), row.Id.ToString(CultureInfo.InvariantCulture), row.Generation.ToString(CultureInfo.InvariantCulture), Cell(row.Class),
                row.X?.ToString("R", CultureInfo.InvariantCulture) ?? "", row.Y?.ToString("R", CultureInfo.InvariantCulture) ?? "",
                row.Z?.ToString("R", CultureInfo.InvariantCulture) ?? "", Cell(row.Detail) }));
    }
}
