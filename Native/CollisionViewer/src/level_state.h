#pragma once

struct StateVolume {
    EntityReference ref;
    std::string name,kind,status,geometry="none",shape;
    bool hasBounds=false,contentsKnown=false;
    uint32_t contents=0;
    Mesh mesh{};
};
struct LevelStateSnapshot {
    DWORD process=GetCurrentProcessId();
    std::string token,phase="loading",profile;
    uint64_t captured=0;
    LevelIdentity level;
    CheckpointObservation checkpoint;
    uint32_t unavailable=0;
    bool membershipComplete=false;
    std::vector<std::pair<uint32_t,uint32_t>> members;
    std::vector<StateVolume> volumes;
    ActiveEncounterSnapshot encounters;
};
static std::string stateClean(std::string value,size_t limit=512) {
    if(value.size()>limit) value.resize(limit);
    for(char& c:value) if(static_cast<unsigned char>(c)<32||c==127) c=' ';
    return value;
}
static std::string stateKey(uint32_t id,uint32_t generation) { return std::to_string(id)+":"+std::to_string(generation); }
static std::string stateShapeFingerprint(const Geometry& geometry) {
    uint64_t hash=1469598103934665603ULL;
    auto add=[&](uint64_t value) { for(unsigned i=0;i<8;++i) { hash^=static_cast<unsigned char>(value>>(i*8)); hash*=1099511628211ULL; } };
    add(geometry.triangles.size());
    for(auto p:geometry.triangles) for(float value:{p.x,p.y,p.z}) {
        if(!std::isfinite(value)||fabsf(value)>1000000) return {};
        add(static_cast<uint64_t>(static_cast<int64_t>(llround(double(value)*10000))));
    }
    std::ostringstream text; text<<std::hex<<hash; return text.str();
}
class LevelStateReader {
    LiveWalls scanner;
    ActiveEncounterReader encounters;
    std::unordered_map<uintptr_t,std::string> classes;
public:
    LevelStateSnapshot capture(const WallMemory& memory,uintptr_t base,const GameProfile& profile,const std::string& token,
        const EncounterFields& encounterFields,const CheckpointLayout& checkpointLayout,TriggerPathLayout paths=triggerPathLayout()) {
        LevelStateSnapshot result; result.token=token; result.profile=profile.name; result.captured=encounterEpochMillis();
        result.level=readLevelIdentity(memory,base,profile); if(!result.level) return result;
        scanner.memory=memory; scanner.refresh(base,profile,true,true,true,GetTickCount64(),paths);
        auto level=result.level; uintptr_t system=0,list=0; uint32_t count=0;
        if(!memory.get(base+profile.gameSystemRva,system)||!memory.get(level.map+profile.mapEntitiesOffset,list)||
           !memory.get(level.map+profile.mapEntitiesOffset+8,count)||count>32768) { result.phase="read_error"; return result; }
        struct Entry { uint32_t id,generation; uintptr_t entity,previous,next; };
        std::vector<Entry> entries(count);
        if(count&&!memory.read(list,entries.data(),entries.size()*sizeof(Entry))) { result.phase="read_error"; return result; }
        std::unordered_map<std::string,const Mesh*> geometry;
        for(const auto& mesh:scanner.meshes) geometry.emplace(stateKey(mesh.entity.id,mesh.entity.generation),&mesh);
        std::unordered_set<std::string> identities; size_t savedVertices=0;
        auto nameLayout=paths.namePointer;
        for(size_t i=0;i<entries.size();++i) {
            const auto& entry=entries[i]; if(!entry.entity) continue;
            auto key=stateKey(entry.id,entry.generation);
            if(!identities.insert(key).second) { result.phase="read_error"; return result; }
            result.members.emplace_back(entry.id,entry.generation);
            uintptr_t vtable=0; if(!memory.get(entry.entity,vtable)) { ++result.unavailable; continue; }
            auto type=classes.find(vtable);
            if(type==classes.end()) type=classes.emplace(vtable,readEntityClass(memory,vtable,base,profile)).first;
            else if(type->second.empty()) type->second=readEntityClass(memory,vtable,base,profile);
            bool trigger=validTriggerClassName(type->second),wall=vtable==base+profile.wallVtableRva;
            if(!trigger&&!wall) { if(type->second.empty()) ++result.unavailable; continue; }
            if(result.volumes.size()>=8192) { ++result.unavailable; continue; }
            EntityReference ref{entry.entity,level.map,list+i*sizeof(Entry),entry.id,entry.generation,type->second.empty()&&wall?"idDynamicEntity":type->second};
            StateVolume row; row.ref=ref; row.kind=trigger?"trigger":"wall"; row.mesh.entity=ref; row.mesh.kind=trigger?0u:2u;
            EntityStatus status=readEntityStatus(memory,ref,base,profile);
            row.status=status==EntityStatus::Enabled?"enabled":status==EntityStatus::Disabled?"disabled":status==EntityStatus::Removed?"removed":"unavailable";
            uintptr_t physics=0;
            row.contentsKnown=memory.get(entry.entity+0x1d0,physics)&&memory.get(physics+0x44,row.contents);
            auto found=geometry.find(key);
            // A disabled generic dynamic entity is only a known wall after it
            // was observed as playerclip; do not reclassify every disabled prop.
            if(wall&&found==geometry.end()&&!(row.contentsKnown&&(row.contents&8)&&!(row.contents&3))) continue;
            if(nameLayout<0x200000) { uintptr_t text=0; if(memory.get(entry.entity+nameLayout,text)) row.name=readEntityText(memory,text,512); }
            if(found!=geometry.end()) {
                row.mesh=*found->second; row.hasBounds=true; row.geometry=row.mesh.historical?"last":"exact";
                if(row.mesh.geometry) {
                    row.shape=stateShapeFingerprint(*row.mesh.geometry);
                    size_t vertices=row.mesh.geometry->lines.size()+row.mesh.geometry->triangles.size();
                    if(savedVertices+vertices<=1500000) savedVertices+=vertices;
                    else { row.mesh.geometry.reset(); row.geometry=row.mesh.historical?"last_bounds":"bounds"; }
                }
            } else {
                float bounds[6]{}; float axis[9]{},origin[3]{};
                bool available=physics&&memory.read(physics+0xe0,bounds,24)&&memory.read(physics+0x8c,axis,36)&&memory.read(physics+0xb0,origin,12);
                for(unsigned k=0;k<3;++k) available=available&&std::isfinite(bounds[k])&&std::isfinite(bounds[k+3])&&bounds[k]<=bounds[k+3]&&fabsf(bounds[k])<1000000&&fabsf(bounds[k+3])<1000000;
                for(float value:axis) available=available&&std::isfinite(value); for(float value:origin) available=available&&std::isfinite(value);
                if(available&&status!=EntityStatus::Removed&&status!=EntityStatus::Unavailable) {
                    row.hasBounds=true; row.geometry="bounds"; memcpy(row.mesh.bounds,bounds,24);
                    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<4;++c) row.mesh.matrix[r*4+c]=c<3?axis[c*3+r]:origin[r];
                }
            }
            if(status==EntityStatus::Unavailable) ++result.unavailable;
            Entry check{};
            if(!memory.get(ref.slot,check)||check.id!=entry.id||check.generation!=entry.generation||check.entity!=entry.entity) { row.status="unavailable"; ++result.unavailable; }
            result.volumes.push_back(std::move(row));
        }
        result.encounters=encounters.capture(memory,base,profile,level,encounterFields);
        result.unavailable+=result.encounters.unreadable;
        result.checkpoint=readLoadedCheckpoint(memory,level.map,level.name,checkpointLayout);
        uintptr_t finalList=0; uint32_t finalCount=0;
        if(readLevelIdentity(memory,base,profile)!=level||!memory.get(level.map+profile.mapEntitiesOffset,finalList)||finalList!=list||
           !memory.get(level.map+profile.mapEntitiesOffset+8,finalCount)||finalCount!=count) {
            result.phase="loading"; result.volumes.clear(); result.members.clear(); return result;
        }
        std::vector<Entry> finalEntries(count);
        if((count&&!memory.read(list,finalEntries.data(),finalEntries.size()*sizeof(Entry)))||
           !std::equal(entries.begin(),entries.end(),finalEntries.begin(),[](const Entry& a,const Entry& b) { return a.id==b.id&&a.generation==b.generation&&a.entity==b.entity; })) {
            result.phase="loading"; result.volumes.clear(); result.members.clear(); result.encounters.rows.clear(); return result;
        }
        result.membershipComplete=true; result.phase="ready"; return result;
    }
};
static bool writeLevelState(const LevelStateSnapshot& state,const std::filesystem::path& path) {
    auto temporary=path; temporary+=L".tmp";
    try {
        std::filesystem::create_directories(path.parent_path()); std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output.imbue(std::locale::classic()); output.precision(9);
        output<<"#DEVIL_LEVEL_STATE_V1\t"<<state.process<<'\t'<<state.token<<'\t'<<state.captured<<'\t'<<state.phase<<'\t'<<stateClean(state.level.name)<<'\t'<<stateClean(state.profile)<<'\t'
            <<state.level.map<<':'<<state.level.world<<'\t'<<state.checkpoint.status<<'\t'<<stateClean(state.checkpoint.name,1023)<<'\t'<<state.unavailable<<'\t'<<state.membershipComplete<<'\t'<<state.encounters.phase<<'\n';
        for(auto entry:state.members) output<<"I\t"<<entry.first<<'\t'<<entry.second<<'\n';
        for(const auto& row:state.volumes) {
            output<<"V\t"<<row.ref.id<<'\t'<<row.ref.generation<<'\t'<<stateClean(row.name)<<'\t'<<row.ref.className<<'\t'<<row.kind<<'\t'<<row.status<<'\t'<<row.geometry<<'\t';
            if(row.contentsKnown) output<<row.contents; else output<<'?';
            for(float value:row.mesh.bounds) { output<<'\t'; if(row.hasBounds) output<<value; else output<<'?'; }
            for(float value:row.mesh.matrix) { output<<'\t'; if(row.hasBounds) output<<value; else output<<'?'; }
            output<<'\t'<<row.shape<<'\n';
        }
        for(const auto& row:state.encounters.rows) {
            output<<"M\t"<<row.id<<'\t'<<row.generation<<'\t'<<stateClean(row.name)<<'\t'<<stateClean(row.label)<<'\t';
            if(row.known) output<<row.think; else output<<'?';
            output<<'\t'<<row.enabled<<'\t'<<row.paused<<'\t'<<row.spawnPaused<<'\t'<<row.waitingExit<<'\t'<<row.script<<'\t'<<row.commands<<'\n';
        }
        output.close(); return bool(output)&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
    } catch(...) { return false; }
}
