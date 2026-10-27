#pragma once
#include "meathook_api.h"

struct InspectorField { uint32_t offset=~0u,size=0; std::string type; explicit operator bool() const { return offset!=~0u; } };
// meathook exposes classTypeInfo_t through findClass. Only inspect its public
// reflected name/size/variable array; never invoke an engine property getter.
static InspectorField readInspectorField(const WallMemory& memory,uintptr_t info,const char* wanted) {
    uintptr_t rows=0;
    uint32_t classSize=0;
    if(!info||!memory.get(info+16,classSize)||classSize>0x200000||!memory.get(info+32,rows)) return {};
    for(unsigned i=0;i<2000;++i) {
        uintptr_t name=0,fieldType=0,ops=0; int32_t offset=0,size=0; uintptr_t row=rows+size_t(i)*72;
        if(!memory.get(row,fieldType)||!fieldType||!memory.get(row+16,name)) break;
        if(readEntityText(memory,name,128)!=wanted) continue;
        if(!memory.get(row+24,offset)||!memory.get(row+28,size)||offset<0||size<=0||uint64_t(offset)+size>classSize) return {};
        memory.get(row+8,ops);
        return {static_cast<uint32_t>(offset),static_cast<uint32_t>(size),readEntityText(memory,fieldType,160)+readEntityText(memory,ops,160)};
    }
    return {};
}
static InspectorField inspectorField(const MeathookHudApi& api,const std::string& type,const char* wanted) {
    return readInspectorField(WallMemory{},reinterpret_cast<uintptr_t>(api.findClass(type.c_str())),wanted);
}
static uint32_t triggerNamePointerOffset(const InspectorField& name,const InspectorField& data) {
    return name&&name.type.rfind("idStr",0)==0&&data&&data.size==8&&data.type.find("char")!=std::string::npos&&
        data.type.find('*')!=std::string::npos&&uint64_t(data.offset)+8<=name.size?name.offset+data.offset:~0u;
}
static TriggerPathLayout resolveTriggerPathLayout(const std::function<InspectorField(const std::string&,const char*)>& lookup) {
    TriggerPathLayout result;
    result.namePointer=triggerNamePointerOffset(lookup("idEntity","name"),lookup("idStr","data"));
    auto definition=lookup("idEntity","entityDef"),name=lookup("idResource","name"),text=lookup("idAtomicString","str");
    if(definition&&definition.size==8&&definition.type.find("idDeclEntityDef")!=std::string::npos&&definition.type.find('*')!=std::string::npos&&
       name&&name.type=="idAtomicString"&&text&&text.size==8&&text.type.find("char")!=std::string::npos&&text.type.find('*')!=std::string::npos&&
       uint64_t(text.offset)+8<=name.size) {
        result.definition=definition.offset; result.definitionNamePointer=name.offset+text.offset;
    }
    return result;
}
static MotionLayout resolveMotionLayout(const std::function<InspectorField(const std::string&,const char*)>& lookup) {
    MotionLayout layout;
    auto field=[&](const char* type,const char* name,const char* expected,uint32_t size) {
        auto value=lookup(type,name);
        return value&&value.size==size&&value.type==expected?value:InspectorField{};
    };
    layout.physics=field("idEntity","physics","idPhysics*",8).offset;
    auto copy=[&](const char* type,const char* name,const char* expected,uint32_t size,uint32_t& into) {
        auto value=field(type,name,expected,size); into=value.offset;
        if(value) layout.bytes=std::max(layout.bytes,value.offset+value.size);
    };
    copy("idPhysics","type","physicsType_t",4,layout.type); copy("idPhysics","mapInstance","idMapInstance*",8,layout.map);
    copy("idHavokPhysics_Base","bodyOrigin","idVec3",12,layout.origin); copy("idHavokPhysics_Base","bodyAxis","idMat3",36,layout.axis);
    copy("idHavokPhysics_Base","bodyBoundsLocal","idBounds",24,layout.localBounds); copy("idHavokPhysics_Base","bodyBoundsWorld","idBounds",24,layout.worldBounds);
    copy("idHavokPhysics_Base","bodyLinearVelocity","idVec3",12,layout.velocity);
    copy("idHavokPhysics_Base","shape","idHavokShapePointer",8,layout.shape);
    layout.shapePointer=field("idHavokShapePointer","pointer","idHavokShape*",8).offset;
    layout.shapeObject=field("idHavokShape","shape","hknpShape*",8).offset;
    copy("idHavokPhysics_Player","currentHavokBodyShapeIndex","int",4,layout.shapeIndex);
    copy("idHavokPhysics_Player","gravityNormal","idVec3",12,layout.up);
    auto current=lookup("idHavokPhysics_Player","current"),grounded=field("playerPState_t","groundPlane","bool",1);
    if(current&&current.type=="playerPState_t"&&grounded&&grounded.offset+grounded.size<=current.size) {
        layout.grounded=current.offset+grounded.offset; layout.bytes=std::max(layout.bytes,layout.grounded+1);
    }
    auto trace=lookup("idHavokPhysics_Player","groundTrace"),contact=lookup("trace_t","c");
    auto fraction=field("trace_t","fraction","float",4),point=field("contactInfo_t","point","idVec3",12),normal=field("contactInfo_t","normal","idVec3",12);
    if(trace&&trace.type=="trace_t"&&contact&&contact.type=="contactInfo_t"&&fraction&&point&&normal&&
       fraction.offset+4<=trace.size&&contact.offset+contact.size<=trace.size&&point.offset+12<=contact.size&&normal.offset+12<=contact.size) {
        layout.fraction=trace.offset+fraction.offset; layout.point=trace.offset+contact.offset+point.offset;
        layout.normal=trace.offset+contact.offset+normal.offset; layout.bytes=std::max(layout.bytes,trace.offset+trace.size);
    }
    return layout;
}
static CheckpointLayout resolveCheckpointLayout(const std::function<InspectorField(const std::string&,const char*)>& lookup) {
    auto map=lookup("idGameSpawnInfo","mapName"),checkpoint=lookup("idGameSpawnInfo","checkpointName");
    auto data=lookup("idStr","data"),length=lookup("idStr","len");
    if(!map||!checkpoint||map.type.rfind("idStrStatic",0)!=0||map.type!=checkpoint.type||map.size!=checkpoint.size||
       !data||data.size!=8||data.type!="char*"||!length||length.size!=4||length.type!="int") return {};
    CheckpointLayout result{map.offset,checkpoint.offset,map.size,data.offset,length.offset}; return result?result:CheckpointLayout{};
}

