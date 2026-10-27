#pragma once
static void printActiveEncounters(const MeathookHudApi& api);

struct LabCommandDefinition { const char* name; const char* usage; bool legacy=false; };
static constexpr LabCommandDefinition labCommandDefinitions[]{
    {"lab_commands_list","List DEViL cvars and action commands"},
    {"lab_encounters_list","List active encounter managers with reference names"},
    {"lab_recording_start","Start/resume research recording"}, {"lab_recording_stop","Stop research recording"},
    {"lab_recording_clear","Start a new research session"}, {"lab_markers_add","<text> - add a research marker"},
    {"lab_trigger_set","<class> 0|1 - one exact trigger class"},
    {"lab_help","Legacy alias",true}, {"lab_encounters","Legacy list / 0|1 HUD alias",true},
    {"lab_viewer","Legacy alias",true}, {"lab_triggers","Legacy alias",true},
    {"lab_collision","Legacy alias",true}, {"lab_walls","Legacy alias",true},
    {"lab_inspector","Legacy alias",true}, {"lab_disabled","Legacy alias",true},
    {"lab_history","Legacy alias",true}, {"lab_fill","Legacy alias",true},
    {"lab_distance","Legacy alias",true}, {"lab_trigger","Legacy alias",true},
    {"lab_path","Legacy alias",true}, {"lab_record","Legacy alias",true},
    {"lab_player","Player collider alias",true}, {"lab_attempts","Attempt overlay alias",true},
    {"lab_surface","Support plane HUD alias",true},
    {"lab_clear","Legacy alias",true}, {"lab_mark","Legacy alias",true}
};
static std::vector<std::string> normalizeLabArguments(const std::vector<std::string>& args) {
    if(args.empty()) return {};
    const auto& name=args[0];
    for(const auto& alias:{std::pair<const char*,const char*>{"lab_commands_list","lab_help"},
        {"lab_encounters_list","lab_encounters"},{"lab_recording_clear","lab_clear"}})
        if(name==alias.first) return args.size()==1?std::vector<std::string>{alias.second}:std::vector<std::string>{};
    if(name=="lab_recording_start"||name=="lab_recording_stop") return args.size()==1?
        std::vector<std::string>{"lab_record",name=="lab_recording_start"?"1":"0"}:std::vector<std::string>{};
    if(name=="lab_markers_add"||name=="lab_trigger_set") {
        auto result=args; result[0]=name=="lab_markers_add"?"lab_mark":"lab_trigger"; return result;
    }
    return args;
}
static bool validLabArguments(const std::vector<std::string>& input) {
    auto args=normalizeLabArguments(input);
    if(args.empty()) return false;
    const std::string& command=args[0];
    if(command=="lab_help"||command=="lab_clear") return args.size()==1;
    if(command=="lab_encounters") return args.size()==1||(args.size()==2&&(args[1]=="0"||args[1]=="1"));
    if(command=="lab_mark") return args.size()>1;
    if(command=="lab_trigger") return args.size()==3&&validTriggerClassName(args[1])&&(args[2]=="0"||args[2]=="1");
    if(args.size()!=2) return false;
    if(command=="lab_distance") {
        if(args[1].empty()||args[1].size()>3||!std::all_of(args[1].begin(),args[1].end(),[](char c) { return c>='0'&&c<='9'; })) return false;
        int value=atoi(args[1].c_str()); return value>=20&&value<=200;
    }
    if(args[1]!="0"&&args[1]!="1") return false;
    for(const auto& definition:labCommandDefinitions) if(definition.legacy&&command==definition.name) return true;
    return false;
}
static bool labAppAvailable(const wchar_t* name=L"Local\\DEViL.Lab.Active") {
    HANDLE presence=OpenEventW(SYNCHRONIZE,FALSE,name);
    if(!presence) return false;
    bool available=WaitForSingleObject(presence,0)==WAIT_OBJECT_0; CloseHandle(presence); return available;
}
static bool sendLabRequest(const std::vector<std::string>& args) {
    if(!labAppAvailable()) return false;
    std::string request=std::to_string(GetCurrentProcessId());
    for(const auto& arg:args) { request+='\t'; request+=arg; }
    request+='\n'; if(request.size()>2000) return false;
    HANDLE pipe=CreateFileW(L"\\\\.\\pipe\\DEViL.Lab",GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(pipe==INVALID_HANDLE_VALUE) return false;
    DWORD written=0; bool success=WriteFile(pipe,request.data(),static_cast<DWORD>(request.size()),&written,nullptr)&&written==request.size();
    CloseHandle(pipe); return success;
}

struct LabCommandSystem { uintptr_t object=0,add=0,exists=0,slot=0; explicit operator bool() const { return object&&add&&exists; } };
// Recognize the public meathook v7.2 ExecuteCommandText thunk, then validate
// the engine object with MSVC RTTI and executable-page checks. Unknown API
// implementations withhold registration instead of guessing addresses.
static LabCommandSystem resolveLabCommandSystem(const WallMemory& memory,uintptr_t execute,uintptr_t base,const GameProfile& profile) {
    unsigned char code[23]{};
    if(!memory.read(execute,code,sizeof(code))) return {};
    const unsigned char prefix[]{0x48,0x89,0xca,0x48,0x8b,0x05};
    const unsigned char suffix[]{0x48,0x8b,0x08,0x48,0x8b,0x01,0x48,0x8b,0x40,0x40,0x48,0xff,0xe0};
    if(memcmp(code,prefix,sizeof(prefix))||memcmp(code+10,suffix,sizeof(suffix))) return {};
    int32_t displacement=0; memcpy(&displacement,code+6,4);
    uintptr_t slot=0,object=0,vtable=0,locator=0; uint32_t rtti[6]{};
    auto inImage=[&](uintptr_t address,size_t size=8) { return address>=base&&address-base<profile.imageSize&&size<=profile.imageSize-(address-base); };
    if(!memory.get(execute+10+displacement,slot)||!inImage(slot)||!memory.get(slot,object)||!memory.get(object,vtable)||
       !inImage(vtable,72)||!memory.get(vtable-8,locator)||!inImage(locator,24)||!memory.read(locator,rtti,sizeof(rtti))||
       rtti[0]!=1||locator-base!=rtti[5]||!inImage(base+rtti[3],64)||
       readEntityText(memory,base+rtti[3]+16,64)!=".?AVidCmdSystemLocal@@") return {};
    LabCommandSystem result; result.object=object; result.slot=slot; uintptr_t executeMethod=0;
    if(!memory.get(vtable+24,result.add)||!memory.get(vtable+40,result.exists)||!memory.get(vtable+64,executeMethod)) return {};
    for(uintptr_t method:{result.add,result.exists,executeMethod}) {
        MEMORY_BASIC_INFORMATION info{};
        SIZE_T inspected=memory.remote?VirtualQueryEx(memory.remote,reinterpret_cast<void*>(method),&info,sizeof(info)):VirtualQuery(reinterpret_cast<void*>(method),&info,sizeof(info));
        if(!inImage(method)||!inspected||info.State!=MEM_COMMIT||
           !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))||info.Protect&PAGE_GUARD) return {};
    }
    return result;
}
#include "lab_cvars.h"

