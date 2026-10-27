#include "../src/vulkan_layer.cpp"
#include <cstdarg>
static void expect(bool ok,const char* text) { if(!ok) { fprintf(stderr,"FAIL: %s\n",text); exit(1); } }
template<class T> static void put(void* memory,size_t offset,T value) { memcpy(static_cast<unsigned char*>(memory)+offset,&value,sizeof(value)); }
static unsigned diagnostics=0,commands=0;
static void print(const char*,...) { ++diagnostics; }
static void execute(const char*) { ++commands; }
struct EngineFixture {
    std::unordered_map<std::string,uintptr_t> names;
    std::unordered_map<std::string,std::string> values;
    OwnedLabCvar debug;
    unsigned creates=0,writes=0;
    bool registerWorks=true,writeWorks=true;
    EngineFixture() {
        const char* name="com_debugHUD"; auto variable=reinterpret_cast<uintptr_t>(debug.object);
        put(debug.object,0,variable+8); put(debug.object,8+40,reinterpret_cast<uintptr_t>(name)); names[name]=variable; change(name,0);
    }
    void change(const char* name,int value,const char* text=nullptr) {
        auto variable=names.at(name); values[name]=text?text:std::to_string(value);
        auto* data=reinterpret_cast<void*>(variable+8);
        put(data,0,reinterpret_cast<uintptr_t>(values[name].c_str())); put(data,8,value);
    }
    int number(const char* name) { int value=-1; std::string text; expect(readLabCvarValue(WallMemory{},names.at(name),name,value,text),"fixture value ABI"); return value; }
    LabCvarBackend backend() {
        return {
            [this](const char* name) { auto found=names.find(name); return found==names.end()?uintptr_t(0):found->second; },
            [this](OwnedLabCvar& storage,const LabCvarDefinition& definition) {
                ++creates; if(!registerWorks) return;
                auto variable=reinterpret_cast<uintptr_t>(storage.object);
                put(storage.object,0,variable+8); put(storage.object,8+40,reinterpret_cast<uintptr_t>(definition.name));
                names[definition.name]=variable; change(definition.name,atoi(storage.initial));
            },
            [this](const char* name,const char* value) { ++writes; if(writeWorks) change(name,atoi(value),value); }
        };
    }
};
int main() {
    MeathookHudApi api{}; api.consolePrint=print; api.executeCommand=execute;
    auto temporary=std::filesystem::temp_directory_path()/(L"DEViL-Cvar-Test-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(temporary); auto ini=(temporary/L"viewer.ini").wstring();
    auto setSaved=[&](const wchar_t* key,const wchar_t* value) { expect(WritePrivateProfileStringW(L"Viewer",key,value,ini.c_str())!=0,"isolated saved setting"); };
    setSaved(L"ShowResearchPath",L"1"); setSaved(L"Distance",L"75");
    EngineFixture engine; LabCvarBridge bridge; std::vector<std::vector<std::string>> requests;
    expect(bridge.initializeBackend(api,engine.backend(),ini,[&](const auto& request) { requests.push_back(request); return true; }),"native creation registers all owned variables");
    expect(engine.creates==std::size(labCvarDefinitions)&&engine.number("lab_path_show")==1&&engine.number("lab_viewer_distance")==75&&
        engine.number("lab_player_show")==0&&engine.number("lab_attempts_show")==1&&commands==0,"initialization uses saved values and sends no console commands");
    for(ULONGLONG time=100;time<1000;time+=100) bridge.poll(api,true,time);
    expect(engine.writes==0&&requests.empty(),"unchanged settings cause no writes or console spam");
    setSaved(L"ShowResearchPath",L"0"); bridge.poll(api,true,1000);
    expect(engine.number("lab_path_show")==0&&engine.writes==1&&requests.empty(),"UI changes update native cvars quietly");
    engine.change("lab_path_show",1); bridge.poll(api,true,1100);
    expect(requests.size()==1&&requests.back()==std::vector<std::string>{"lab_path","1"},"console cvar changes reach the injected companion transport");
    setSaved(L"ShowResearchPath",L"1"); bridge.poll(api,true,1200); unsigned acknowledged=engine.writes;
    bridge.poll(api,true,4000); expect(engine.writes==acknowledged,"acknowledged changes do not time out or repeat");
    engine.change("lab_path_show",0); bridge.poll(api,false,4100);
    expect(engine.number("lab_path_show")==1&&requests.size()==1,"closed companion changes are restored without sending a request");
    engine.change("lab_path_show",0); bridge.poll(api,true,4200); // reopening restores saved values
    expect(engine.number("lab_path_show")==1,"reopening restores saved state");
    engine.change("lab_path_show",0); bridge.poll(api,true,4300); bridge.poll(api,true,6500);
    expect(engine.number("lab_path_show")==1,"unacknowledged console changes are restored after timeout");
    engine.writeWorks=false; setSaved(L"ShowResearchPath",L"0"); bridge.poll(api,true,6600);
    expect(bridge.blocked()&&!bridge.ready(),"failed native writes block further synchronization");
    unsigned failedWrites=engine.writes,failedDiagnostics=diagnostics;
    for(ULONGLONG time=6700;time<10000;time+=100) bridge.poll(api,true,time);
    expect(engine.writes==failedWrites&&diagnostics==failedDiagnostics&&commands==0,"failed updates are not retried on every poll");

    EngineFixture rejected; rejected.registerWorks=false; LabCvarBridge failed;
    auto ignore=[](const auto&) { return false; };
    expect(!failed.initializeBackend(api,rejected.backend(),ini,ignore)&&failed.blocked()&&rejected.creates==1,"unregistered variables stop initialization immediately");
    auto initialDiagnostics=diagnostics;
    for(unsigned attempt=0;attempt<50;++attempt) failed.initializeBackend(api,rejected.backend(),ini,ignore);
    expect(rejected.creates==1&&diagnostics==initialDiagnostics&&commands==0,"registration failure produces one diagnostic and no retry storm");
    EngineFixture foreign; foreign.names["lab_path_show"]=foreign.names["com_debugHUD"]; LabCvarBridge collision;
    expect(!collision.initializeBackend(api,foreign.backend(),ini,ignore)&&collision.blocked()&&foreign.creates==0,"foreign names are detected before creating or changing any cvar");

    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x744d000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); expect(block!=nullptr,"ABI fixture allocation");
    uintptr_t base=reinterpret_cast<uintptr_t>(block); LabCvarSystem system{base+0x800,base+0x178e160};
    put(block,0x800,base+0x1000); put(block,0x1108,base+0x178f470); put(block,0x1038,base+0x178f8e0);
    memcpy(block+0x371100,labCvarConstructorPrefix,sizeof(labCvarConstructorPrefix));
    memcpy(block+0x178f470,labCvarRegisterPrefix,sizeof(labCvarRegisterPrefix));
    memcpy(block+0x178f8e0,labCvarSetPrefix,sizeof(labCvarSetPrefix));
    DWORD old=0; VirtualProtect(block+0x371000,4096,PAGE_EXECUTE_READWRITE,&old); VirtualProtect(block+0x178f000,4096,PAGE_EXECUTE_READWRITE,&old);
    GameProfile profile=*kGameProfiles[20]; profile.timestamp=0x669f0dcf; profile.imageSize=0x744d000;
    expect(bool(resolveLabCvarFactory(WallMemory{},system,base,profile)),"audited constructor, register and setter code resolves");
    block[0x371100]^=1; expect(!resolveLabCvarFactory(WallMemory{},system,base,profile),"changed constructor fails closed"); block[0x371100]^=1;
    block[0x178f8e0]^=1; expect(!resolveLabCvarFactory(WallMemory{},system,base,profile),"changed setter fails closed"); block[0x178f8e0]^=1;
    put(block,0x1108,base+0x178f480); expect(!resolveLabCvarFactory(WallMemory{},system,base,profile),"foreign registration method is rejected"); put(block,0x1108,base+0x178f470);
    VirtualProtect(block+0x371000,4096,PAGE_READWRITE,&old); expect(!resolveLabCvarFactory(WallMemory{},system,base,profile),"nonexecutable constructor is rejected");
    for(const auto* other:kGameProfiles) if(other->timestamp!=profile.timestamp)
        expect(!resolveLabCvarFactory(WallMemory{},system,base,*other),"unaudited versions never call a guessed constructor");
    VirtualFree(block,0,MEM_RELEASE); std::filesystem::remove_all(temporary);
    puts("PASS: native cvar creation/sync, quiet idle, UI/console changes, acknowledgment, close/reopen, timeout, foreign ownership, failed registration/update, and guarded 6.66 Rev 3 ABI. No engine commands executed.");
}
