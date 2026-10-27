namespace Devil.Configuration;

public sealed record TriggerPathInfo(string ClassName, string Prefix);

public static class TriggerPathCatalog
{
    public static IReadOnlyList<TriggerPathInfo> Known { get; } = ReadBundled();
    private static IReadOnlyList<TriggerPathInfo> ReadBundled()
    {
        using var stream = typeof(TriggerPathCatalog).Assembly.GetManifestResourceStream("Devil.TriggerPaths.tsv");
        if (stream is null) return [];
        using var reader = new StreamReader(stream);
        var lines = new List<string>();
        while (reader.ReadLine() is { } line && lines.Count < 2049) lines.Add(line);
        return Parse(lines);
    }
    public static string Normalize(string prefix) => prefix.Replace('\\', '/').ToLowerInvariant();
    public static bool IsValidPrefix(string? prefix)
    {
        return prefix is { Length: > 1 and <= 256 } && prefix.EndsWith('/') && prefix == Normalize(prefix)
            && prefix.All(c => char.IsAsciiLetterOrDigit(c) || c is '_' or '-' or '.' or '/')
            && prefix[..^1].Split('/').All(part => part.Length > 0 && part is not ("." or ".."));
    }
    public static bool IsValid(TriggerPathInfo? item) => item is not null
        && TriggerClassCatalog.IsValidName(item.ClassName) && IsValidPrefix(item.Prefix);
    public static IEnumerable<string> Ancestors(string prefix)
    {
        for (int i = 0; i < prefix.Length; ++i) if (prefix[i] == '/') yield return prefix[..(i + 1)];
    }
    public static IReadOnlyList<TriggerPathInfo> Read(string file)
    {
        try
        {
            if (!File.Exists(file) || new FileInfo(file).Length > 1_048_576) return [];
            return Parse(File.ReadLines(file).Take(2049).ToArray());
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { return []; }
    }
    private static IReadOnlyList<TriggerPathInfo> Parse(IReadOnlyList<string> lines)
    {
        if (lines.Count == 0 || lines[0] != "#DEVIL_TRIGGER_PATHS_V1") return [];
        var result = new HashSet<TriggerPathInfo>();
        foreach (string line in lines.Skip(1))
        {
            var cells = line.Split('\t');
            if (cells.Length == 2)
            {
                var item = new TriggerPathInfo(cells[0], cells[1]);
                if (IsValid(item)) result.Add(item);
            }
        }
        return result.OrderBy(item => item.ClassName, StringComparer.Ordinal).ThenBy(item => item.Prefix, StringComparer.Ordinal).ToArray();
    }
}
