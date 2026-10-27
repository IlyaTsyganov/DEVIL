#pragma once

struct VolumeIdentity {
    uintptr_t map=0,address=0;
    uint32_t id=0,generation=0;
    explicit VolumeIdentity(const EntityReference& ref):map(ref.map),address(ref.address),id(ref.id),generation(ref.generation) {}
    bool operator==(const VolumeIdentity& other) const {
        return map==other.map&&address==other.address&&id==other.id&&generation==other.generation;
    }
};
struct VolumeIdentityHash {
    size_t operator()(const VolumeIdentity& value) const {
        size_t h=std::hash<uintptr_t>{}(value.address);
        h^=std::hash<uintptr_t>{}(value.map)+(h<<6)+(h>>2);
        h^=std::hash<uint64_t>{}((uint64_t(value.id)<<32)|value.generation)+(h<<6)+(h>>2);
        return h;
    }
};
class VolumeHistory {
    struct Record { Mesh mesh; bool present=false; ULONGLONG lostAt=0; };
    std::unordered_map<VolumeIdentity,Record,VolumeIdentityHash> records;
    uintptr_t map=0;
    static void state(Record& record,EntityStatus next,ULONGLONG now) {
        if(record.mesh.volumeState!=next) record.mesh.changedAt=now;
        bool lost=next==EntityStatus::Removed||next==EntityStatus::Unavailable;
        if(lost&&!record.lostAt) record.lostAt=now;
        if(!lost) record.lostAt=0;
        record.mesh.volumeState=next;
    }
public:
    static constexpr ULONGLONG retentionMs=15000,pulseMs=2000;
    void clear() { records.clear(); map=0; }
    void begin(uintptr_t currentMap) {
        if(map!=currentMap) { clear(); map=currentMap; }
        for(auto& entry:records) entry.second.present=false;
    }
    void present(const EntityReference& ref) {
        auto found=records.find(VolumeIdentity(ref));
        if(found!=records.end()) { found->second.present=true; found->second.mesh.entity.slot=ref.slot; }
    }
    bool knownWall(const EntityReference& ref) const {
        auto found=records.find(VolumeIdentity(ref));
        return found!=records.end()&&found->second.mesh.kind==2;
    }
    void sample(Mesh mesh,EntityStatus status,ULONGLONG now) {
        VolumeIdentity key(mesh.entity); auto found=records.find(key);
        if(found==records.end()) {
            // Initial discovery is not evidence of a recent activation.
            mesh.changedAt=0; mesh.volumeState=status;
            found=records.emplace(key,Record{}).first;
        } else {
            state(found->second,status,now);
            mesh.changedAt=found->second.mesh.changedAt;
        }
        mesh.volumeState=status; mesh.historical=false; mesh.geometryAt=now;
        found->second.mesh=std::move(mesh); found->second.present=true; found->second.lostAt=0;
    }
    void missing(const EntityReference& ref,EntityStatus status,ULONGLONG now) {
        auto found=records.find(VolumeIdentity(ref)); if(found==records.end()) return;
        auto& record=found->second; record.present=true; record.mesh.entity.slot=ref.slot;
        state(record,status,now); record.mesh.historical=true;
    }
    void collect(std::vector<Mesh>& output,bool triggers,bool disabled,bool history,ULONGLONG now) {
        for(auto it=records.begin();it!=records.end();) {
            Record& record=it->second;
            if(!record.present) { state(record,EntityStatus::Removed,now); record.mesh.historical=true; }
            if(record.lostAt&&now>=record.lostAt&&now-record.lostAt>retentionMs) { it=records.erase(it); continue; }
            const Mesh& mesh=record.mesh;
            bool visible=mesh.volumeState==EntityStatus::Enabled||
                (mesh.volumeState==EntityStatus::Disabled?disabled:history);
            if(visible&&(mesh.kind!=0||triggers)) output.push_back(mesh);
            ++it;
        }
    }
};
