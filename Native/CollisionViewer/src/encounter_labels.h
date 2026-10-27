#pragma once
struct EncounterLabel { const char* map; const char* entity; const char* label; };
static constexpr EncounterLabel encounterLabels[]{
#include "encounter_labels.inc"
};
static std::string encounterLabel(const std::string& map,const std::string& name) {
    if(name.empty()) return {};
    if(map.find("master")!=std::string::npos||map.find("horde")!=std::string::npos) return {};
    // Do not apply campaign aliases to master levels or horde maps sharing
    // a resource name. The last component must match the document's map scope.
    if(map.find("/sp/")==std::string::npos&&map.find("/dlc/")==std::string::npos&&
       map.find("/dlc2/")==std::string::npos&&map.find("/hub/")==std::string::npos) return {};
    auto slash=map.find_last_of('/'); std::string component=map.substr(slash==std::string::npos?0:slash+1);
    for(const auto& entry:encounterLabels)
        if(component==entry.map&&name==entry.entity) return std::string("Encounter: ")+entry.label;
    return {};
}
static const char* volumeRole(const std::string& type) {
    if(type.empty()) return "Volume (class unavailable)";
    if(type=="idEncounterTrigger_PreCombat") return "Encounter preparation";
    if(type=="idEncounterTrigger_Commit") return "Encounter commit";
    if(type=="idEncounterTrigger_Exit") return "Encounter exit";
    if(type=="idEncounterTrigger_RaiseUserFlag") return "Encounter user flag";
    if(type=="idEncounterTrigger_OverrideCombatGrouping") return "Combat grouping override";
    if(type=="idEncounterTrigger_AmbientAIAudio") return "Ambient AI audio";
    if(type.rfind("idEncounterTrigger",0)==0) return "Encounter trigger";
    if(type.rfind("idTrigger",0)==0) return "Trigger";
    return "Invisible wall";
}
