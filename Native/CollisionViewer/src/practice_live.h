#pragma once
static uint64_t encounterEpochMillis();
static std::string practiceClean(std::string text) {
    for(char& c:text) if(static_cast<unsigned char>(c)<32||c==127) c=' ';
    if(text.size()>2048) text.resize(2048); return text;
}
// Live checkpoint archival/restore experiment, restricted to TAG1 slot 3.
// All Steam/engine calls, fixed-path file writes and quiescence checks run in
// meathook's frame callback. Account ownership is never changed.
struct PracticeRuntime {
    uintptr_t system=0,map=0,record=0;
    uint32_t user=0,index=~0u;
    std::string folder,level,error;
    explicit operator bool() const { return error.empty()&&system&&map&&record; }
};
static PracticeRuntime readPracticeRuntime(const WallMemory& memory,uintptr_t base,const GameProfile* profile) {
    PracticeRuntime out;
    auto fail=[&](const char* message) { out.error=message; return out; };
    if(!profile||profile->timestamp!=kProfile6663.timestamp||profile->imageSize!=kProfile6663.imageSize)
        return fail("Live experiment requires audited DOOM Eternal 6.66 Rev 3.");
    uint32_t state=0,pending=~0u,type=0; unsigned char requested=1,paused=0,removed=1;
    uintptr_t ghost=0,globalGhost=0,users=0,master=0,shared=0,player=0,vt=0;
    if(!memory.get(base+profile->gameSystemRva,out.system)||out.system!=base+0x45d82c0||
       !memory.get(out.system+0x44,state)||state!=2||!memory.get(out.system+0x50,out.map)||!out.map)
        return fail("Load the expendable TAG1 slot 3 first.");
    if(!memory.get(out.map+0x19c8,type)||type!=1||!memory.get(out.map+0x20,shared)) return fail("Campaign map unavailable.");
    out.level=readEntityText(memory,shared,512);
    if(out.level.rfind("game/dlc/e4m",0)!=0) return fail("This first experiment is restricted to TAG1 maps.");
    if(!memory.get(out.map+profile->mapPlayerOffset,player)||!memory.get(player,vt)||vt!=base+profile->playerVtableRva||
       !memory.get(player+0xa4,removed)||removed||!memory.get(player+profile->playerHudOffset+profile->hudPausedOffset,paused)||paused!=1)
        return fail("Pause the game in the expendable TAG1 slot 3.");
    if(!memory.get(out.system+0x9ca8,pending)||pending||!memory.get(out.system+0xb8,requested)||requested)
        return fail("Engine save/map-change queue is busy. Wait on pause and retry.");
    if(!memory.get(out.system+0x9b38,ghost)||!ghost||!memory.get(base+0x45e1df8,globalGhost)||ghost!=globalGhost)
        return fail("Save manager unavailable.");
    for(auto offset:{0x50u,0x58u,0x60u}) {
        uintptr_t task=0,object=0,count=0;
        if(!memory.get(ghost+offset,task)||(task&&!memory.get(task+8,object))) return fail("Cannot read save task state.");
        if(offset==0x58) {
            if(object&&(!memory.get(object+8,count)||count)) return fail("Engine is still writing saves.");
        } else if(object) return fail("Engine save/load task is busy.");
    }
    if(!memory.get(base+0x47cb468,users)||!memory.get(users+0x18,shared)||!shared||
       !memory.get(shared+8,master)||!master||!memory.get(master+4,out.user)) return fail("Master local user unavailable.");
    uintptr_t data=0; uint64_t count=0;
    if(!memory.get(ghost+0x70,data)||!memory.get(ghost+0x78,count)||count>8) return fail("User slot list unavailable.");
    for(uint64_t i=0;i<count;++i) {
        uintptr_t object=0; uint32_t handle=0;
        if(!memory.get(data+i*8,shared)||!shared||!memory.get(shared+8,object)||!object||!memory.get(object,handle)) return fail("Unreadable user slot record.");
        if(handle!=out.user) continue;
        if(out.record) return fail("Ambiguous master user slot.");
        out.record=object+8;
        if(!memory.get(out.record,out.index)||!memory.get(out.record+16,shared)) return fail("Current slot unavailable.");
        out.folder=readEntityText(memory,shared,64);
    }
    if(out.index!=2||out.folder!="AUTOSAVE5") return fail("Current slot must be TAG1 slot 3 (DLC1-AUTOSAVE5).");
    return out;
}
static bool practiceCloudDisabled(int accountEnabled,int appEnabled) {
    return accountEnabled>=0&&accountEnabled<=1&&appEnabled>=0&&appEnabled<=1&&!(accountEnabled&&appEnabled);
}
struct PracticeSteamIdentity {
    uint64_t account=0;
    bool cloudDisabled=false;
    int cloudAccount=-1,cloudApp=-1;
};
static PracticeSteamIdentity readPracticeSteam() {
    auto module=GetModuleHandleW(L"steam_api64.dll"); if(!module) return {};
    using User=int(__cdecl*)(); using Find=void*(__cdecl*)(int,const char*);
    using Id=uint64_t(__cdecl*)(void*); using Enabled=bool(__cdecl*)(void*);
    auto user=reinterpret_cast<User>(GetProcAddress(module,"SteamAPI_GetHSteamUser"));
    auto find=reinterpret_cast<Find>(GetProcAddress(module,"SteamInternal_FindOrCreateUserInterface"));
    auto id=reinterpret_cast<Id>(GetProcAddress(module,"SteamAPI_ISteamUser_GetSteamID"));
    auto cloudAccount=reinterpret_cast<Enabled>(GetProcAddress(module,"SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount"));
    auto cloudApp=reinterpret_cast<Enabled>(GetProcAddress(module,"SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp"));
    if(!user||!find||!id) return {};
    // Interface strings present in the audited retail EXE; use existing Steam
    // client interfaces only. Never initialize/shut down Steam or change Cloud.
    auto steamUser=find(user(),"SteamUser020"); if(!steamUser) return {};
    PracticeSteamIdentity state; state.account=id(steamUser);
    auto storage=find(user(),"STEAMREMOTESTORAGE_INTERFACE_VERSION014");
    if(!storage||!cloudAccount||!cloudApp) return state;
    state.cloudAccount=cloudAccount(storage)?1:0; state.cloudApp=cloudApp(storage)?1:0;
    state.cloudDisabled=practiceCloudDisabled(state.cloudAccount,state.cloudApp);
    return state;
}
static bool practiceToken(const std::string& token) {
    return token.size()==32&&std::all_of(token.begin(),token.end(),[](char c) { return (c>='0'&&c<='9')||(c>='a'&&c<='f'); });
}
static void practiceNoLinks(std::filesystem::path path) {
    for(;!path.empty();path=path.parent_path()) {
        auto attrs=GetFileAttributesW(path.c_str());
        if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Linked save paths are unsupported.");
        if(path==path.parent_path()) break;
    }
}
static bool practiceRemote(const std::filesystem::path& remote,uint64_t account) {
    constexpr uint64_t individual=76561197960265728ULL;
    if(account<=individual||account>individual+UINT32_MAX||!remote.is_absolute()||remote.lexically_normal()!=remote) return false;
    auto app=remote.parent_path(),user=app.parent_path();
    return remote.filename()==L"remote"&&app.filename()==L"782330"&&user.filename()==std::to_wstring(account-individual)&&user.parent_path().filename()==L"userdata";
}
static std::array<std::vector<unsigned char>,4> readPracticeFiles(const std::filesystem::path& slot) {
    static const wchar_t* names[]{L"game.details",L"game.details-BACKUP",L"game_duration.dat",L"game_duration.dat-BACKUP"};
    practiceNoLinks(slot); std::vector<HANDLE> handles;
    struct Close { std::vector<HANDLE>& handles; ~Close() { for(auto handle:handles) CloseHandle(handle); } } close{handles};
    HANDLE directory=CreateFileW(slot.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(directory==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock save directory for reading."); handles.push_back(directory);
    size_t count=0;
    for(const auto& file:std::filesystem::directory_iterator(slot)) {
        practiceNoLinks(file.path());
        if(!file.is_regular_file()||std::find_if(std::begin(names),std::end(names),[&](auto name) { return file.path().filename()==name; })==std::end(names))
            throw std::runtime_error("Unexpected save files; this first experiment supports the four standard containers only.");
        ++count;
    }
    if(count!=4) throw std::runtime_error("All four checkpoint containers are required.");
    std::array<std::vector<unsigned char>,4> result; uint64_t total=0;
    // Open ALL containers before reading any; a concurrent writer is denied.
    for(const auto name:names) {
        HANDLE handle=CreateFileW((slot/name).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Save files are still open for writing. Retry after autosave completes."); handles.push_back(handle);
    }
    for(size_t i=0;i<4;++i) {
        LARGE_INTEGER length{};
        if(!GetFileSizeEx(handles[i+1],&length)||length.QuadPart<28||length.QuadPart>64000000||(total+=length.QuadPart)>128000000)
            throw std::runtime_error("Unsupported checkpoint container size.");
        auto& bytes=result[i]; bytes.resize(static_cast<size_t>(length.QuadPart)); DWORD got=0;
        if(!ReadFile(handles[i+1],bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)||got!=bytes.size()) throw std::runtime_error("Checkpoint read failed.");
    }
    return result;
}
#include "practice_transaction.h"
static bool practiceLoaderMatches(const WallMemory& memory,uintptr_t base) {
    struct Signature { uintptr_t rva; std::array<unsigned char,16> bytes; };
    static const Signature signatures[]{
        {0x6682c0,{0x48,0x83,0xec,0x28,0x48,0x8b,0x05,0x9d,0x31,0x16,0x04,0x4c,0x8b,0x40,0x18,0x4d}},
        {0x3f5c00,{0x48,0x8d,0x05,0xe1,0x2a,0x66,0x02,0xc7,0x41,0x14,0x14,0x00,0x00,0x80,0x48,0x89}},
        {0x3f6190,{0x48,0x8b,0x52,0x08,0xe9,0x77,0x01,0x00,0x00,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}},
        {0x3f83c0,{0x40,0x53,0x48,0x83,0xec,0x20,0x45,0x33,0xc9,0x41,0xb0,0x01,0x48,0x8b,0xd9,0x41}},
        {0x3f5fd0,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x51,0x08,0x48,0x8d,0x05,0x07,0x27,0x66}}
    };
    for(const auto& signature:signatures) { std::array<unsigned char,16> bytes{}; if(!memory.read(base+signature.rva,bytes.data(),bytes.size())||bytes!=signature.bytes) return false; }
    return true;
}
// Mirrors the audited RequestRestartMission parameter construction, with b9=0
// to load PRIMARY checkpoint files. No patching of game code or save contents.
static bool requestPracticeReload(const WallMemory& memory,uintptr_t base,const PracticeRuntime& state) {
    if(!practiceLoaderMatches(memory,base)) return false;
    alignas(16) std::array<unsigned char,192> parms{};
    using StringMethod=void(__fastcall*)(void*); using Assign=void(__fastcall*)(void*,const void*);
    auto construct=reinterpret_cast<StringMethod>(base+0x3f5c00),destroy=reinterpret_cast<StringMethod>(base+0x3f5fd0);
    auto clear=reinterpret_cast<StringMethod>(base+0x3f83c0); auto assign=reinterpret_cast<Assign>(base+0x3f6190);
    for(size_t i=0;i<3;++i) construct(parms.data()+i*48);
    assign(parms.data(),reinterpret_cast<void*>(state.record+8)); clear(parms.data()+48);
    bool valid=memory.read(state.map+0x196c,parms.data()+0x90,8)&&memory.read(state.map+0x19c8,parms.data()+0x98,4)&&memory.read(state.map+0x19d8,parms.data()+0xa0,8);
    if(valid) reinterpret_cast<void(__fastcall*)(void*,const void*)>(base+0x6682c0)(reinterpret_cast<void*>(state.system),parms.data());
    for(int i=2;i>=0;--i) destroy(parms.data()+size_t(i)*48);
    return valid;
}
static bool practiceReloadCompleted(bool& sawTransition,uintptr_t originalMap,bool readable,uint32_t state,uintptr_t map,bool requested,bool validTag1Level) {
    // A queued request alone does not prove that the previous map unloaded.
    if(readable&&(state!=2||map!=originalMap)) sawTransition=true;
    return sawTransition&&readable&&state==2&&!requested&&validTag1Level;
}
class PracticeLiveBridge {
    std::string seen,token,operation,account,checkpoint,cloudDetail,recoveryDetail; std::filesystem::path remote;
    uint64_t requested=0; ULONGLONG idleSince=0,nextPoll=0,loadingSince=0; uintptr_t originalMap=0;
    bool loading=false,sawTransition=false;
    void result(const std::filesystem::path& directory,const std::string& phase,const std::string& detail,const std::string& level="") {
        auto path=directory/(L"practice-live-"+std::to_wstring(GetCurrentProcessId())+L".tsv"),temporary=path; temporary+=L".tmp";
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        out<<"#DEVIL_LIVE_PRACTICE_V1\t"<<GetCurrentProcessId()<<'\t'<<token<<'\t'<<encounterEpochMillis()<<'\t'<<phase<<'\t'<<account<<"\tDLC1-AUTOSAVE5\t"<<practiceClean(level)<<'\t'<<practiceClean(checkpoint)<<'\t'<<practiceClean(detail+(cloudDetail.empty()?"":" "+cloudDetail)+recoveryDetail)<<'\n';
        out.close(); if(out) MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
    }
public:
    void update(const GameProfile* profile) {
        if(!labAppAvailable()||GetTickCount64()<nextPoll) return; nextPoll=GetTickCount64()+100;
        const auto directory=researchDirectory(); if(directory.empty()) return;
        auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); WallMemory memory;
        try {
            if(loading) {
                uintptr_t system=0,map=0; uint32_t state=0; unsigned char change=0;
                bool readable=profile&&memory.get(base+profile->gameSystemRva,system)&&memory.get(system+0x44,state)&&memory.get(system+0x50,map)&&memory.get(system+0xb8,change);
                auto level=profile?readLevelIdentity(memory,base,*profile):LevelIdentity{};
                if(practiceReloadCompleted(sawTransition,originalMap,readable,state,map,change!=0,level&&level.name.rfind("game/dlc/e4m",0)==0)) {
                    auto layout=std::atomic_load(&checkpointMetadata);
                    auto loaded=layout?readLoadedCheckpoint(memory,level.map,level.name,*layout):CheckpointObservation{};
                    checkpoint=loaded.name; result(directory,"reloaded",operation=="restore"?"Archive installed through Steam storage; engine map transition completed.":"Engine map transition completed. No save files were replaced.",level.name); loading=false; token.clear();
                } else if(GetTickCount64()-loadingSince>300000) {
                    result(directory,"error",operation=="restore"?"Archive files were installed but engine completion was not observed. Use retained Original if recovery is needed.":"Engine reload was requested but completion was not observed."); loading=false; token.clear();
                }
                return;
            }
            auto path=directory/(L"practice-live-"+std::to_wstring(GetCurrentProcessId())+L".request");
            if(token.empty()) {
                std::error_code error; auto length=std::filesystem::file_size(path,error); if(error||length>4096) return;
                std::ifstream in(path,std::ios::binary); std::string line; if(!std::getline(in,line)) return;
                std::vector<std::string> values; std::istringstream split(line); std::string part;
                while(std::getline(split,part,'\t')) values.push_back(part);
                if(values.size()!=8||values[0]!="#DEVIL_LIVE_PRACTICE_REQUEST_V1"||values[1]!=std::to_string(GetCurrentProcessId())||
                   !practiceToken(values[2])||values[2]==seen||(values[4]!="archive"&&values[4]!="reload"&&values[4]!="restore")||values[6]!="DLC1-AUTOSAVE5") return;
                auto number=[](const std::string& value) { return !value.empty()&&value.size()<=20&&std::all_of(value.begin(),value.end(),[](char c) { return c>='0'&&c<='9'; }); };
                if(!number(values[3])||!number(values[5])) return;
                requested=std::stoull(values[3]); auto now=encounterEpochMillis();
                if(requested>now+2000||now>requested+10000) { seen=values[2]; return; }
                token=seen=values[2]; operation=values[4]; account=values[5]; checkpoint.clear(); cloudDetail.clear(); recoveryDetail.clear(); idleSince=0;
                remote=std::filesystem::u8path(values[7]);
            }
            if(encounterEpochMillis()>requested+10000) { result(directory,"error","No paused, idle save state was available within ten seconds."); token.clear(); return; }
            auto state=readPracticeRuntime(memory,base,profile);
            if(!state) { result(directory,"error",state.error); token.clear(); return; }
            auto steam=readPracticeSteam();
            cloudDetail="Steam Cloud flags: account="+std::to_string(steam.cloudAccount)+", app="+std::to_string(steam.cloudApp)+".";
            if(!steam.account||std::to_string(steam.account)!=account||!practiceRemote(remote,steam.account)) {
                result(directory,"error","Steam runtime account and save-folder owner must match. Account mixing is blocked."); token.clear(); return;
            }
            if(!steam.cloudDisabled) {
                result(directory,"error",steam.cloudAccount<0||steam.cloudApp<0?"Steam Cloud state is unavailable; live operation blocked.":"Steam Cloud is enabled both for the account and the game. Disable Cloud before the live operation.");
                token.clear(); return;
            }
            if(!idleSince) { idleSince=GetTickCount64(); originalMap=state.map; result(directory,"waiting","Checking idle save state for 500 ms.",state.level); return; }
            if(originalMap!=state.map) { result(directory,"error","Map changed during preparation."); token.clear(); return; }
            if(GetTickCount64()-idleSince<500) return;
            auto files=readPracticeFiles(remote/L"DLC1-AUTOSAVE5");
            auto after=readPracticeRuntime(memory,base,profile);
            if(!after||after.map!=state.map||after.record!=state.record||after.user!=state.user) throw std::runtime_error("Engine state changed during checkpoint read.");
            auto layout=std::atomic_load(&checkpointMetadata);
            auto loaded=layout?readLoadedCheckpoint(memory,state.map,state.level,*layout):CheckpointObservation{}; checkpoint=loaded.name;
            if(operation=="archive") {
                practiceAuthenticate(files,account);
                if(practiceStorageApi().read()!=files) throw std::runtime_error("Steam storage cache and selected disk checkpoint differ. No archive created.");
                auto output=directory/L"live-practice"/std::to_wstring(GetCurrentProcessId())/token/L"slot";
                practiceNoLinks(output); if(std::filesystem::exists(output)) throw std::runtime_error("Capture destination already exists.");
                std::filesystem::create_directories(output);
                const wchar_t* names[]{L"game.details",L"game.details-BACKUP",L"game_duration.dat",L"game_duration.dat-BACKUP"};
                for(size_t i=0;i<4;++i) { std::ofstream file(output/names[i],std::ios::binary); file.write(reinterpret_cast<const char*>(files[i].data()),files[i].size()); file.close(); if(!file) throw std::runtime_error("Checkpoint copy failed."); }
                result(directory,"archived","Completed checkpoint copied after native authentication and disk/Steam consistency checks. Companion must verify the copy.",state.level); token.clear();
            } else {
                if(operation=="restore") {
                    if(!practiceLoaderMatches(memory,base)) throw std::runtime_error("Audited engine loader signatures did not match; original slot untouched.");
                    auto bundle=directory/L"live-practice"/L"restore-pending"/std::to_wstring(GetCurrentProcessId())/token;
                    auto plan=readPracticeRestorePlan(bundle,account,token,GetCurrentProcessId());
                    auto incoming=readPracticeFiles(bundle/L"incoming");
                    auto stillIdle=[&] {
                        auto check=readPracticeRuntime(memory,base,profile); auto owner=readPracticeSteam();
                        return check&&check.map==state.map&&check.record==state.record&&check.user==state.user&&owner.account==steam.account&&owner.cloudDisabled;
                    };
                    commitPracticeRestore(remote,bundle,incoming,files,plan,account,token,practiceStorageApi(),stillIdle,[&](const char* step) {
                        if(strcmp(step,"backed_up")==0) recoveryDetail=" Original retained: "+(remote.parent_path()/(L".devil-live-original-"+std::wstring(token.begin(),token.end()))).u8string()+". Recovery journal: "+(bundle/L"RESTORE_PATHS.txt").u8string()+".";
                    });
                    if(!stillIdle()) throw std::runtime_error("Files installed, but engine/account/Cloud changed before reload. Original retained; do not continue until reviewed.");
                }
                if(!requestPracticeReload(memory,base,state)) throw std::runtime_error("Audited engine-loader signatures or load parameters did not match.");
                loading=true; loadingSince=GetTickCount64(); sawTransition=false; originalMap=state.map;
                result(directory,"loading",operation=="restore"?"Archive files installed and verified through Steam storage. Loading PRIMARY checkpoint; press Space in game if loading reaches 100%.":"Requested PRIMARY checkpoint through the engine. No save files replaced. Press Space in game if loading reaches 100%.",state.level);
                // Keep the request token until asynchronous completion is reported.
            }
        } catch(const std::exception& error) { if(!token.empty()) result(directory,"error",error.what()); token.clear(); loading=false; }
        if(!loading&&operation=="reload") token.clear();
    }
};
static PracticeLiveBridge practiceLiveBridge;