// A mailbox carries values only. Never call engine HUD methods from Vulkan's
// presentation thread, or hold its global lock in an engine callback.
struct InspectorPacket {
    EntityReference entity;
    LevelIdentity level;
    uintptr_t base=0;
    const GameProfile* profile=nullptr;
    ULONGLONG updated=0;
    float distance=0;
    bool enabled=false;
    bool historicalGeometry=false;
    ULONGLONG geometryAt=0;
    EntityStatus geometryState=EntityStatus::Enabled;
};
struct InspectorEncounter { std::string label,technical; };
static InspectorEncounter inspectorEncounter(const MeathookHudApi& api,const InspectorPacket& current,const std::string& entityName) {
    InspectorEncounter result; result.label=encounterLabel(current.level.name,entityName);
    const auto& ref=current.entity;
    if(ref.className.rfind("idEncounterTrigger_",0)!=0) return result;
    static std::unordered_map<std::string,InspectorField> fields;
    auto found=fields.find(ref.className);
    if(found==fields.end()) found=fields.emplace(ref.className,inspectorField(api,ref.className,"owners")).first;
    const InspectorField& owners=found->second;
    if(!owners||owners.size!=24||owners.type.rfind("idList",0)!=0||owners.type.find("idEncounterManager")==std::string::npos) return result;
    // Resolve layout from reflection instead of assuming manager/trigger
    // offsets are identical between executable versions.
    static const InspectorField ptr=inspectorField(api,"idManagedClassPtr < idEncounterManager >","ptr");
    static const InspectorField spawn=inspectorField(api,"idManagedClassPtr < idEncounterManager >","spawnId");
    static const InspectorField objectNumber=inspectorField(api,"idManagedClass","objectNumber");
    if(!ptr||ptr.size!=8||!spawn||spawn.size!=4||!objectNumber||objectNumber.size!=4) return result;
    WallMemory memory; uintptr_t data=0; uint32_t count=0;
    if(!memory.get(ref.address+owners.offset,data)||!memory.get(ref.address+owners.offset+8,count)||count>64) return result;
    for(uint32_t i=0;i<count;++i) {
        uintptr_t owner=0,vtable=0; uint32_t expected=0,actual=0; unsigned char removed=1;
        uintptr_t link=data+size_t(i)*32;
        if(!memory.get(link+ptr.offset,owner)||!owner||!memory.get(link+spawn.offset,expected)||
           !memory.get(owner+objectNumber.offset,actual)||expected!=actual||!memory.get(owner+0xa4,removed)||removed||
           !memory.get(owner,vtable)||readEntityClass(memory,vtable,current.base,*current.profile).rfind("idEncounterManager",0)!=0) continue;
        std::string name=readEntityText(memory,reinterpret_cast<uintptr_t>(api.entityName(reinterpret_cast<void*>(owner))));
        if(name.empty()) continue;
        if(!result.technical.empty()) result.technical+="; ";
        result.technical+=name;
        std::string label=encounterLabel(current.level.name,name);
        if(!label.empty()&&result.label.find(label)==std::string::npos) {
            if(!result.label.empty()) result.label+="; ";
            result.label+=label;
        }
        // Keep DebugHUD lines bounded even when a volume has many owners.
        if(result.technical.size()>240||result.label.size()>240) break;
    }
    return result;
}
class MeathookHud {
    std::mutex mutex;
    InspectorPacket packet;
    const MeathookHudApi* api=nullptr;
    ULONGLONG retryAt=0;
    ULONGLONG reportAt=0;
    bool registered=false,visible=false,hudRequested=false;
    enum class Phase : unsigned { Waiting, Disabled, Stale, LevelChanged, Menu, Console, Active };
    std::atomic<Phase> phase{Phase::Waiting};
    std::atomic<uint64_t> callbacks{0},activeFrames{0},submittedRows{0},visibilityRequests{0};
    std::array<std::string,10> previous;
    static constexpr unsigned x=24,y=280,lineHeight=20;
    void clear() {
        hudRequested=false;
        if(!visible) return;
        for(unsigned i=0;i<previous.size();++i) api->removeText(x,y+i*lineHeight);
        previous={}; visible=false;
    }
    static void frameCallback(void* user) { static_cast<MeathookHud*>(user)->onGameFrame(); }
    void frame() {
        ++callbacks;
        InspectorPacket current;
        { std::lock_guard<std::mutex> lock(mutex); current=packet; }
        Phase state=Phase::Active;
        if(!current.enabled||!current.profile) state=Phase::Disabled;
        else if(GetTickCount64()-current.updated>500) state=Phase::Stale;
        else if(readLevelIdentity(WallMemory{},current.base,*current.profile)!=current.level) state=Phase::LevelChanged;
        else if(!gameplayVisible(WallMemory{},current.base,*current.profile)) state=Phase::Menu;
        else if(readConsoleFraction(WallMemory{},current.base,*current.profile)>0) state=Phase::Console;
        phase=state;
        if(state!=Phase::Active) { clear(); return; }
        ++activeFrames;
        if(!hudRequested) {
            // The released meathook v7.2 can enqueue persistent text while
            // com_debugHUD is 0. Its render hook does not enable that cvar;
            // the engine then skips drawing the entire queue. Execute on the
            // game frame thread, once per active interval, before adding rows.
            api->executeCommand("com_debugHUD 1");
            ++visibilityRequests;
            hudRequested=true;
        }
        std::array<std::string,10> lines;
        lines[0]="DEViL | Volume inspector";
        if(!current.entity) lines[1]="Aim at a trigger or invisible wall";
        else {
            const auto& ref=current.entity;
            EntityStatus status=readEntityStatus(WallMemory{},ref,current.base,*current.profile);
            // Validate the map slot before calling meathook with an entity.
            std::string name;
            if(status==EntityStatus::Enabled||status==EntityStatus::Disabled)
                name=readEntityText(WallMemory{},reinterpret_cast<uintptr_t>(api->entityName(reinterpret_cast<void*>(ref.address))));
            lines[1]="Name: "+(name.empty()?std::string("<unavailable>"):name);
            lines[2]="Class: "+(ref.className.empty()?std::string("<unavailable>"):ref.className);
            lines[3]="ID: "+std::to_string(ref.id)+" / generation "+std::to_string(ref.generation);
            lines[4]=std::string("State: ")+entityStatusText(status);
            lines[5]=std::string("Role: ")+volumeRole(ref.className);
            if(status==EntityStatus::Enabled||status==EntityStatus::Disabled) {
                auto encounter=inspectorEncounter(*api,current,name);
                lines[6]=encounter.label;
                if(!encounter.technical.empty()) lines[7]="Manager: "+encounter.technical;
            }
            char distance[80]; sprintf_s(distance,"Ray distance: %.2f game units",current.distance); lines[8]=distance;
            if(current.historicalGeometry) {
                ULONGLONG age=GetTickCount64()>=current.geometryAt?GetTickCount64()-current.geometryAt:0;
                char snapshot[150]; sprintf_s(snapshot,"Geometry: last snapshot, %.1fs old (%s)",age/1000.0,entityStatusText(current.geometryState)); lines[9]=snapshot;
            } else if(current.geometryAt) lines[9]="Geometry: current sample";
        }
        for(unsigned i=0;i<lines.size();++i) {
            if(lines[i]==previous[i]&&visible) continue;
            if(lines[i].empty()) api->removeText(x,y+i*lineHeight);
            else {
                api->addText(x,y+i*lineHeight,i?0xffeeeeeeu:0xffffd040u,0.8f,"%s",lines[i].c_str());
                ++submittedRows;
            }
        }
        previous=std::move(lines); visible=true;
    }
public:
    explicit MeathookHud(const MeathookHudApi* backend=nullptr):api(backend) {}
    void onGameFrame() { frame(); }
    // Bind only an already-loaded meathook, never install/load a DLL for the user.
    bool connect() {
        if(registered) return true;
        if(GetTickCount64()<retryAt) return false;
        retryAt=GetTickCount64()+2000;
        HMODULE module=GetModuleHandleW(L"XINPUT1_3.dll");
        if(!module) return false;
        auto getApi=reinterpret_cast<const MeathookHudApi* (*)()>(GetProcAddress(module,"mh_api_get"));
        if(!getApi) return false;
        const auto* candidate=getApi();
        MeathookHudApi copy{};
        if(!candidate||!WallMemory{}.get(reinterpret_cast<uintptr_t>(candidate),copy)||
           !copy.executeCommand||!copy.findClass||!copy.entityName||!copy.addText||!copy.removeText||!copy.registerPreFrame) return false;
        // The public API has no callback removal. Pin both modules for the
        // process lifetime so a retained callback can never point into freed code.
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
             reinterpret_cast<LPCWSTR>(&frameCallback),&pinned)||
           !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
             reinterpret_cast<LPCWSTR>(getApi),&pinned)) return false;
        api=candidate;
        api->registerPreFrame(&frameCallback,this);
        registered=true;
        note("Inspector HUD: meathook API connected; game-frame callback registered, awaiting HUD activation.");
        return true;
    }
    // Called only by the presentation thread. Engine callbacks update counters
    // without using the renderer's logger or taking its global mutex.
    void report() {
        if(!registered||GetTickCount64()<reportAt) return;
        reportAt=GetTickCount64()+5000;
        const char* states[]{"waiting","disabled","stale selection","level changed","menu/pause","console","active"};
        char message[300];
        sprintf_s(message,"Inspector HUD: callbacks=%llu activeFrames=%llu submittedRows=%llu com_debugHUD_requests=%llu state=%s.",
            static_cast<unsigned long long>(callbacks.load()),static_cast<unsigned long long>(activeFrames.load()),
            static_cast<unsigned long long>(submittedRows.load()),static_cast<unsigned long long>(visibilityRequests.load()),states[static_cast<unsigned>(phase.load())]);
        note(message);
    }
    void publish(InspectorPacket next) {
        std::lock_guard<std::mutex> lock(mutex); packet=std::move(next);
    }
    InspectorPacket selection() { std::lock_guard<std::mutex> lock(mutex); return packet; }
};
// Intentionally process-lifetime: meathook retains its callback without an
// unregister API. Do not destroy a mutex while the engine can still call it.
static MeathookHud& inspectorHud() { static auto* hud=new MeathookHud; return *hud; }
