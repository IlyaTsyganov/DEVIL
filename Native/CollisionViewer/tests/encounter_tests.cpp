#include "../src/vulkan_layer.cpp"
#include <cstdarg>
static std::unordered_map<unsigned,std::string> hudRows;
static unsigned hudVisibilityRequests=0;
static void hudExecute(const char* text) { if(strcmp(text,"com_debugHUD 1")) exit(2); ++hudVisibilityRequests; }
static void hudAdd(unsigned,unsigned y,unsigned,float,const char* format,...) {
    char text[1024]; va_list args; va_start(args,format); vsprintf_s(text,format,args); va_end(args); hudRows[y]=text;
}
static void hudRemove(unsigned,unsigned y) { hudRows.erase(y); }
static void expect(bool condition,const char* message) { if(!condition) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
template<class T> static void put(void* memory,size_t offset,T value) { memcpy(static_cast<unsigned char*>(memory)+offset,&value,sizeof(value)); }
int main() {
    unsigned variant=0;
    for(const auto* selected:kGameProfiles) {
        const auto& profile=*selected;
        auto* base=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x7000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        expect(base!=nullptr,"fixture allocation"); uintptr_t b=reinterpret_cast<uintptr_t>(base);
        std::vector<unsigned char> system(0x60),map(profile.mapEntitiesOffset+16);
        std::array<std::array<unsigned char,2048>,4> managers{};
        struct Entry { uint32_t id,generation; uintptr_t entity,previous,next; };
        std::array<Entry,4> entries{};
        const char* names[]{"starting_platform_encounter_manager_1","unlisted_manager","starting_platform_encounter_manager_3","removed_manager"};
        const char* managerClass="idEncounterManager";
        const unsigned char getter[]{0x48,0x8d,0x05,0,0,0,0,0xc3};
        memcpy(base+0x210000,getter,8); put(base,0x210003,int32_t(0x220000-0x210007));
        put(base,0x200010,b+0x210000); put(base,0x220000,reinterpret_cast<uintptr_t>(managerClass));
        put(base,profile.gameSystemRva,reinterpret_cast<uintptr_t>(system.data()));
        put(system.data(),0x50,reinterpret_cast<uintptr_t>(map.data()));
        put(map.data(),profile.mapEntitiesOffset,reinterpret_cast<uintptr_t>(entries.data())); put(map.data(),profile.mapEntitiesOffset+8,uint32_t(4));
        EncounterFields fields;
        fields.think={0x200+4*variant++,4,"int"}; fields.name={0x40,48,"idStr"}; fields.stringData={8,8,"char*"};
        fields.enabled={0x300,1,"bool"}; fields.paused={0x301,1,"bool"}; fields.spawnPaused={0x302,1,"bool"}; fields.waitingExit={0x303,1,"bool"};
        fields.script={0x308,4,"int"}; fields.commands={0x310,24,"idList < idEncounterEvent * >"};
        for(unsigned i=0;i<4;++i) {
            entries[i]={100+i,3,reinterpret_cast<uintptr_t>(managers[i].data()),0,0};
            put(managers[i].data(),0,b+0x200000); put(managers[i].data(),0x48,reinterpret_cast<uintptr_t>(names[i]));
            put(managers[i].data(),fields.think.offset,int32_t(i==1?0:i+1)); put(managers[i].data(),0x300,uint8_t(1));
            put(managers[i].data(),0x308,int32_t(i)); put(managers[i].data(),0x318,uint32_t(i+1));
        }
        managers[2][0x301]=1; managers[3][0xa4]=1;
        LevelIdentity level{reinterpret_cast<uintptr_t>(map.data()),1,"game/dlc/e4m1_rig/e4m1_rig"};
        ActiveEncounterReader reader; auto snapshot=reader.capture(WallMemory{},b,profile,level,fields);
        expect(snapshot.phase=="live"&&snapshot.rows.size()==3&&snapshot.unreadable==0,"live list excludes removed managers without pretending they are read errors");
        expect(snapshot.rows[0].active()&&!snapshot.rows[1].active()&&snapshot.rows[2].active()&&snapshot.rows[2].paused==1,"active, inactive and paused managers use reflected state");
        expect(snapshot.rows[0].label=="UAC Atlantica / Combat 1 Landing Pad 1"&&snapshot.rows[0].name==names[0],"exact human and technical encounter names are both retained");
        expect(snapshot.rows[1].label.empty(),"unknown managers do not receive guessed names");
        expect(snapshot.rows[0].script==0&&snapshot.rows[2].commands==3,"script index and active command count are sampled");
        entries[0].generation=9; snapshot=reader.capture(WallMemory{},b,profile,level,fields);
        expect(snapshot.rows[0].generation==9,"reused slots carry the current generation");
        auto otherLevel=level; otherLevel.name="game/master/e4m1_rig/e4m1_rig";
        auto master=reader.capture(WallMemory{},b,profile,otherLevel,fields); expect(master.rows[0].label.empty(),"campaign reference names never leak into master levels");
        auto* badManager=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        memcpy(badManager,managers[0].data(),2048); DWORD oldProtection=0;
        expect(VirtualProtect(badManager+4096,4096,PAGE_NOACCESS,&oldProtection)!=0,"unreadable state fixture");
        auto savedEntry=entries[0]; entries[0].entity=reinterpret_cast<uintptr_t>(badManager);
        put(map.data(),profile.mapEntitiesOffset+8,uint32_t(1));
        auto unavailable=fields; unavailable.think.offset=4096;
        auto unreadable=reader.capture(WallMemory{},b,profile,level,unavailable);
        expect(unreadable.rows.size()==1&&unreadable.unreadable==1&&!unreadable.rows[0].known&&!unreadable.rows[0].active(),"unreadable states are distinct from inactive managers");
        entries[0]=savedEntry; put(map.data(),profile.mapEntitiesOffset+8,uint32_t(4)); VirtualFree(badManager,0,MEM_RELEASE);
        auto missing=fields; missing.think.type="float";
        expect(reader.capture(WallMemory{},b,profile,level,missing).phase=="metadata_unavailable","unsupported metadata does not look like an empty active list");
        put(map.data(),profile.mapEntitiesOffset+8,uint32_t(32769));
        expect(reader.capture(WallMemory{},b,profile,level,fields).phase=="read_error","invalid entity table is reported as unavailable");
        put(map.data(),profile.mapEntitiesOffset+8,uint32_t(0));
        auto empty=reader.capture(WallMemory{},b,profile,level,fields); expect(empty.phase=="live"&&empty.rows.empty(),"confirmed empty maps remain a valid empty list");
        put(system.data(),0x50,uintptr_t(0)); expect(reader.capture(WallMemory{},b,profile,level,fields).phase=="loading","map transitions remove old values");
        auto temporary=std::filesystem::temp_directory_path()/(L"DEViL-Encounters-Test-"+std::to_wstring(GetCurrentProcessId()));
        expect(writeActiveEncounters(snapshot,temporary),"snapshot publication succeeds");
        auto file=temporary/(L"active-encounters-"+std::to_wstring(GetCurrentProcessId())+L".tsv");
        std::ifstream input(file); std::string text((std::istreambuf_iterator<char>(input)),{}); input.close();
        expect(text.find("#DEVIL_ENCOUNTERS_V1\t")==0&&text.find("UAC Atlantica / Combat 1 Landing Pad 1")!=std::string::npos,"published snapshot contains scoped labels and versioned metadata");
        expect(std::filesystem::equivalent(temporary.parent_path(),std::filesystem::temp_directory_path()),"test cleanup stays in its named temporary directory");
        std::filesystem::remove_all(temporary); VirtualFree(base,0,MEM_RELEASE);
    }
    expect(validLabArguments({"lab_encounters_list"})&&!validLabArguments({"lab_encounters_list","1"}),"list action accepts no state-changing arguments");
    expect(normalizeLabArguments({"lab_recording_start"})==std::vector<std::string>{"lab_record","1"}&&
           normalizeLabArguments({"lab_recording_stop"})==std::vector<std::string>{"lab_record","0"},"recording actions keep their explicit start/stop names");
    expect(!validLabArguments({"lab_encounters_show","1"}),"native engine cvars are not registered as duplicate commands");
    MeathookHudApi api{}; api.executeCommand=hudExecute; api.addText=hudAdd; api.removeText=hudRemove;
    ActiveEncounterHud hud(&api); ActiveEncounterSnapshot snapshot; snapshot.phase="live"; snapshot.captured=1000;
    ActiveEncounterRow row; row.known=true; row.think=1; row.id=1930; row.name="starting_platform_encounter_manager_1"; row.label="UAC Atlantica / Combat 1 Landing Pad 1";
    snapshot.rows.push_back(row); hudRows[280]="Inspector text";
    hud.update(&snapshot,true,true,1000);
    expect(hudVisibilityRequests==1&&hudRows[64].find("Landing Pad 1")!=std::string::npos,"native HUD enables com_debugHUD and displays the human encounter name");
    hud.update(&snapshot,true,true,1300); expect(hudVisibilityRequests==1,"HUD visibility cvar is requested once per visible interval");
    hud.update(&snapshot,true,true,4000); expect(hudRows[48].find("fresh")!=std::string::npos&&hudRows.count(64)==0,"stale encounter names are removed from the HUD");
    hud.update(&snapshot,false,true,4100); expect(hudRows.size()==1&&hudRows[280]=="Inspector text","hiding encounters removes only their own native HUD rows");
    expect(std::size(labCvarDefinitions)==14,"state preferences including collider, surface HUD and attempt overlays are native cvars");
    std::array<unsigned char,96> variable{},data{}; const char* cvarName="lab_encounters_show"; const char* value="1";
    put(variable.data(),0,reinterpret_cast<uintptr_t>(data.data())); put(data.data(),40,reinterpret_cast<uintptr_t>(cvarName));
    put(data.data(),8,int32_t(1)); put(data.data(),0,reinterpret_cast<uintptr_t>(value)); int numeric=0; std::string string;
    expect(readLabCvarValue(WallMemory{},reinterpret_cast<uintptr_t>(variable.data()),cvarName,numeric,string)&&numeric==1&&string=="1","native cvar value ABI validates both name and value");
    expect(!readLabCvarValue(WallMemory{},reinterpret_cast<uintptr_t>(variable.data()),"another_variable",numeric,string),"foreign cvar objects cannot control lab settings");
    puts("PASS: active/inactive/paused managers, exact scoped aliases, unknown names, script counters, slot reuse, unreadable vs empty states and snapshot publication on all 22 profiles.");
}
