#pragma once

struct LabCvarDefinition { const char* name; const wchar_t* setting; const char* request; int initial,minimum,maximum; };
static constexpr LabCvarDefinition labCvarDefinitions[]{
    {"lab_viewer_show",L"Enabled","lab_viewer",0,0,1},
    {"lab_triggers_show",L"ShowTriggers","lab_triggers",1,0,1},
    {"lab_collision_show",L"ShowCollision","lab_collision",1,0,1},
    {"lab_walls_show",L"ShowWalls","lab_walls",1,0,1},
    {"lab_inspector_show",L"InspectVolumes","lab_inspector",0,0,1},
    {"lab_disabled_volumes_show",L"ShowDisabledVolumes","lab_disabled",0,0,1},
    {"lab_volume_history_show",L"ShowVolumeHistory","lab_history",0,0,1},
    {"lab_path_show",L"ShowResearchPath","lab_path",1,0,1},
    {"lab_player_show",L"ShowPlayerCollider","lab_player",0,0,1},
    {"lab_surface_show",L"ShowSupportHud","lab_surface",0,0,1},
    {"lab_attempts_show",L"ShowMotionPaths","lab_attempts",1,0,1},
    {"lab_encounters_show",L"ShowActiveEncountersHud","lab_encounters",0,0,1},
    {"lab_viewer_distance",L"Distance","lab_distance",40,20,200},
    {"lab_viewer_fill",L"CollisionMode","lab_fill",1,0,1}
};
struct LabCvarSystem { uintptr_t object=0,find=0; explicit operator bool() const { return object&&find; } };
static LabCvarSystem resolveLabCvarSystem(const WallMemory& memory,const LabCommandSystem& command,uintptr_t base,const GameProfile& profile) {
    uintptr_t object=0,vtable=0,locator=0; uint32_t rtti[6]{};
    auto inside=[&](uintptr_t at,size_t size=8) { return at>=base&&at-base<profile.imageSize&&size<=profile.imageSize-(at-base); };
    // meathook's audited memory-system globals place cvarSystem immediately
    // before cmdSystem. Confirm the actual object type before calling it.
    if(!inside(command.slot)||command.slot<base+8||!memory.get(command.slot-8,object)||!memory.get(object,vtable)||!inside(vtable,40)||
       !memory.get(vtable-8,locator)||!inside(locator,24)||!memory.read(locator,rtti,sizeof(rtti))||rtti[0]!=1||locator-base!=rtti[5]||
       !inside(base+rtti[3],64)||readEntityText(memory,base+rtti[3]+16,64)!=".?AVidCVarSystemLocal@@") return {};
    LabCvarSystem result; result.object=object;
    if(!memory.get(vtable+32,result.find)||!inside(result.find)) return {};
    MEMORY_BASIC_INFORMATION info{};
    SIZE_T queried=memory.remote?VirtualQueryEx(memory.remote,reinterpret_cast<void*>(result.find),&info,sizeof(info)):VirtualQuery(reinterpret_cast<void*>(result.find),&info,sizeof(info));
    if(!queried||info.State!=MEM_COMMIT||!(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))||info.Protect&PAGE_GUARD) return {};
    return result;
}
static bool readLabCvarValue(const WallMemory& memory,uintptr_t variable,const char* expected,int& value,std::string& text) {
    uintptr_t data=0,name=0,string=0;
    if(!memory.get(variable,data)||!memory.get(data+40,name)||readEntityText(memory,name,128)!=expected||
       !memory.get(data+8,value)||!memory.get(data,string)) return false;
    text=readEntityText(memory,string,64); return !text.empty();
}
// Audited against the user's 6.66 Rev 3 image, 2026-10-27. Other profiles
// withhold creation until their constructor/register/set ABI is audited.
struct LabCvarFactory { uintptr_t construct=0,registerVariable=0,setString=0; explicit operator bool() const { return construct&&registerVariable&&setString; } };
static constexpr unsigned char labCvarConstructorPrefix[]{
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
    0x48,0x83,0xec,0x20,0x48,0x8d,0x41,0x08,0x33,0xed,0x48,0x89,0x01,0x41,0x8b,0xd9
};
static constexpr unsigned char labCvarRegisterPrefix[]{
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x56,
    0x57,0x41,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x33,0xed,0x4c,0x8b,0x71,0x08
};
static constexpr unsigned char labCvarSetPrefix[]{
    0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x49,0x8b,0xd8,0xff,0x50,0x20,0x48,
    0x85,0xc0
};
static LabCvarFactory resolveLabCvarFactory(const WallMemory& memory,const LabCvarSystem& system,uintptr_t base,const GameProfile& profile) {
    if(profile.timestamp!=0x669f0dcf||profile.imageSize!=0x744d000||!system) return {};
    uintptr_t vtable=0; LabCvarFactory result{base+0x371100,0,0};
    if(!memory.get(system.object,vtable)||!memory.get(vtable+0x108,result.registerVariable)||!memory.get(vtable+0x38,result.setString)||
       result.registerVariable!=base+0x178f470||result.setString!=base+0x178f8e0) return {};
    auto matches=[&](uintptr_t address,const unsigned char* expected,size_t size) {
        unsigned char code[32]{}; MEMORY_BASIC_INFORMATION info{};
        SIZE_T queried=memory.remote?VirtualQueryEx(memory.remote,reinterpret_cast<void*>(address),&info,sizeof(info)):VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info));
        return queried&&info.State==MEM_COMMIT&&!(info.Protect&PAGE_GUARD)&&
            (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))&&
            memory.read(address,code,size)&&memcmp(code,expected,size)==0;
    };
    if(!matches(result.construct,labCvarConstructorPrefix,sizeof(labCvarConstructorPrefix))||
       !matches(result.registerVariable,labCvarRegisterPrefix,sizeof(labCvarRegisterPrefix))||
       !matches(result.setString,labCvarSetPrefix,sizeof(labCvarSetPrefix))) return {};
    return result;
}
struct OwnedLabCvar {
    // Public meathook idCVar ABI: data pointer, 104-byte dataStorage, next.
    // Engine constructor owns initialization and string allocation. Never
    // write a foreign cvar's storage or free an engine-owned value string.
    alignas(8) unsigned char object[120]{};
    char initial[16]{}; // resetString must live as long as the registered cvar.
};
struct LabCvarBackend {
    std::function<uintptr_t(const char*)> find;
    std::function<void(OwnedLabCvar&,const LabCvarDefinition&)> create;
    std::function<void(const char*,const char*)> write;
};
static LabCvarBackend nativeLabCvarBackend(const LabCvarSystem& system,const LabCvarFactory& factory) {
    return {
        [system](const char* name) { using Find=void* (*)(void*,const char*); return reinterpret_cast<uintptr_t>(reinterpret_cast<Find>(system.find)(reinterpret_cast<void*>(system.object),name)); },
        [system,factory](OwnedLabCvar& owned,const LabCvarDefinition& definition) {
            using Construct=void (*)(void*,const char*,const char*,int,const char*,float,float,const char* const*,void*);
            using Register=void (*)(void*,void*);
            // Current engine flags: BOOL=1, INTEGER=2, NOCHEAT=0x10. These
            // are DEViL settings; expose them in normal console completion.
            int flags=(definition.maximum==1?1:2)|0x10;
            reinterpret_cast<Construct>(factory.construct)(owned.object,definition.name,owned.initial,flags,
                "DEViL companion setting",float(definition.minimum),float(definition.maximum),nullptr,nullptr);
            reinterpret_cast<Register>(factory.registerVariable)(reinterpret_cast<void*>(system.object),owned.object);
        },
        [system,factory](const char* name,const char* value) {
            using SetString=void (*)(void*,const char*,const char*,int);
            reinterpret_cast<SetString>(factory.setString)(reinterpret_cast<void*>(system.object),name,value,0);
        }
    };
}
class LabCvarBridge {
    struct Tracked { const LabCvarDefinition* definition=nullptr; uintptr_t variable=0; int engine=0,saved=0,pending=0; bool awaiting=false; ULONGLONG sent=0; };
    std::vector<Tracked> variables;
    std::array<OwnedLabCvar,std::size(labCvarDefinitions)> owned;
    LabCvarBackend backend;
    std::function<bool(const std::vector<std::string>&)> request;
    std::wstring settings;
    bool initialized=false,wasAvailable=false,registrationBlocked=false;
    ULONGLONG lastPoll=0;
    int saved(const LabCvarDefinition& definition) const {
        int value=static_cast<int>(GetPrivateProfileIntW(L"Viewer",definition.setting,definition.initial,settings.c_str()));
        return std::clamp(value,definition.minimum,definition.maximum);
    }
    void block(const MeathookHudApi& api,const char* reason) {
        initialized=false;
        if(registrationBlocked) return;
        registrationBlocked=true;
        note(reason);
        api.consolePrint("%s\n",reason);
    }
    bool set(const MeathookHudApi& api,const Tracked& tracked,int value) {
        // Only addresses created and registered by this bridge may be changed.
        if(!backend.find||backend.find(tracked.definition->name)!=tracked.variable) { block(api,"lab_: cvar identity changed; console settings disabled for this game process."); return false; }
        auto text=std::to_string(value); backend.write(tracked.definition->name,text.c_str());
        int actual=0; std::string string;
        if(!readLabCvarValue(WallMemory{},tracked.variable,tracked.definition->name,actual,string)||actual!=value||string!=text) {
            block(api,"lab_: engine rejected the cvar update; console settings disabled for this game process."); return false;
        }
        return true;
    }
public:
    bool ready() const { return initialized; }
    bool blocked() const { return registrationBlocked; }
    bool initialize(const MeathookHudApi& api,const LabCommandSystem& command,uintptr_t base,const GameProfile& profile) {
        if(initialized) return true;
        if(registrationBlocked) return false;
        auto system=resolveLabCvarSystem(WallMemory{},command,base,profile); if(!system) return false;
        auto factory=resolveLabCvarFactory(WallMemory{},system,base,profile);
        if(!factory) { block(api,"lab_: native cvar creation ABI is unavailable; use the DEViL window. No registration retries in this game process."); return false; }
        auto directory=researchDirectory(); if(directory.empty()) return false;
        return initializeBackend(api,nativeLabCvarBackend(system,factory),(directory/L"viewer.ini").wstring(),sendLabRequest);
    }
    bool initializeBackend(const MeathookHudApi& api,LabCvarBackend engine,std::wstring ini,
                           std::function<bool(const std::vector<std::string>&)> transport) {
        if(initialized) return true;
        if(registrationBlocked) return false;
        backend=std::move(engine); request=std::move(transport); settings=std::move(ini);
        if(!backend.find||!backend.create||!backend.write||!request) return false;
        int known=0; std::string knownText;
        // Verify the value-object ABI against an existing engine cvar before
        // creating any owned variables. No raw game-memory writes are used.
        if(!readLabCvarValue(WallMemory{},backend.find("com_debugHUD"),"com_debugHUD",known,knownText)) return false;
        for(const auto& definition:labCvarDefinitions) if(backend.find(definition.name)) {
            block(api,"lab_: a cvar name is already owned by another module; console settings disabled."); return false;
        }
        size_t index=0;
        for(const auto& definition:labCvarDefinitions) {
            int value=saved(definition); auto& storage=owned[index++]; sprintf_s(storage.initial,"%d",value);
            backend.create(storage,definition);
            uintptr_t variable=backend.find(definition.name);
            int actual=0; std::string actualText;
            if(variable!=reinterpret_cast<uintptr_t>(storage.object)||!readLabCvarValue(WallMemory{},variable,definition.name,actual,actualText)||actual!=value||actualText!=std::to_string(value)) {
                block(api,"lab_: native cvar registration failed; console settings disabled. No registration retries in this game process."); return false;
            }
            variables.push_back({&definition,variable,value,value,0,false,0});
        }
        initialized=true; wasAvailable=true; note("lab_ state settings registered as native engine cvars; action commands remain separate."); return true;
    }
    void poll(const MeathookHudApi& api,bool available,ULONGLONG now=GetTickCount64()) {
        if(!initialized||now-lastPoll<100) return; lastPoll=now;
        for(auto& variable:variables) {
            const auto& definition=*variable.definition; int engine=0; std::string text;
            if(backend.find(definition.name)!=variable.variable||!readLabCvarValue(WallMemory{},variable.variable,definition.name,engine,text)) {
                block(api,"lab_: cvar identity became unreadable; console settings disabled for this game process."); return;
            }
            int stored=saved(definition);
            if(stored!=variable.saved||(available&&!wasAvailable)) {
                variable.saved=stored; variable.engine=stored; variable.awaiting=false; if(!set(api,variable,stored)) return; continue;
            }
            if(engine!=variable.engine||text!=std::to_string(engine)) {
                if(available&&engine>=definition.minimum&&engine<=definition.maximum&&request({definition.request,std::to_string(engine)})) {
                    variable.engine=engine; variable.pending=engine; variable.awaiting=true; variable.sent=now;
                    if(text!=std::to_string(engine)&&!set(api,variable,engine)) return;
                } else {
                    if(!set(api,variable,stored)) return; variable.engine=stored; variable.awaiting=false;
                    api.consolePrint("%s","lab_: open DEViL and use a value within the documented range.\n");
                }
            }
            if(variable.awaiting&&stored==variable.pending) variable.awaiting=false;
            else if(variable.awaiting&&now-variable.sent>2000) {
                if(!set(api,variable,stored)) return; variable.engine=stored; variable.awaiting=false;
                api.consolePrint("%s","lab_: DEViL did not acknowledge the cvar change; restored the saved value.\n");
            }
        }
        wasAvailable=available;
    }
};
