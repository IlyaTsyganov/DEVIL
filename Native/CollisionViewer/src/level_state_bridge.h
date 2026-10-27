#pragma once
#include "level_state.h"
#include "research_bookmarks.h"

static bool validStateToken(const std::string& token) {
    return token.size()==32&&std::all_of(token.begin(),token.end(),[](char c) { return (c>='0'&&c<='9')||(c>='a'&&c<='f'); });
}
struct StateOutline { Mesh mesh; bool bounds=false,dashed=false; std::array<float,4> color; };
struct StateHighlight { LevelIdentity level; uint64_t expires=0; bool ignoreDistance=true; std::vector<StateOutline> outlines; };
class LevelStateBridge {
    std::mutex mutex;
    const GameProfile* profile=nullptr;
    bool started=false;
    std::shared_ptr<const LevelStateSnapshot> before,after;
    std::shared_ptr<const StateHighlight> drawing;
    static std::vector<std::string> cells(const std::string& line) {
        std::vector<std::string> values; std::istringstream input(line); std::string value;
        while(std::getline(input,value,'\t')) values.push_back(value); return values;
    }
    static bool uintValue(const std::string& text,uint32_t& value) {
        if(text.empty()||text.size()>10||!std::all_of(text.begin(),text.end(),[](char c) { return c>='0'&&c<='9'; })) return false;
        try { auto parsed=std::stoull(text); if(parsed>UINT32_MAX) return false; value=static_cast<uint32_t>(parsed); return true; } catch(...) { return false; }
    }
    void highlights(const std::filesystem::path& directory,DWORD process,const GameProfile& selected,uint64_t now) {
        std::shared_ptr<const LevelStateSnapshot> first,second;
        { std::lock_guard<std::mutex> lock(mutex); first=before; second=after; }
        std::string phase="off"; auto next=std::make_shared<StateHighlight>(); unsigned exact=0,bounds=0,missing=0;
        try {
            auto path=directory/(L"state-highlight-"+std::to_wstring(process)+L".request"); std::error_code error;
            if(std::filesystem::file_size(path,error)<=65536&&!error) {
                std::ifstream input(path); std::string line;
                if(std::getline(input,line)) {
                    auto header=cells(line);
                    if(header.size()==8&&header[0]=="#DEVIL_STATE_HIGHLIGHT_V1"&&header[1]==std::to_string(process)&&validStateToken(header[3])&&validStateToken(header[4])&&
                       (header[5]=="0"||header[5]=="1")&&(header[6]=="0"||header[6]=="1")&&(header[7]=="0"||header[7]=="1")) {
                        auto expires=std::stoull(header[2]);
                        if(expires>=now&&expires<=now+5000) {
                            phase="snapshot_not_cached";
                            if(first&&second&&first->token==header[3]&&second->token==header[4]&&first->level==second->level&&first->profile==second->profile) {
                                phase="map_changed";
                                if(readLevelIdentity(WallMemory{},reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),selected)==first->level) {
                                    phase="ready"; next->level=first->level; next->expires=expires; next->ignoreDistance=header[7]=="1";
                                    std::unordered_set<std::string> keys;
                                    for(unsigned i=0;i<256&&std::getline(input,line);++i) {
                                        auto values=cells(line); uint32_t id=0,generation=0;
                                        if(values.size()!=3||values[0]!="V"||!uintValue(values[1],id)||!uintValue(values[2],generation)) { phase="invalid_request"; next->outlines.clear(); break; }
                                        keys.insert(stateKey(id,generation));
                                    }
                                    size_t vertices=0;
                                    auto add=[&](const LevelStateSnapshot& source,bool previous) {
                                        for(const auto& row:source.volumes) if(keys.count(stateKey(row.ref.id,row.ref.generation))) {
                                            if(!row.hasBounds) { ++missing; continue; }
                                            StateOutline outline; outline.mesh=row.mesh; outline.dashed=previous||row.geometry=="last"||row.geometry=="last_bounds";
                                            outline.bounds=!row.mesh.geometry||vertices+row.mesh.geometry->lines.size()>300000;
                                            if(outline.bounds) ++bounds; else { vertices+=row.mesh.geometry->lines.size(); ++exact; }
                                            outline.color=previous?std::array<float,4>{.25f,.7f,1.f,.9f}:
                                                row.status=="disabled"?std::array<float,4>{1.f,.35f,.85f,1.f}:row.status=="removed"?std::array<float,4>{1,.3f,.3f,1}:
                                                row.status=="unavailable"?std::array<float,4>{.7f,.7f,.7f,.8f}:std::array<float,4>{.3f,1.f,.45f,1.f};
                                            if(previous&&std::none_of(second->members.begin(),second->members.end(),[&](const auto& member) { return member.first==row.ref.id&&member.second==row.ref.generation; })) outline.color={1,.3f,.3f,1};
                                            next->outlines.push_back(std::move(outline));
                                        }
                                    };
                                    if(phase=="ready") { if(header[5]=="1") add(*first,true); if(header[6]=="1") add(*second,false); }
                                }
                            }
                        }
                    }
                }
            }
        } catch(...) { phase="invalid_request"; next->outlines.clear(); }
        { std::lock_guard<std::mutex> lock(mutex); drawing=std::move(next); }
        try {
            auto path=directory/(L"state-highlight-"+std::to_wstring(process)+L".status"),temporary=path; temporary+=L".tmp";
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            output<<"#DEVIL_STATE_HIGHLIGHT_STATUS_V1\t"<<process<<'\t'<<now<<'\t'<<phase<<'\t'<<exact<<'\t'<<bounds<<'\t'<<missing<<'\n'; output.close();
            if(output) MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
        } catch(...) {}
    }
    void run() {
        const auto directory=researchDirectory(); const DWORD process=GetCurrentProcessId(); LevelStateReader reader; std::string seen,seenPoint;
        for(;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const GameProfile* selected=nullptr; { std::lock_guard<std::mutex> lock(mutex); selected=profile; }
            if(!selected||!labAppAvailable()) { std::lock_guard<std::mutex> lock(mutex); drawing.reset(); continue; }
            uint64_t now=encounterEpochMillis();
            try {
                auto path=directory/(L"research-point-"+std::to_wstring(process)+L".request"); std::error_code error;
                if(std::filesystem::file_size(path,error)<=256&&!error) {
                    std::ifstream input(path); std::string line;
                    if(std::getline(input,line)) {
                        auto request=cells(line);
                        if(request.size()==4&&request[0]=="#DEVIL_POINT_REQUEST_V1"&&request[1]==std::to_string(process)&&validStateToken(request[2])&&request[2]!=seenPoint) {
                            auto created=std::stoull(request[3]);
                            if(created<=now+5000&&created+10000>=now) {
                                seenPoint=request[2]; auto checkpoint=std::atomic_load(&checkpointMetadata);
                                auto value=captureBookmark(WallMemory{},reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*selected,seenPoint,checkpoint?*checkpoint:CheckpointLayout{},inspectorHud().selection());
                                writeBookmark(value,directory/(L"research-point-"+std::to_wstring(process)+L".tsv"));
                            }
                        }
                    }
                }
            } catch(...) { /* Capturing a bookmark never executes an engine command. */ }
            try {
                auto path=directory/(L"level-state-"+std::to_wstring(process)+L".request"); std::error_code error;
                if(std::filesystem::file_size(path,error)<=512&&!error) {
                    std::ifstream input(path); std::string line;
                    if(std::getline(input,line)) {
                        auto request=cells(line);
                        if(request.size()==5&&request[0]=="#DEVIL_STATE_REQUEST_V1"&&request[1]==std::to_string(process)&&validStateToken(request[2])&&
                            request[2]!=seen&&(request[3]=="before"||request[3]=="after")) {
                            auto created=std::stoull(request[4]);
                            if(created<=now+5000&&created+10000>=now) {
                                seen=request[2]; auto fields=std::atomic_load(&stateEncounterMetadata); auto checkpoint=std::atomic_load(&checkpointMetadata);
                                auto state=reader.capture(WallMemory{},reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*selected,seen,fields?*fields:EncounterFields{},checkpoint?*checkpoint:CheckpointLayout{});
                                auto output=directory/(L"level-state-"+std::to_wstring(process)+L"-"+std::wstring(request[3].begin(),request[3].end())+L".tsv");
                                if(writeLevelState(state,output)&&state.phase=="ready") {
                                    auto value=std::make_shared<LevelStateSnapshot>(std::move(state));
                                    std::lock_guard<std::mutex> lock(mutex); if(request[3]=="before") before=std::move(value); else after=std::move(value);
                                }
                            }
                        }
                    }
                }
            } catch(...) { /* Invalid/expired requests never become engine actions. */ }
            highlights(directory,process,*selected,encounterEpochMillis());
        }
    }
