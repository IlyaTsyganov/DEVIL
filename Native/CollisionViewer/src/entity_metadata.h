#pragma once

// Identity belongs to a map and a generation, never to a reusable pointer alone.
struct EntityReference {
    uintptr_t address=0, map=0, slot=0;
    uint32_t id=0, generation=0;
    std::string className;
    explicit operator bool() const { return address!=0; }
};

static std::string readEntityText(const WallMemory& memory,uintptr_t address,size_t limit=160) {
    std::string text;
    for(size_t i=0;address&&i<limit;++i) {
        unsigned char c=0;
        if(!memory.get(address+i,c)) return {};
        if(!c) return text;
        // DebugHUD interprets colour escapes; meathook's text wrapper also
        // formats its result a second time. Never pass '%' or '^' through.
        text.push_back(c<32||c==127||c=='%'||c=='^'?'?':static_cast<char>(c));
    }
    return {}; // Reject unterminated/unreadable strings.
}

static std::string readEntityClass(const WallMemory& memory,uintptr_t vtable,uintptr_t base,const GameProfile& profile) {
    uintptr_t getter=0,info=0,name=0; unsigned char code[8]{};
    if(vtable<base||vtable>=base+profile.imageSize||!memory.get(vtable+16,getter)||
       getter<base||getter>=base+profile.typeGetterLimitRva||!memory.read(getter,code,sizeof(code))||
       code[0]!=0x48||code[1]!=0x8d||code[2]!=0x05||code[7]!=0xc3) return {};
    int32_t delta=0; memcpy(&delta,code+3,4); info=getter+7+delta;
    return memory.get(info,name)?readEntityText(memory,name,96):std::string{};
}

enum class EntityStatus { Unavailable, Removed, Disabled, Enabled };
static EntityStatus volumeCollisionStatus(uint32_t type,uint32_t contents,bool disabled,uint32_t body,bool inWorld,bool trigger) {
    if(type!=2&&type!=7) return EntityStatus::Unavailable;
    if(disabled||(contents&0x40000000)||!(contents&(trigger?0x4000u:9u))||body==~0u||!inWorld) return EntityStatus::Disabled;
    return EntityStatus::Enabled;
}
static const char* entityStatusText(EntityStatus status) {
    switch(status) {
    case EntityStatus::Removed:return "Removed / slot reused";
    case EntityStatus::Disabled:return "Disabled";
    case EntityStatus::Enabled:return "Enabled";
    default:return "Unavailable";
    }
}
static EntityStatus readEntityStatus(const WallMemory& memory,const EntityReference& ref,uintptr_t base,const GameProfile& profile) {
    uintptr_t system=0,map=0,list=0; uint32_t count=0;
    if(!ref||!memory.get(base+profile.gameSystemRva,system)||!memory.get(system+0x50,map)||map!=ref.map||
       !memory.get(map+profile.mapEntitiesOffset,list)||!memory.get(map+profile.mapEntitiesOffset+8,count)||count>32768) return EntityStatus::Unavailable;
    constexpr size_t stride=32;
    if(ref.slot<list||(ref.slot-list)%stride||(ref.slot-list)/stride>=count) return EntityStatus::Removed;
    struct Slot { uint32_t id,generation; uintptr_t entity,previous,next; } slot{};
    if(!memory.get(ref.slot,slot)) return EntityStatus::Unavailable;
    if(slot.id!=ref.id||slot.generation!=ref.generation||slot.entity!=ref.address) return EntityStatus::Removed;
    unsigned char removed=0,disabled=0; uintptr_t physics=0; uint32_t contents=0,type=0,body=0;
    if(!memory.get(ref.address+0xa4,removed)) return EntityStatus::Unavailable;
    if(removed) return EntityStatus::Removed;
    if(!memory.get(ref.address+0x1d0,physics)||!memory.get(physics+0x44,contents)||!memory.get(physics+0x4f,disabled)||
       !memory.get(physics+0x14,type)||!memory.get(physics+0x88,body)||(type!=2&&type!=7)) return EntityStatus::Unavailable;
    bool trigger=ref.className.rfind("idTrigger",0)==0||ref.className.rfind("idEncounterTrigger",0)==0;
    bool present=true;
    if(type==7) {
        unsigned char inWorld=0;
        if(!memory.get(physics+0x210,inWorld)) return EntityStatus::Unavailable;
        present=inWorld!=0;
    }
    return volumeCollisionStatus(type,contents,disabled!=0,body,present,trigger);
}