class LabConsole {
    const MeathookHudApi* api=nullptr;
    bool callbackRegistered=false,commandsRegistered=false,registrationBlocked=false,reportedUnavailable=false;
    ULONGLONG retryAt=0;
    ULONGLONG cvarsRetryAt=0;
    LabCommandSystem commandSystem;
    LabCvarBridge cvars;
    static LabConsole*& instance() { static LabConsole* value=nullptr; return value; }
    static void callback(void* self) { static_cast<LabConsole*>(self)->frame(); }
    static void command(void* raw) {
        auto* self=instance(); if(!self||!self->api) return;
        if(!labAppAvailable()) { self->api->consolePrint("%s","lab_: DEViL is closed; commands are inactive.\n"); return; }
        WallMemory memory; int count=0;
        if(!memory.get(reinterpret_cast<uintptr_t>(raw),count)||count<1||count>128) return;
        std::vector<std::string> args;
        for(int i=0;i<count;++i) {
            uintptr_t text=0;
            if(!memory.get(reinterpret_cast<uintptr_t>(raw)+8+size_t(i)*8,text)) return;
            auto value=readEntityText(memory,text,256);
            if(value.find('\t')!=std::string::npos||value.find('\n')!=std::string::npos) return;
            args.push_back(std::move(value));
        }
        if(!validLabArguments(args)) { self->api->consolePrint("%s","lab_: invalid arguments. Use lab_commands_list.\n"); return; }
        args=normalizeLabArguments(args);
        if(args[0]=="lab_encounters"&&args.size()==1) { printActiveEncounters(*self->api); }
        else if(args[0]=="lab_help") {
            for(const auto& definition:labCvarDefinitions) self->api->consolePrint("%s : %d..%d\n",definition.name,definition.minimum,definition.maximum);
            for(const auto& definition:labCommandDefinitions) if(!definition.legacy) self->api->consolePrint("%s : %s\n",definition.name,definition.usage);
        } else self->api->consolePrint("%s",sendLabRequest(args)?"lab_: request sent to DEViL.\n":"lab_: DEViL is unavailable or busy; retry.\n");
    }
    void frame() {
        bool available=labAppAvailable();
        const auto* profile=currentGameProfile(); if(!profile) return;
        if(commandsRegistered) {
            if(available&&!cvars.ready()&&!cvars.blocked()&&GetTickCount64()>=cvarsRetryAt) {
                cvarsRetryAt=GetTickCount64()+2000;
                cvars.initialize(*api,commandSystem,reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile);
            }
            cvars.poll(*api,available); return;
        }
        if(registrationBlocked||!available) return;
        auto system=resolveLabCommandSystem(WallMemory{},reinterpret_cast<uintptr_t>(api->executeCommand),reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile);
        if(!system) {
            if(!reportedUnavailable) { note("lab_ registration withheld: unrecognized meathook command API or engine command-system RTTI."); reportedUnavailable=true; }
            return;
        }
        using Exists=bool (*)(void*,const char*,bool);
        using Add=void (*)(void*,const char*,void (*)(void*),const char*,uintptr_t,unsigned);
        for(const auto& definition:labCommandDefinitions)
            if(reinterpret_cast<Exists>(system.exists)(reinterpret_cast<void*>(system.object),definition.name,false)) {
                note("lab_ registration withheld: command name is already owned by another module."); registrationBlocked=true; return;
            }
        instance()=this;
        for(const auto& definition:labCommandDefinitions)
            reinterpret_cast<Add>(system.add)(reinterpret_cast<void*>(system.object),definition.name,command,definition.usage,0,0);
        commandSystem=system;
        commandsRegistered=true; note("lab_ commands registered on the game thread; execution requires the running DEViL companion.");
    }
public:
    void connect() {
        if(callbackRegistered||!labAppAvailable()||GetTickCount64()<retryAt) return;
        retryAt=GetTickCount64()+2000;
        HMODULE module=GetModuleHandleW(L"XINPUT1_3.dll"); if(!module) return;
        auto getter=reinterpret_cast<const MeathookHudApi* (*)()>(GetProcAddress(module,"mh_api_get")); if(!getter) return;
        const auto* candidate=getter(); MeathookHudApi copy{};
        if(!candidate||!WallMemory{}.get(reinterpret_cast<uintptr_t>(candidate),copy)||!copy.executeCommand||!copy.consolePrint||!copy.registerPreFrame) return;
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&callback),&pinned)||
           !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(getter),&pinned)) return;
        api=candidate; callbackRegistered=true; api->registerPreFrame(callback,this);
    }
};
static LabConsole& labConsole() { static auto* console=new LabConsole; return *console; }
