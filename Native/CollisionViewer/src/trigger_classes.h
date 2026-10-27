#pragma once

static bool validTriggerClassName(const std::string& name) {
    if(name.empty()||name.size()>128||!(name=="idTrigger"||name.rfind("idTrigger_",0)==0||name.rfind("idEncounterTrigger_",0)==0)) return false;
    return std::all_of(name.begin(),name.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
    });
}

static std::unordered_set<std::string> hiddenTriggerClasses;
static std::unordered_set<std::string> hiddenTriggerPaths;
struct TriggerPathLayout { uint32_t namePointer=~0u,definition=~0u,definitionNamePointer=~0u; };
static std::shared_ptr<const TriggerPathLayout> triggerPathMetadata;
static TriggerPathLayout triggerPathLayout() {
    auto layout=std::atomic_load(&triggerPathMetadata);
    return layout?*layout:TriggerPathLayout{};
}
static bool validTriggerPathPrefix(const std::string& prefix) {
    if(prefix.size()<2||prefix.size()>256||prefix.back()!='/') return false;
    size_t begin=0;
    for(size_t i=0;i<prefix.size();++i) {
        unsigned char c=prefix[i];
        if(c=='/') {
            auto part=prefix.substr(begin,i-begin);
            if(part.empty()||part=="."||part=="..") return false;
            begin=i+1;
        } else if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return false;
    }
    return true;
}
static std::string triggerNamePath(std::string name) {
    if(name.empty()) return {};
    for(char& c:name) { if(c=='\\') c='/'; else if(c>='A'&&c<='Z') c=char(c-'A'+'a'); }
    auto last=name.rfind('/');
    if(last==std::string::npos) return {};
    if(last+1==name.size()) return {}; // Reject malformed instance names.
    name.resize(last+1); return validTriggerPathPrefix(name)?name:std::string{};
}
static bool validTriggerPathKey(const std::string& key) {
    auto split=key.find('|');
    return split!=std::string::npos&&validTriggerClassName(key.substr(0,split))&&validTriggerPathPrefix(key.substr(split+1));
}
static std::unordered_set<std::string> readHiddenTriggerPaths(const std::wstring& path) {
    std::unordered_set<std::string> hidden;
    for(size_t capacity=4096;capacity<=1048576;capacity*=2) {
        std::vector<wchar_t> section(capacity);
        DWORD length=GetPrivateProfileSectionW(L"TriggerPaths",section.data(),static_cast<DWORD>(section.size()),path.c_str());
        if(length>=capacity-2) continue;
        for(size_t offset=0;offset<length&&section[offset];) {
            std::wstring row(section.data()+offset); offset+=row.size()+1; auto equals=row.find(L'=');
            if(equals==std::wstring::npos||equals>385||row.substr(equals+1)!=L"0") continue;
            std::string key;
            for(size_t i=0;i<equals;++i) { if(row[i]>127) { key.clear(); break; } key.push_back(static_cast<char>(row[i])); }
            if(hidden.size()<2048&&validTriggerPathKey(key)) hidden.insert(std::move(key));
        }
        break;
    }
    return hidden;
}
static std::unordered_set<std::string> readHiddenTriggerClasses(const std::wstring& path) {
    std::vector<wchar_t> section(65536);
    DWORD length=GetPrivateProfileSectionW(L"TriggerClasses",section.data(),static_cast<DWORD>(section.size()),path.c_str());
    std::unordered_set<std::string> hidden;
    for(size_t offset=0;offset<length&&section[offset];) {
        std::wstring row(section.data()+offset); offset+=row.size()+1;
        size_t equals=row.find(L'=');
        if(equals==std::wstring::npos||equals>128||row.substr(equals+1)!=L"0") continue;
        std::string name;
        for(size_t i=0;i<equals;++i) { if(row[i]>127) { name.clear(); break; } name.push_back(static_cast<char>(row[i])); }
        if(validTriggerClassName(name)) hidden.insert(std::move(name));
    }
    return hidden;
}
static bool triggerClassShown(const Mesh& mesh) {
    if(mesh.kind!=0) return true;
    if(hiddenTriggerClasses.count(mesh.entity.className)) return false;
    const auto key=mesh.entity.className+'|';
    auto hidden=[&](const std::string& path) {
        for(size_t i=0;i<path.size();++i)
            if(path[i]=='/'&&hiddenTriggerPaths.count(key+path.substr(0,i+1))) return true;
        return false;
    };
    if((mesh.triggerNameKnown&&hidden(mesh.triggerPath))||hidden(mesh.triggerDefinitionPath)) return false;
    return true;
}

