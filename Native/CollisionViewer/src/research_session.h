#pragma once
#include <deque>
#include <sstream>
#include <locale>

struct ResearchControl {
    bool recording=false,showPath=false;
    DWORD process=0;
    ULONGLONG reset=0,marker=0;
    std::string label;
};
struct ResearchPoint { Vec3 position{}; ULONGLONG time=0; uint64_t segment=0; };
struct ResearchRoute { uintptr_t map=0,world=0; std::string level; std::vector<ResearchPoint> points; };
struct ResearchRow {
    uint64_t sequence=0; ULONGLONG time=0;
    std::string kind,level,type,detail;
    uint32_t id=0,generation=0;
    bool positioned=false; Vec3 position{};
};
static std::filesystem::path researchDirectory() {
    wchar_t directory[32768]{}; DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",directory,32768);
    return length&&length<32768?std::filesystem::path(directory)/L"DEViL":std::filesystem::path{};
}
static bool readResearchPosition(const WallMemory& memory,uintptr_t base,const GameProfile& profile,Vec3& position) {
    uintptr_t system=0,map=0,player=0,vtable=0,physics=0;
    if(!memory.get(base+profile.gameSystemRva,system)||!memory.get(system+0x50,map)||
       !memory.get(map+profile.mapPlayerOffset,player)||!memory.get(player,vtable)||vtable!=base+profile.playerVtableRva||
       !memory.get(player+0x1d0,physics)||!memory.read(physics+0xb0,&position,sizeof(position))) return false;
    return std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z)&&
        fabsf(position.x)<1000000&&fabsf(position.y)<1000000&&fabsf(position.z)<1000000;
}
class ResearchSession {
    struct Observed { EntityStatus state; };
    std::unordered_map<VolumeIdentity,Observed,VolumeIdentityHash> observed;
    std::deque<ResearchRow> rows;
    std::deque<ResearchPoint> points;
    std::filesystem::path directory;
    std::ofstream journal;
    ULONGLONG started=0,lastWrite=0,lastPosition=0,resetToken=0,markerToken=0;
    uint64_t sequence=0,segment=0,omitted=0;
    uintptr_t currentMap=0,currentWorld=0;
    std::string currentLevel;
    bool recording=false,interrupted=true,baseline=false,dirty=false;
    std::string session;
    static std::string clean(std::string text) {
        if(text.size()>512) text.resize(512);
        for(char& c:text) if(static_cast<unsigned char>(c)<32||c==127) c=' ';
        return text;
    }
    static void writeRow(std::ostream& output,const ResearchRow& row) {
        output<<row.sequence<<'\t'<<row.time<<'\t'<<row.kind<<'\t'<<clean(row.level)<<'\t'<<row.id<<'\t'<<row.generation<<'\t'<<clean(row.type)<<'\t';
        if(row.positioned) output<<row.position.x<<'\t'<<row.position.y<<'\t'<<row.position.z;
        else output<<"\t\t";
        output<<'\t'<<clean(row.detail)<<'\n';
    }
    void add(std::string kind,ULONGLONG now,const Vec3* position,std::string detail={},const EntityReference* ref=nullptr) {
        ResearchRow row; row.sequence=++sequence; row.time=now>=started?now-started:0;
        row.kind=std::move(kind); row.level=currentLevel; row.detail=std::move(detail);
        if(position) { row.positioned=true; row.position=*position; }
        if(ref) { row.id=ref->id; row.generation=ref->generation; row.type=ref->className; }
        if(journal.is_open()&&journal) writeRow(journal,row);
        rows.push_back(std::move(row)); if(rows.size()>maxRows) { rows.pop_front(); ++omitted; } dirty=true;
    }
    void newSession(ULONGLONG now) {
        if(journal.is_open()) journal.close(); journal.clear();
        rows.clear(); points.clear(); observed.clear(); started=now; lastPosition=0;
        sequence=segment=omitted=0; currentMap=currentWorld=0; currentLevel.clear();
        interrupted=true; baseline=false;
        session=std::to_string(GetCurrentProcessId())+"-"+std::to_string(now); dirty=true;
        if(!directory.empty()) {
            try {
                std::filesystem::create_directories(directory/L"research");
                journal.open(directory/L"research"/(session+".full.tsv"),std::ios::binary|std::ios::trunc);
                journal.imbue(std::locale::classic()); journal.precision(9);
                journal<<"#DEVIL_RESEARCH_V1\t"<<GetCurrentProcessId()<<"\t1\t0\t"<<session<<'\n';
                journal<<"sequence\telapsed_ms\tkind\tmap\tid\tgeneration\tclass\tx\ty\tz\tdetail\n";
            } catch(...) { }
        }
    }
public:
    static constexpr size_t maxPoints=12000,maxRows=16000;
    explicit ResearchSession(std::filesystem::path output=researchDirectory()):directory(std::move(output)) {}
    bool active() const { return recording; }
    void finish(ULONGLONG now=GetTickCount64()) {
        if(recording) { recording=false; add("recording_stopped",now,nullptr); }
        publish(now,true); if(journal.is_open()) journal.close();
    }
    const std::deque<ResearchRow>& events() const { return rows; }
    void update(const ResearchControl& control,uintptr_t map,uintptr_t world,const std::string& level,
                const Vec3* position,const std::vector<Mesh>* volumes,ULONGLONG now=GetTickCount64()) {
        bool requested=control.recording&&control.process==GetCurrentProcessId();
        if(control.reset!=resetToken) {
            resetToken=control.reset;
            markerToken=control.marker;
            if(!session.empty()) publish(now,true);
            newSession(now); add("session_reset",now,nullptr);
        }
        if(requested&&!recording) {
            if(session.empty()) { markerToken=control.marker; newSession(now); }
            recording=true; interrupted=true; add("recording_started",now,nullptr);
        } else if(!requested&&recording) {
            recording=false; interrupted=true; add("recording_stopped",now,nullptr); publish(now,true);
        }
        if(!recording) { publish(now); return; }
        if(map!=currentMap||world!=currentWorld||level!=currentLevel) {
            currentMap=map; currentWorld=world; currentLevel=level;
            points.clear(); observed.clear(); baseline=false; interrupted=true;
            add("level_changed",now,position,level);
        }
        if(control.marker!=markerToken) {
            markerToken=control.marker;
            if(control.marker) add("marker",now,position,control.label);
        }
        if(!position) {
            if(!interrupted) add("sampling_paused",now,nullptr,"Menu, loading or unreadable player position");
            interrupted=true; publish(now); return;
        }
        if(interrupted) { ++segment; add("sampling_resumed",now,position); interrupted=false; }
        if(!points.empty()) {
            const auto& previous=points.back();
            Vec3 delta{position->x-previous.position.x,position->y-previous.position.y,position->z-previous.position.z};
            float distance=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
            if(previous.segment==segment&&((lastPosition&&now-lastPosition>1000)||distance>2500)) {
                ++segment; add("path_break",now,position,"Sampling gap or large position change; no connecting line");
            }
        }
        if(now-lastPosition>=100) {
            lastPosition=now;
            bool moved=points.empty()||points.back().segment!=segment;
            if(!moved) {
                Vec3 delta{position->x-points.back().position.x,position->y-points.back().position.y,position->z-points.back().position.z};
                moved=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>=.0025f;
            }
            if(moved) {
                points.push_back({*position,now,segment}); if(points.size()>maxPoints) points.pop_front();
                add("position",now,position,std::to_string(segment));
            }
        }
        if(volumes) {
            for(const auto& mesh:*volumes) {
                if(!mesh.entity) continue;
                VolumeIdentity identity(mesh.entity); auto found=observed.find(identity);
                if(found==observed.end()) {
                    observed.emplace(identity,Observed{mesh.volumeState});
                    if(baseline) add("volume_discovered",now,position,entityStatusText(mesh.volumeState),&mesh.entity);
                } else if(found->second.state!=mesh.volumeState) {
                    std::string change=std::string(entityStatusText(found->second.state))+" -> "+entityStatusText(mesh.volumeState);
                    add("volume_state",now,position,std::move(change),&mesh.entity); found->second.state=mesh.volumeState;
                }
            }
            // Bound retained identities independently of the rendering history.
            if(observed.size()>65536) { observed.clear(); baseline=false; add("observation_reset",now,position,"Identity cache limit"); }
            else baseline=true;
        }
        publish(now);
    }
    std::shared_ptr<const ResearchRoute> route() const {
        auto result=std::make_shared<ResearchRoute>(); result->map=currentMap; result->world=currentWorld; result->level=currentLevel;
        result->points.assign(points.begin(),points.end()); return result;
    }
    void publish(ULONGLONG now,bool force=false) {
        if(directory.empty()||session.empty()||(!force&&(!dirty||now-lastWrite<1000))) return;
        lastWrite=now;
        if(journal) journal.flush();
        try {
            std::filesystem::create_directories(directory/L"research");
            auto target=directory/L"research"/(session+".tsv"); auto temporary=target; temporary+=L".tmp";
            {
                std::ofstream output(temporary,std::ios::binary|std::ios::trunc); output.imbue(std::locale::classic());
                output<<"#DEVIL_RESEARCH_V1\t"<<GetCurrentProcessId()<<'\t'<<recording<<'\t'<<omitted<<'\t'<<clean(session)<<'\n';
                output<<"sequence\telapsed_ms\tkind\tmap\tid\tgeneration\tclass\tx\ty\tz\tdetail\n";
                output.precision(9);
                for(const auto& row:rows) writeRow(output,row);
                output.close(); if(!output) return;
            }
            if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING)) return;
            // A tiny atomic pointer lets the companion find the latest session.
            auto status=directory/L"research-current.txt",statusTemp=status; statusTemp+=L".tmp";
            { std::ofstream output(statusTemp,std::ios::binary|std::ios::trunc); output<<session<<".tsv\n"; output.close(); if(!output) return; }
            if(MoveFileExW(statusTemp.c_str(),status.c_str(),MOVEFILE_REPLACE_EXISTING)) dirty=false;
        } catch(...) { /* Recording failures never interrupt the game renderer. */ }
    }
};
