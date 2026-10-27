#pragma once
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
using PracticeFiles=std::array<std::vector<unsigned char>,4>;
static const wchar_t* const practiceFileNames[]{L"game.details",L"game.details-BACKUP",L"game_duration.dat",L"game_duration.dat-BACKUP"};
static const char* const practiceFileKeys[]{"game.details","game.details-BACKUP","game_duration.dat","game_duration.dat-BACKUP"};
struct PracticeAlgorithm {
    BCRYPT_ALG_HANDLE handle=nullptr;
    explicit PracticeAlgorithm(const wchar_t* name) { if(BCryptOpenAlgorithmProvider(&handle,name,nullptr,0)<0) throw std::runtime_error("Windows cryptography unavailable."); }
    ~PracticeAlgorithm() { if(handle) BCryptCloseAlgorithmProvider(handle,0); }
    PracticeAlgorithm(const PracticeAlgorithm&)=delete;
};
static std::array<unsigned char,32> practiceSha(const std::vector<unsigned char>& bytes) {
    PracticeAlgorithm algorithm(BCRYPT_SHA256_ALGORITHM); std::array<unsigned char,32> digest{};
    if(BCryptHash(algorithm.handle,nullptr,0,const_cast<unsigned char*>(bytes.data()),static_cast<ULONG>(bytes.size()),digest.data(),static_cast<ULONG>(digest.size()))<0)
        throw std::runtime_error("Checkpoint SHA-256 failed.");
    return digest;
}
static std::string practiceHash(const std::vector<unsigned char>& bytes) {
    const char hex[]="0123456789abcdef"; std::string result; for(auto value:practiceSha(bytes)) { result+=hex[value>>4]; result+=hex[value&15]; } return result;
}
static bool practiceAuthentic(const std::vector<unsigned char>& bytes,const std::string& account,const char* name) {
    if(bytes.size()<28||bytes.size()>64000000) return false;
    std::string aad=account+"MANCUBUS"+name; std::vector<unsigned char> associated(aad.begin(),aad.end()); auto digest=practiceSha(associated);
    PracticeAlgorithm algorithm(BCRYPT_AES_ALGORITHM);
    if(BCryptSetProperty(algorithm.handle,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)<0) return false;
    ULONG length=0,got=0; if(BCryptGetProperty(algorithm.handle,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&got,0)<0||length>65536) return false;
    std::vector<unsigned char> object(length),plain(std::max<size_t>(1,bytes.size()-28)); BCRYPT_KEY_HANDLE key=nullptr;
    if(BCryptGenerateSymmetricKey(algorithm.handle,&key,object.data(),length,digest.data(),16,0)<0) return false;
    struct Close { BCRYPT_KEY_HANDLE key; ~Close() { BCryptDestroyKey(key); } } close{key};
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth; BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce=const_cast<PUCHAR>(bytes.data()); auth.cbNonce=12; auth.pbTag=const_cast<PUCHAR>(bytes.data()+bytes.size()-16); auth.cbTag=16;
    auth.pbAuthData=associated.data(); auth.cbAuthData=static_cast<ULONG>(associated.size());
    auto status=BCryptDecrypt(key,const_cast<PUCHAR>(bytes.data()+12),static_cast<ULONG>(bytes.size()-28),&auth,nullptr,0,plain.data(),static_cast<ULONG>(plain.size()),&got,0);
    SecureZeroMemory(plain.data(),plain.size()); return status>=0&&got==bytes.size()-28;
}
static void practiceAuthenticate(const PracticeFiles& files,const std::string& account) {
    for(size_t i=0;i<4;++i) if(!practiceAuthentic(files[i],account,practiceFileKeys[i])) throw std::runtime_error("Checkpoint authentication failed for the current Steam owner. Account mixing is blocked.");
}
static void writePracticeFiles(const std::filesystem::path& path,const PracticeFiles& files) {
    practiceNoLinks(path); if(std::filesystem::exists(path)) throw std::runtime_error("Transaction destination already exists.");
    std::filesystem::create_directories(path);
    for(size_t i=0;i<4;++i) {
        HANDLE file=CreateFileW((path/practiceFileNames[i]).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create staged checkpoint.");
        DWORD written=0; bool ok=WriteFile(file,files[i].data(),static_cast<DWORD>(files[i].size()),&written,nullptr)&&written==files[i].size()&&FlushFileBuffers(file); CloseHandle(file);
        if(!ok) throw std::runtime_error("Staged checkpoint write failed.");
    }
}
struct PracticeExpectedFile { uint64_t length=0; std::string hash; };
struct PracticeRestorePlan {
    std::string archive;
    std::array<PracticeExpectedFile,4> targets,sources;
};
static PracticeRestorePlan readPracticeRestorePlan(const std::filesystem::path& bundle,const std::string& account,const std::string& token,DWORD process) {
    practiceNoLinks(bundle); auto path=bundle/L"plan.tsv"; practiceNoLinks(path);
    if(std::filesystem::file_size(path)>4096) throw std::runtime_error("Restore plan is too large.");
    std::ifstream in(path,std::ios::binary); std::string line,part; std::vector<std::string> header;
    if(!std::getline(in,line)) throw std::runtime_error("Restore plan missing."); std::istringstream fields(line);
    while(std::getline(fields,part,'\t')) header.push_back(part);
    if(header.size()!=6||header[0]!="#DEVIL_LIVE_RESTORE_PLAN_V1"||header[1]!=account||header[2]!="DLC1-AUTOSAVE5"||header[3]!=token||header[4]!=std::to_string(process)||!practiceToken(header[5]))
        throw std::runtime_error("Restore plan owner/slot/process/token mismatch.");
    PracticeRestorePlan plan; plan.archive=header[5];
    for(size_t i=0;i<8;++i) {
        if(!std::getline(in,line)) throw std::runtime_error("Incomplete restore target fingerprint.");
        std::istringstream row(line); std::string name,size,hash,extra;
        if(!std::getline(row,name,'\t')||!std::getline(row,size,'\t')||!std::getline(row,hash,'\t')||std::getline(row,extra,'\t')||name!=practiceFileKeys[i%4]||size.empty()||size.size()>9||
           !std::all_of(size.begin(),size.end(),[](char c) { return c>='0'&&c<='9'; })||hash.size()!=64||!std::all_of(hash.begin(),hash.end(),[](char c) { return (c>='0'&&c<='9')||(c>='a'&&c<='f'); }))
            throw std::runtime_error("Malformed restore target fingerprint.");
        (i<4?plan.targets[i]:plan.sources[i-4])={std::stoull(size),hash};
    }
    if(std::getline(in,line)) throw std::runtime_error("Unexpected restore plan data."); return plan;
}
static bool practiceFilesMatch(const PracticeFiles& files,const std::array<PracticeExpectedFile,4>& expected) {
    for(size_t i=0;i<4;++i) if(files[i].size()!=expected[i].length||practiceHash(files[i])!=expected[i].hash) return false; return true;
}
static bool practiceTargetMatches(const PracticeFiles& files,const PracticeRestorePlan& plan) {
    return practiceFilesMatch(files,plan.targets);
}
struct PracticeCommitResult { std::filesystem::path original,journal; };
struct PracticeStorageApi {
    std::function<bool(size_t,const std::vector<unsigned char>&)> write;
    std::function<PracticeFiles()> read;
};
static PracticeStorageApi practiceStorageApi() {
    auto module=GetModuleHandleW(L"steam_api64.dll"); if(!module) throw std::runtime_error("Steam storage unavailable.");
    using User=int(__cdecl*)(); using Find=void*(__cdecl*)(int,const char*);
    using Write=bool(__cdecl*)(void*,const char*,const void*,int); using Read=int(__cdecl*)(void*,const char*,void*,int); using Size=int(__cdecl*)(void*,const char*);
    auto user=reinterpret_cast<User>(GetProcAddress(module,"SteamAPI_GetHSteamUser")); auto find=reinterpret_cast<Find>(GetProcAddress(module,"SteamInternal_FindOrCreateUserInterface"));
    auto write=reinterpret_cast<Write>(GetProcAddress(module,"SteamAPI_ISteamRemoteStorage_FileWrite")); auto read=reinterpret_cast<Read>(GetProcAddress(module,"SteamAPI_ISteamRemoteStorage_FileRead")); auto size=reinterpret_cast<Size>(GetProcAddress(module,"SteamAPI_ISteamRemoteStorage_GetFileSize"));
    if(!user||!find||!write||!read||!size) throw std::runtime_error("Required Steam storage exports unavailable.");
    auto storage=find(user(),"STEAMREMOTESTORAGE_INTERFACE_VERSION014"); if(!storage) throw std::runtime_error("Steam storage interface unavailable.");
    PracticeStorageApi api;
    api.write=[=](size_t index,const std::vector<unsigned char>& bytes) {
        if(index>=4||bytes.size()>64000000) return false;
        auto path=std::string("DLC1-AUTOSAVE5/")+practiceFileKeys[index]; return write(storage,path.c_str(),bytes.data(),static_cast<int>(bytes.size()));
    };
    api.read=[=] {
        PracticeFiles files; uint64_t total=0;
        for(size_t i=0;i<4;++i) {
            auto path=std::string("DLC1-AUTOSAVE5/")+practiceFileKeys[i]; int length=size(storage,path.c_str());
            if(length<28||length>64000000||(total+=length)>128000000) throw std::runtime_error("Unsupported Steam checkpoint file size.");
            files[i].resize(static_cast<size_t>(length)); if(read(storage,path.c_str(),files[i].data(),length)!=length) throw std::runtime_error("Steam checkpoint read failed.");
        }
        return files;
    };
    return api;
}
// Caller MUST be the paused, idle engine frame callback.
// Storage is supplied explicitly: production uses Steam's own local storage API
// to update its file cache/index, tests use a bounded recording implementation.
// After commit both copies remain; async load failures must not roll files back.
static PracticeCommitResult commitPracticeRestore(const std::filesystem::path& remote,const std::filesystem::path& bundle,
    const PracticeFiles& incoming,const PracticeFiles& current,const PracticeRestorePlan& plan,const std::string& account,const std::string& token,
    const PracticeStorageApi& storage,const std::function<bool()>& stillIdle,const std::function<void(const char*)>& step={}) {
    practiceAuthenticate(incoming,account); practiceAuthenticate(current,account);
    if(!practiceToken(token)||!practiceTargetMatches(current,plan)||!practiceFilesMatch(incoming,plan.sources)) throw std::runtime_error("Archive or target checkpoint changed after review. Nothing restored.");
    auto app=remote.parent_path(),target=remote/L"DLC1-AUTOSAVE5",original=app/(L".devil-live-original-"+std::wstring(token.begin(),token.end()));
    practiceNoLinks(app); practiceNoLinks(target); practiceNoLinks(original);
    if(std::filesystem::exists(original)||!storage.read||!storage.write) throw std::runtime_error("Restore transaction path already exists or storage unavailable.");
    if(storage.read()!=current||!stillIdle()) throw std::runtime_error("Steam storage and selected disk slot differ, or engine state changed. Nothing restored.");
    auto journal=bundle/L"RESTORE_PATHS.txt";
    {
        std::ofstream out(journal,std::ios::binary|std::ios::trunc);
        out<<"Target: "<<target.u8string()<<"\nOriginal: "<<original.u8string()<<"\nArchive: "<<plan.archive
           <<"\nOriginal four-file copy is retained. After interruption close game/Steam and restore ALL four files from Original together. Do not mix pairs or replace PROFILE/Steam metadata.\n";
        out.close(); if(!out) throw std::runtime_error("Cannot record restore recovery paths.");
    }
    writePracticeFiles(original,current);
    if(readPracticeFiles(original)!=current||!practiceTargetMatches(readPracticeFiles(target),plan)||!stillIdle()) throw std::runtime_error("Recovery copy, target or engine changed. Original slot untouched.");
    if(step) step("backed_up"); bool writing=false;
    try {
        if(!stillIdle()||storage.read()!=current||!practiceTargetMatches(readPracticeFiles(target),plan)) throw std::runtime_error("Engine/target changed before replacement.");
        writing=true;
        for(size_t i=0;i<4;++i) { if(!storage.write(i,incoming[i])) throw std::runtime_error("Steam checkpoint write failed."); if(step) step("file_written"); }
        if(storage.read()!=incoming||readPracticeFiles(target)!=incoming) throw std::runtime_error("Steam/disk checkpoint verification failed after write.");
    } catch(...) {
        if(writing) {
            for(size_t i=0;i<4;++i) storage.write(i,current[i]);
            bool restored=false; try { restored=storage.read()==current&&readPracticeFiles(target)==current; } catch(...) { restored=false; }
            if(!restored) throw std::runtime_error("Checkpoint rollback failed. Stop game/Steam and restore all four Original files using RESTORE_PATHS.txt.");
        }
        throw;
    }
    return {original,journal};
}
