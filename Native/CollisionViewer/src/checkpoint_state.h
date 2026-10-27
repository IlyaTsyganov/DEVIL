#pragma once

struct CheckpointLayout {
    uint32_t mapName=~0u,checkpoint=~0u,size=0,data=~0u,length=~0u;
    explicit operator bool() const { return mapName!=~0u&&checkpoint!=~0u&&checkpoint>=mapName&&checkpoint-mapName<0x200000&&
        size>=48&&size<=8192&&data<0x20&&length<0x20&&data+8<=size&&length+4<=size; }
};
static std::shared_ptr<const CheckpointLayout> checkpointMetadata;
struct CheckpointObservation { std::string status="unavailable",name; };
// The audited map-name idStr data slot is also used by readLevelIdentity.
// Resolve the relative locations within idGameSpawnInfo through reflection;
// accept them only when BOTH strings are bounded self-owned idStrStatic data.
static CheckpointObservation readLoadedCheckpoint(const WallMemory& memory,uintptr_t map,const std::string& mapName,const CheckpointLayout& layout) {
    CheckpointObservation result;
    if(!layout||!map||layout.data>0x20) return result;
    uintptr_t mapString=map+0x20-layout.data;
    if(mapString<layout.mapName) return result;
    uintptr_t owner=mapString-layout.mapName+layout.checkpoint;
    auto read=[&](uintptr_t at,std::string& text) {
        uintptr_t pointer=0; int32_t length=0;
        if(!memory.get(at+layout.data,pointer)||!memory.get(at+layout.length,length)||length<0||length>1023||pointer<at||
           pointer>at+layout.size||uint64_t(length)+1>at+layout.size-pointer) return false;
        std::vector<char> bytes(static_cast<size_t>(length)+1);
        if(!memory.read(pointer,bytes.data(),bytes.size())||bytes.back()!=0) return false;
        for(int32_t i=0;i<length;++i) if(static_cast<unsigned char>(bytes[i])<32||static_cast<unsigned char>(bytes[i])>=127) return false;
        uintptr_t check=0; int32_t count=0;
        if(!memory.get(at+layout.data,check)||check!=pointer||!memory.get(at+layout.length,count)||count!=length) return false;
        text.assign(bytes.data(),static_cast<size_t>(length)); return true;
    };
    std::string anchored;
    if(!read(mapString,anchored)||anchored!=mapName||!read(owner,result.name)) { result.status="read_error"; return result; }
    result.status="known"; return result;
}
