#pragma once
#include "practice_live.h"
static void updateSupportPlaneHud(const MeathookHudApi& api,const GameProfile* profile);

struct EncounterFields {
    InspectorField think,name,stringData,enabled,paused,spawnPaused,waitingExit,script,commands;
    explicit operator bool() const {
        return think&&think.size==4&&think.type=="int"&&name&&name.type.rfind("idStr",0)==0&&
            stringData&&stringData.size==8&&stringData.type.find("char")!=std::string::npos&&stringData.type.find('*')!=std::string::npos&&stringData.offset+8<=name.size;
    }
};
static std::shared_ptr<const EncounterFields> stateEncounterMetadata;
static EncounterFields resolveEncounterFields(const std::function<InspectorField(const std::string&,const char*)>& lookup) {
    EncounterFields fields;
    fields.think=lookup("idBloatedEntity","thinkFlags");
    fields.name=lookup("idEntity","name");
    fields.stringData=lookup("idStr","data");
    auto boolean=[&](const char* name) { auto field=lookup("idEncounterManager",name); return field.size==1&&field.type=="bool"?field:InspectorField{}; };
    fields.enabled=boolean("isEnabled"); fields.paused=boolean("isPaused");
    fields.spawnPaused=boolean("isPausedForAvailableSpawn"); fields.waitingExit=boolean("waitingForExitTrigger");
    fields.script=lookup("idEncounterManager","activeScriptIndex");
    if(fields.script.size!=4||fields.script.type!="int") fields.script={};
    fields.commands=lookup("idEncounterManager","activeScriptCommands");
    if(fields.commands.size!=24||fields.commands.type.rfind("idList",0)!=0) fields.commands={};
    return fields;
}
static EncounterFields resolveEncounterFields(const MeathookHudApi& api) {
    return resolveEncounterFields([&](const std::string& type,const char* field) { return inspectorField(api,type,field); });
}
struct ActiveEncounterRow {
    uint32_t id=0,generation=0;
    std::string name,label;
    bool known=false;
    int32_t think=0;
    int enabled=-1,paused=-1,spawnPaused=-1,waitingExit=-1,script=-1,commands=-1;
    bool active() const { return known&&think!=0; }
};
struct ActiveEncounterSnapshot {
    DWORD process=GetCurrentProcessId();
    uint64_t captured=0;
    uintptr_t map=0,world=0;
    std::string phase="waiting_meathook",level;
    uint32_t unreadable=0;
    std::vector<ActiveEncounterRow> rows;
};
static uint64_t encounterEpochMillis() {
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    return ((uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime)/10000-11644473600000ULL;
}
class ActiveEncounterReader {
    std::unordered_map<uintptr_t,std::string> classes;
public:
    ActiveEncounterSnapshot capture(const WallMemory& memory,uintptr_t base,const GameProfile& profile,
                                    const LevelIdentity& level,const EncounterFields& fields) {
        ActiveEncounterSnapshot result; result.captured=encounterEpochMillis(); result.level=level.name; result.map=level.map; result.world=level.world;
        if(!fields) { result.phase="metadata_unavailable"; return result; }
        uintptr_t system=0,map=0,list=0; uint32_t count=0;
        if(!level||!memory.get(base+profile.gameSystemRva,system)||!memory.get(system+0x50,map)||map!=level.map) { result.phase="loading"; return result; }
        if(!memory.get(map+profile.mapEntitiesOffset,list)||!memory.get(map+profile.mapEntitiesOffset+8,count)||count>32768) { result.phase="read_error"; return result; }
        struct Entry { uint32_t id,generation; uintptr_t entity,previous,next; };
        std::vector<Entry> entries(count);
        if(count&&!memory.read(list,entries.data(),entries.size()*sizeof(Entry))) { result.phase="read_error"; return result; }
        result.phase="live";
        for(size_t i=0;i<entries.size();++i) {
            const auto& entry=entries[i]; if(!entry.entity) continue;
            uintptr_t vtable=0;
            if(!memory.get(entry.entity,vtable)) { ++result.unreadable; continue; }
            auto found=classes.find(vtable);
            if(found==classes.end()) {
                auto type=readEntityClass(memory,vtable,base,profile);
                if(type.empty()) { ++result.unreadable; continue; }
                found=classes.emplace(vtable,std::move(type)).first;
            }
            if(found->second!="idEncounterManager") continue;
            unsigned char removed=1; Entry current{}; uintptr_t currentVtable=0;
            if(!memory.get(entry.entity+0xa4,removed)) { ++result.unreadable; continue; }
            if(removed) continue;
            auto valid=[&] {
                return memory.get(list+i*sizeof(Entry),current)&&current.id==entry.id&&current.generation==entry.generation&&
                    current.entity==entry.entity&&memory.get(entry.entity+0xa4,removed)&&!removed&&memory.get(entry.entity,currentVtable)&&currentVtable==vtable;
            };
            if(!valid()) { ++result.unreadable; continue; }
            ActiveEncounterRow row; row.id=entry.id; row.generation=entry.generation;
            row.known=memory.get(entry.entity+fields.think.offset,row.think);
            auto boolean=[&](const InspectorField& field) {
                unsigned char value=2; return field&&memory.get(entry.entity+field.offset,value)&&value<=1?int(value):-1;
            };
            row.enabled=boolean(fields.enabled); row.paused=boolean(fields.paused);
            row.spawnPaused=boolean(fields.spawnPaused); row.waitingExit=boolean(fields.waitingExit);
            if(fields.script&&!memory.get(entry.entity+fields.script.offset,row.script)) row.script=-1;
            uint32_t commands=0;
            if(fields.commands&&memory.get(entry.entity+fields.commands.offset+8,commands)&&commands<=65536) row.commands=static_cast<int>(commands);
            uintptr_t name=0;
            if(memory.get(entry.entity+fields.name.offset+fields.stringData.offset,name)) row.name=readEntityText(memory,name,256);
            std::string label=encounterLabel(level.name,row.name);
            if(label.rfind("Encounter: ",0)==0) row.label=label.substr(11);
            int32_t check=0;
            if(!valid()) { ++result.unreadable; continue; }
            if(!memory.get(entry.entity+fields.think.offset,check)||check!=row.think) row.known=false;
            if(!row.known) ++result.unreadable;
            result.rows.push_back(std::move(row));
            if(result.rows.size()>2048) { result.rows.clear(); result.phase="read_error"; return result; }
        }
        uintptr_t finalMap=0,finalList=0; uint32_t finalCount=0;
        if(!memory.get(system+0x50,finalMap)||finalMap!=map||!memory.get(map+profile.mapEntitiesOffset,finalList)||finalList!=list||
           !memory.get(map+profile.mapEntitiesOffset+8,finalCount)||finalCount!=count) { result.rows.clear(); result.phase="loading"; }
        return result;
    }
};
static bool writeActiveEncounters(const ActiveEncounterSnapshot& snapshot,const std::filesystem::path& directory) {
    if(directory.empty()) return false;
    auto clean=[](std::string text) { for(char& c:text) if(static_cast<unsigned char>(c)<32||c==127) c=' '; return text; };
    try {
        std::filesystem::create_directories(directory);
        auto path=directory/(L"active-encounters-"+std::to_wstring(snapshot.process)+L".tsv"),temporary=path; temporary+=L".tmp";
        {
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            output.imbue(std::locale::classic());
            output<<"#DEVIL_ENCOUNTERS_V1\t"<<snapshot.process<<'\t'<<snapshot.captured<<'\t'<<snapshot.phase<<'\t'<<clean(snapshot.level)<<'\t'<<snapshot.unreadable<<'\n';
            output<<"id\tgeneration\tmanager\tlabel\tthinkFlags\tenabled\tpaused\tspawnPaused\twaitingExit\tscript\tcommands\n";
            for(const auto& row:snapshot.rows) {
                output<<row.id<<'\t'<<row.generation<<'\t'<<clean(row.name)<<'\t'<<clean(row.label)<<'\t';
                if(row.known) output<<row.think; else output<<'?';
                output<<'\t'<<row.enabled<<'\t'<<row.paused<<'\t'<<row.spawnPaused<<'\t'<<row.waitingExit<<'\t'<<row.script<<'\t'<<row.commands<<'\n';
            }
            output.close(); if(!output) return false;
        }
        return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)!=0;
    } catch(...) { return false; }
}
class ActiveEncounterHud {
    const MeathookHudApi* api=nullptr;
    std::array<std::string,14> previous{};
    bool visible=false,requested=false;
    ULONGLONG formattedAt=0;
    static constexpr unsigned x=24,y=32,height=16;
public:
    explicit ActiveEncounterHud(const MeathookHudApi* backend=nullptr):api(backend) {}
    void bind(const MeathookHudApi* backend) { api=backend; }
    void clear() {
        if(visible&&api&&api->removeText) for(unsigned i=0;i<previous.size();++i) api->removeText(x,y+i*height);
        previous={}; visible=false; requested=false; formattedAt=0;
    }
    void update(const ActiveEncounterSnapshot* snapshot,bool wanted,bool playing,uint64_t now) {
        if(!wanted||!playing||!api||!api->executeCommand||!api->addText||!api->removeText) { clear(); return; }
        if(visible&&now>=formattedAt&&now-formattedAt<200) return;
        formattedAt=now;
        if(!requested) { api->executeCommand("com_debugHUD 1"); requested=true; }
        std::array<std::string,14> lines{};
        lines[0]="DEViL | Active encounters";
        if(!snapshot||now>snapshot->captured+2000||snapshot->captured>now+5000) lines[1]="Waiting for fresh encounter data";
        else if(snapshot->phase!="live"&&snapshot->phase!="paused") lines[1]="State: "+snapshot->phase;
        else {
            unsigned active=0,shown=0;
            for(const auto& row:snapshot->rows) if(row.active()) {
                ++active;
                if(shown>=5) continue;
                std::string state=row.paused==1?" [paused]":row.spawnPaused==1?" [spawn pause]":row.waitingExit==1?" [waiting exit]":"";
                lines[2+shown*2]=std::to_string(shown+1)+". "+(row.label.empty()?row.name:row.label)+state;
                lines[3+shown*2]="   script "+std::to_string(row.script)+" | ID "+std::to_string(row.id)+" | "+row.name;
                ++shown;
            }
            lines[1]=std::string(snapshot->unreadable?"Known active managers: ":"Active managers: ")+std::to_string(active);
            if(active>shown) lines[12]="+"+std::to_string(active-shown)+" more in DEVIL / lab_encounters_list";
            if(snapshot->unreadable) lines[13]="Unavailable reads: "+std::to_string(snapshot->unreadable);
        }
        for(unsigned i=0;i<lines.size();++i) {
            for(char& c:lines[i]) if(c=='%'||c=='^'||static_cast<unsigned char>(c)<32) c='?';
            if(lines[i].size()>88) lines[i]=lines[i].substr(0,85)+"...";
            if(visible&&lines[i]==previous[i]) continue;
            if(lines[i].empty()) api->removeText(x,y+i*height);
            else api->addText(x,y+i*height,i?0xffeeeeeeu:0xffffd040u,.72f,"%s",lines[i].c_str());
        }
        previous=std::move(lines); visible=true;
    }
};
class ActiveEncounterBridge {
    const MeathookHudApi* api=nullptr;
    std::mutex exchange;
    EncounterFields fields;
    std::shared_ptr<const ActiveEncounterSnapshot> latest;
    ActiveEncounterHud hud;
    const GameProfile* profile=nullptr;
    enum class Phase { Meathook, Callback, Metadata, Ready };
    std::atomic<Phase> phase{Phase::Meathook};
    bool registered=false,workerStarted=false;
    ULONGLONG retryAt=0,fieldsRetryAt=0;
    static void callback(void* self) { static_cast<ActiveEncounterBridge*>(self)->onFrame(); }
    void onFrame() {
        practiceLiveBridge.update(profile);
        updateSupportPlaneHud(*api,profile);
        if(!labAppAvailable()) { hud.clear(); return; }
        if(phase.load()!=Phase::Ready&&GetTickCount64()>=fieldsRetryAt) {
            fieldsRetryAt=GetTickCount64()+2000;
            auto resolved=resolveEncounterFields(*api);
            std::atomic_store(&stateEncounterMetadata,std::make_shared<const EncounterFields>(resolved));
            auto checkpoint=resolveCheckpointLayout([&](const std::string& type,const char* field) { return inspectorField(*api,type,field); });
            std::atomic_store(&checkpointMetadata,std::make_shared<const CheckpointLayout>(checkpoint));
            auto paths=resolveTriggerPathLayout([&](const std::string& type,const char* field) { return inspectorField(*api,type,field); });
            std::atomic_store(&triggerPathMetadata,std::make_shared<const TriggerPathLayout>(paths));
            auto motion=resolveMotionLayout([&](const std::string& type,const char* field) { return inspectorField(*api,type,field); });
            std::atomic_store(&motionMetadata,std::make_shared<const MotionLayout>(motion));
            { std::lock_guard<std::mutex> lock(exchange); fields=resolved; }
            phase=resolved?Phase::Ready:Phase::Metadata;
        }
        bool wanted=showActiveEncountersHud.load();
        bool playing=wanted&&profile&&gameplayVisible(WallMemory{},reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile)&&
            readConsoleFraction(WallMemory{},reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile)<=0;
        auto current=snapshot();
        if(playing&&current&&(current->phase=="live"||current->phase=="paused")) {
            auto level=readLevelIdentity(WallMemory{},reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile);
            if(level.map!=current->map||level.world!=current->world||level.name!=current->level) current.reset();
        }
        hud.update(current.get(),wanted,playing,encounterEpochMillis());
    }
    void run() {
        ActiveEncounterReader reader; uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto directory=researchDirectory();
        for(;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if(!labAppAvailable()) continue;
            ActiveEncounterSnapshot snapshot; snapshot.captured=encounterEpochMillis();
            try {
                auto state=phase.load(); EncounterFields layout;
                { std::lock_guard<std::mutex> lock(exchange); layout=fields; }
                if(!profile) snapshot.phase="unsupported_profile";
                else if(state!=Phase::Ready) snapshot.phase=state==Phase::Meathook?"waiting_meathook":state==Phase::Callback?"waiting_callback":"metadata_unavailable";
                else {
                    auto level=readLevelIdentity(WallMemory{},base,*profile);
                    if(!level) snapshot.phase="loading";
                    else {
                        snapshot=reader.capture(WallMemory{},base,*profile,level,layout);
                        if(readLevelIdentity(WallMemory{},base,*profile)!=level) { snapshot.phase="loading"; snapshot.rows.clear(); }
                        else if(snapshot.phase=="live"&&!gameplayVisible(WallMemory{},base,*profile)) snapshot.phase="paused";
                    }
                }
            } catch(...) { snapshot.phase="read_error"; snapshot.rows.clear(); }
            { std::lock_guard<std::mutex> lock(exchange); latest=std::make_shared<ActiveEncounterSnapshot>(snapshot); }
            writeActiveEncounters(snapshot,directory);
        }
    }
public:
    std::shared_ptr<const ActiveEncounterSnapshot> snapshot() { std::lock_guard<std::mutex> lock(exchange); return latest; }
    void connect() {
        if(!labAppAvailable()) return;
        HMODULE pinned=nullptr;
        if(!workerStarted) {
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&callback),&pinned)) return;
            profile=currentGameProfile(); workerStarted=true; std::thread([this] { run(); }).detach();
        }
        if(registered||GetTickCount64()<retryAt) return;
        retryAt=GetTickCount64()+2000;
        HMODULE module=GetModuleHandleW(L"XINPUT1_3.dll"); if(!module) return;
        auto getter=reinterpret_cast<const MeathookHudApi* (*)()>(GetProcAddress(module,"mh_api_get")); if(!getter) return;
        const auto* candidate=getter(); MeathookHudApi copy{};
        if(!candidate||!WallMemory{}.get(reinterpret_cast<uintptr_t>(candidate),copy)||!copy.findClass||!copy.registerPreFrame) return;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(getter),&pinned)) return;
        api=candidate; hud.bind(api); registered=true; phase=Phase::Callback; api->registerPreFrame(callback,this);
        note("Active encounters: meathook metadata callback registered; background snapshots update every 250ms.");
    }
};
static ActiveEncounterBridge& activeEncounters() { static auto* bridge=new ActiveEncounterBridge; return *bridge; }
static void printActiveEncounters(const MeathookHudApi& api) {
    auto snapshot=activeEncounters().snapshot();
    if(!snapshot||encounterEpochMillis()>snapshot->captured+2000) { api.consolePrint("%s","lab_encounters: waiting for a fresh snapshot.\n"); return; }
    if(snapshot->phase!="live"&&snapshot->phase!="paused") { api.consolePrint("lab_encounters: %s\n",snapshot->phase.c_str()); return; }
    unsigned active=0;
    for(const auto& row:snapshot->rows) if(row.active()) {
        if(++active<=128) api.consolePrint("%s | %s | script %d\n",row.label.empty()?"<no reference name>":row.label.c_str(),row.name.empty()?"<name unavailable>":row.name.c_str(),row.script);
    }
    api.consolePrint("Active managers: %u (meathook thinkFlags criterion); unreadable: %u\n",active,snapshot->unreadable);
}