// Only copied class/folder pairs are published. No entity addresses or full
// per-instance names are written to the discovery catalogue.
class TriggerPathDiscovery {
    std::filesystem::path path;
    std::unordered_set<std::string> seen;
    bool dirty=false; ULONGLONG lastAttempt=0;
    static std::filesystem::path defaultPath() {
        wchar_t directory[32768]{}; DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",directory,32768);
        return length&&length<32768?std::filesystem::path(directory)/L"DEViL"/L"trigger-paths.tsv":std::filesystem::path{};
    }
public:
    explicit TriggerPathDiscovery(std::filesystem::path output=defaultPath()):path(std::move(output)) {
        std::error_code error;
        if(path.empty()||std::filesystem::file_size(path,error)>1048576||error) return;
        std::ifstream input(path); std::string line;
        if(std::getline(input,line)&&line=="#DEVIL_TRIGGER_PATHS_V1") for(unsigned i=0;i<2048&&std::getline(input,line);++i) {
            auto tab=line.find('\t'); if(tab!=std::string::npos) line[tab]='|';
            if(validTriggerPathKey(line)) seen.insert(line);
        }
    }
    void publish(const std::unordered_set<std::string>& paths,ULONGLONG now=GetTickCount64()) {
        for(const auto& key:paths) if(seen.size()<2048&&validTriggerPathKey(key)) dirty=seen.insert(key).second||dirty;
        if(!dirty||path.empty()||now-lastAttempt<1000) return;
        lastAttempt=now; auto temporary=path; temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId());
        try {
            std::filesystem::create_directories(path.parent_path());
            std::vector<std::string> keys(seen.begin(),seen.end()); std::sort(keys.begin(),keys.end());
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc); output<<"#DEVIL_TRIGGER_PATHS_V1\n";
            for(auto key:keys) { key[key.find('|')]='\t'; output<<key<<'\n'; }
            output.close(); if(output&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)) dirty=false;
        } catch(...) { /* Optional discovery must not interrupt rendering. */ }
    }
};

// Only the background scanner writes this small class catalogue. It contains
// class names, never entity names/addresses, and does not control game logic.
class TriggerClassDiscovery {
    std::filesystem::path path;
    std::unordered_set<std::string> seen;
    bool dirty=false;
    ULONGLONG lastAttempt=0;
    static std::filesystem::path defaultPath() {
        wchar_t directory[32768]{};
        DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",directory,32768);
        if(!length||length>=32768) return {};
        return std::filesystem::path(directory)/L"DEViL"/L"trigger-classes.txt";
    }
public:
    explicit TriggerClassDiscovery(std::filesystem::path output=defaultPath()):path(std::move(output)) {}
    void publish(const std::unordered_set<std::string>& classes,ULONGLONG now=GetTickCount64()) {
        for(const auto& name:classes)
            if(seen.size()<512&&validTriggerClassName(name)) dirty=seen.insert(name).second||dirty;
        if(!dirty||path.empty()||now-lastAttempt<1000) return;
        lastAttempt=now;
        auto temporary=path; temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId());
        try {
            std::vector<std::string> names(seen.begin(),seen.end()); std::sort(names.begin(),names.end());
            {
                std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
                for(const auto& name:names) output<<name<<'\n';
                output.close(); if(!output) return;
            }
            if(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)) dirty=false;
        } catch(...) { /* Discovery is optional; retry later without interrupting rendering. */ }
    }
};
