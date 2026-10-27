namespace Devil.Configuration;

public sealed record TriggerClassInfo(string ClassName, string Label)
{
    public override string ToString() => $"{ClassName} — {Label}";
}

public static class TriggerClassCatalog
{
    // Concrete trigger classes from meathook's reflected Eternal class list.
    // Labels describe the class; actual effects still depend on its targets.
    public static IReadOnlyList<TriggerClassInfo> Known { get; } = Array.AsReadOnly<TriggerClassInfo>([
        new("idEncounterTrigger_Commit", "Encounter start"),
        new("idEncounterTrigger_RaiseUserFlag", "Encounter flag"),
        new("idEncounterTrigger_Exit", "Encounter exit"),
        new("idEncounterTrigger_PreCombat", "Encounter preparation"),
        new("idEncounterTrigger_OverrideCombatGrouping", "Combat grouping"),
        new("idEncounterTrigger_AmbientAIAudio", "Ambient AI audio"),
        new("idTrigger", "General purpose / inspect targets"),
        new("idTrigger_Teleporter", "Teleporter"),
        new("idTrigger_Teleporter_Fade", "Teleporter with fade"),
        new("idTrigger_BouncePad", "Jump pad"),
        new("idTrigger_Push", "Push volume"),
        new("idTrigger_GorillaBar", "Swing bar"),
        new("idTrigger_ModPlayerVelocity", "Player velocity"),
        new("idTrigger_MovementModifier", "Movement modifier"),
        new("idTrigger_Repulsor", "Repulsor"),
        new("idTrigger_Hurt", "Damage volume"),
        new("idTrigger_DynamicHurt", "Dynamic damage volume"),
        new("idTrigger_Damage", "Damage trigger"),
        new("idTrigger_TakeDamage", "Activated by damage"),
        new("idTrigger_VisibilityController", "Map visibility / layers"),
        new("idTrigger_Dormancy", "Entity dormancy"),
        new("idTrigger_RemoveEntities", "Entity removal"),
        new("idTrigger_RemoveInventoryItems", "Inventory removal"),
        new("idTrigger_TestPlayerState", "Player state condition"),
        new("idTrigger_Facing", "Facing condition"),
        new("idTrigger_ForcePlayerBodyReactionOnce", "Player body reaction"),
        new("idTrigger_StatDrivenEvent", "Stat driven event"),
        new("idTrigger_VoiceCommunication", "Voice communication"),
        new("idTrigger_AIInteraction", "AI interaction"),
        new("idTrigger_ChallengeBoundry", "Challenge boundary"),
        new("idTrigger_DemonDamageOutModifier", "Demon damage modifier"),
        new("idTrigger_DummyFire", "Dummy fire"),
        new("idTrigger_EnergyField", "Energy field"),
        new("idTrigger_SonicBoom", "Sonic boom"),
        new("idTrigger_InvasionAreaEnter", "Invasion area entrance"),
        new("idTrigger_InvasionAreaExit", "Invasion area exit"),
        new("idTrigger_Multiplayer", "Multiplayer")
    ]);

    public static bool IsValidName(string? name) => name is { Length: > 0 and <= 128 }
        && (name == "idTrigger" || name.StartsWith("idTrigger_", StringComparison.Ordinal)
            || name.StartsWith("idEncounterTrigger_", StringComparison.Ordinal))
        && name.All(c => char.IsAsciiLetterOrDigit(c) || c == '_');

    public static IReadOnlyList<TriggerClassInfo> Choices(IEnumerable<string> additionalNames)
    {
        var result = Known.ToList();
        var existing = result.Select(item => item.ClassName).ToHashSet(StringComparer.Ordinal);
        foreach (string name in additionalNames.Where(IsValidName).Order(StringComparer.Ordinal))
            if (existing.Add(name)) result.Add(new(name, "Additional class"));
        return result;
    }
}
