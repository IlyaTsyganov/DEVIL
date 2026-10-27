#pragma once

struct BookmarkCapture {
    DWORD process=GetCurrentProcessId();
    std::string token,phase="loading",profile;
    uint64_t captured=0;
    LevelIdentity level;
    Vec3 body{},eye{},forward{};
    GameCamera camera{};
    CheckpointObservation checkpoint;
    EntityReference entity;
    std::string entityName,entityState;
    ULONGLONG aimAge=0;
};
// Body origin and rendered eye/aim are separate sources. Never substitute the
// camera for the physical player or describe this observation as a save game.
static BookmarkCapture captureBookmark(const WallMemory& memory,uintptr_t base,const GameProfile& profile,
    const std::string& token,const CheckpointLayout& checkpoint,InspectorPacket selection={},TriggerPathLayout names=triggerPathLayout()) {
    BookmarkCapture result; result.token=token; result.captured=encounterEpochMillis(); result.profile=profile.name;
    result.level=readLevelIdentity(memory,base,profile); if(!result.level) return result;
    uintptr_t player=0,physics=0; unsigned char removed=1;
    if(!memory.get(result.level.map+profile.mapPlayerOffset,player)||!memory.get(player+0xa4,removed)||removed||
       !memory.get(player+0x1d0,physics)||!readResearchPosition(memory,base,profile,result.body)||
       !readGameCamera(memory,base,profile,result.camera)||!inspectorRay(result.camera,result.eye,result.forward)) { result.phase="read_error"; return result; }
    result.checkpoint=readLoadedCheckpoint(memory,result.level.map,result.level.name,checkpoint);
    if(selection.entity&&selection.level==result.level&&selection.enabled&&GetTickCount64()-selection.updated<=10000) {
        auto state=readEntityStatus(memory,selection.entity,base,profile);
        if(state!=EntityStatus::Removed&&state!=EntityStatus::Unavailable) {
            result.entity=selection.entity; result.entityState=state==EntityStatus::Enabled?"enabled":"disabled";
            result.aimAge=GetTickCount64()-selection.updated;
            uintptr_t text=0; if(names.namePointer<0x200000&&memory.get(selection.entity.address+names.namePointer,text)) result.entityName=readEntityText(memory,text,512);
        }
    }
    uintptr_t finalPlayer=0,finalPhysics=0; Vec3 finalBody{};
    if(readLevelIdentity(memory,base,profile)!=result.level||!memory.get(result.level.map+profile.mapPlayerOffset,finalPlayer)||finalPlayer!=player||
       !memory.get(player+0x1d0,finalPhysics)||finalPhysics!=physics||!readResearchPosition(memory,base,profile,finalBody)||motionLength(motionSub(finalBody,result.body))>.1f) {
        result.phase="changed_during_capture"; result.entity={}; return result;
    }
    result.phase="ready"; return result;
}
static bool writeBookmark(const BookmarkCapture& value,const std::filesystem::path& path) {
    auto temporary=path; temporary+=L".tmp";
    try {
        std::filesystem::create_directories(path.parent_path()); std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output.imbue(std::locale::classic()); output.precision(9);
        output<<"#DEVIL_POINT_V1\t"<<value.process<<'\t'<<value.token<<'\t'<<value.captured<<'\t'<<value.phase<<'\t'<<stateClean(value.level.name)<<'\t'<<stateClean(value.profile)<<'\t'
            <<value.level.map<<':'<<value.level.world<<'\t'<<value.checkpoint.status<<'\t'<<stateClean(value.checkpoint.name,1023)<<'\n';
        if(value.phase=="ready") {
            output<<"P"; for(auto p:{value.body,value.eye,value.forward}) output<<'\t'<<p.x<<'\t'<<p.y<<'\t'<<p.z;
            for(float number:value.camera.matrix) output<<'\t'<<number; output<<'\n';
            if(value.entity) output<<"E\t"<<value.entity.id<<'\t'<<value.entity.generation<<'\t'<<stateClean(value.entityName)<<'\t'<<value.entity.className<<'\t'<<value.entityState<<'\t'<<value.aimAge<<'\n';
        }
        output.close(); return bool(output)&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
    } catch(...) { return false; }
}