public:
    void connect() {
        if(!labAppAvailable()) return;
        std::lock_guard<std::mutex> lock(mutex); profile=currentGameProfile();
        if(!started&&profile) { started=true; std::thread([this] { run(); }).detach(); }
    }
    std::shared_ptr<const StateHighlight> snapshot() { std::lock_guard<std::mutex> lock(mutex); return drawing; }
};
static LevelStateBridge& levelStates() { static auto* bridge=new LevelStateBridge; return *bridge; }
static void appendStateHighlight(const GameCamera& camera,std::vector<Vertex>& output,const StateHighlight& state,const LevelIdentity& level,uint64_t now) {
    if(state.level!=level||state.expires<now) return;
    ViewFilter filter(camera);
    for(const auto& outline:state.outlines) {
        const auto& mesh=outline.mesh;
        if(!filter.inView(mesh.bounds)||(!state.ignoreDistance&&filter.distanceSquared(mesh.bounds)>cullingRadius()*cullingRadius())) continue;
        auto line=[&](Vec3 a,Vec3 b) {
            auto append=[&](Vec3 p) { Vertex v{}; v.position=p; memcpy(v.color,outline.color.data(),16); output.push_back(v); };
            if(!outline.dashed) { append(a); append(b); }
            else for(unsigned dash=0;dash<3;++dash) { float t=dash*.4f; auto p=[&](float s) { return motionAdd(a,motionScale(motionSub(b,a),s)); }; append(p(t)); append(p(t+.2f)); }
        };
        if(!outline.bounds&&mesh.geometry) {
            for(size_t i=0;i+1<mesh.geometry->lines.size();i+=2) line(mesh.world(mesh.geometry->lines[i]),mesh.world(mesh.geometry->lines[i+1]));
        } else for(unsigned corner=0;corner<8;++corner) for(unsigned bit:{1u,2u,4u}) if(!(corner&bit)) {
            auto p=[&](unsigned index) { return Vec3{mesh.bounds[index&1?3:0],mesh.bounds[index&2?4:1],mesh.bounds[index&4?5:2]}; }; line(p(corner),p(corner|bit));
        }
    }
}
