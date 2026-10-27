#include "../src/vulkan_layer.cpp"
#include <cstdarg>
static void expect(bool value,const char* message) { if(!value) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
template<class T> static void put(void* memory,size_t offset,T value) { memcpy(static_cast<unsigned char*>(memory)+offset,&value,sizeof(value)); }
static std::unordered_map<unsigned,std::string> hudLines;
static unsigned nameCalls=0;
static bool nativeHudEnabled=false;
static unsigned visibilityCommands=0;
static void executeCommand(const char* command) {
    expect(!strcmp(command,"com_debugHUD 1"),"inspector only requests its native HUD visibility cvar");
    nativeHudEnabled=true; ++visibilityCommands;
}
static std::unordered_map<std::string,std::array<unsigned char,72>> classInfo;
static std::unordered_map<std::string,std::vector<unsigned char>> variables;
static void* findClass(const char* name) { auto it=classInfo.find(name); return it==classInfo.end()?nullptr:it->second.data(); }
static const char* entityName(void* entity) { ++nameCalls; const char* name=nullptr; memcpy(&name,static_cast<char*>(entity)+0x40,8); return name; }
static void addText(unsigned,unsigned y,unsigned,float,const char* format,...) {
    expect(nativeHudEnabled,"native HUD must be enabled before submitting its otherwise invisible text queue");
    char text[1024]; va_list args; va_start(args,format); vsprintf_s(text,format,args); va_end(args);
    expect(!strchr(text,'%')&&!strchr(text,'^'),"native HUD receives no second-format or colour escapes");
    hudLines[y]=text;
}
static void removeText(unsigned,unsigned y) { hudLines.erase(y); }
static void field(const char* type,unsigned classSize,const char* name,unsigned offset,unsigned size,const char* fieldType) {
    auto& rows=variables[type]; rows.resize(rows.size()+72);
    size_t at=rows.size()-72;
    put(rows.data(),at,reinterpret_cast<uintptr_t>(fieldType));
    put(rows.data(),at+16,reinterpret_cast<uintptr_t>(name)); put(rows.data(),at+24,offset); put(rows.data(),at+28,size);
    auto& info=classInfo[type]; put(info.data(),16,classSize); put(info.data(),32,reinterpret_cast<uintptr_t>(rows.data()));
}
static void endFields(const char* type) { auto& rows=variables[type]; rows.resize(rows.size()+72); put(classInfo[type].data(),32,reinterpret_cast<uintptr_t>(rows.data())); }
int main() {
    GameCamera camera{}; camera.matrix[0]=camera.matrix[5]=camera.matrix[14]=1;
    auto geometry=std::make_shared<Geometry>();
    geometry->triangles={{-1,-1,0},{1,-1,0},{0,1,0}};
    Mesh nearMesh{},farMesh{};
    nearMesh.geometry=farMesh.geometry=geometry;
    nearMesh.entity.address=1; farMesh.entity.address=2;
    nearMesh.matrix[11]=4; farMesh.matrix[11]=8;
    float bounds[]{-1,-1,4,1,1,4}; memcpy(nearMesh.bounds,bounds,24); bounds[2]=bounds[5]=8; memcpy(farMesh.bounds,bounds,24);
    std::vector<Candidate> candidates{{&farMesh,0},{&nearMesh,0}};
    auto hit=pickInspectorVolume(camera,candidates,20);
    expect(hit.mesh==&nearMesh&&hit.distance==4,"nearest exact volume surface wins independently of draw order");
    expect(!pickInspectorVolume(camera,candidates,3).mesh,"pick respects configured range");
    camera.position[0]=.9f; camera.position[1]=.9f;
    expect(!pickInspectorVolume(camera,candidates,20).mesh,"bounds hit without a triangle hit does not select empty space");
    camera.position[0]=camera.position[1]=0; camera.matrix[14]=-1;
    expect(!pickInspectorVolume(camera,candidates,20).mesh,"rear volumes are not selected");
    camera.position[2]=6;
    expect(pickInspectorVolume(camera,candidates,20).mesh==&nearMesh,"double-sided surfaces can be selected from inside a volume");
    nearMesh.entity={}; camera.position[2]=0; camera.matrix[14]=1;
    expect(pickInspectorVolume(camera,candidates,20).mesh==&farMesh,"anonymous world geometry is never presented as an entity");
    expect(encounterLabel("game/sp/e1m1_intro/e1m1_intro","barge_encounter_manager_barge_arena").find("Barge")!=std::string::npos,"confirmed document alias");
    expect(encounterLabel("game/sp/e1m2_battle/e1m2_battle","barge_encounter_manager_barge_arena").empty(),"aliases never leak between levels");
    expect(encounterLabel("game/sp/e1m1_intro/e1m1_intro","unknown").empty(),"unknown names are never guessed");
    expect(encounterLabel("game/master/e1m1_intro/e1m1_intro","barge_encounter_manager_barge_arena").empty(),"campaign labels excluded from master levels");
    expect(readEntityText(WallMemory{},reinterpret_cast<uintptr_t>("name%08x^1")).find('%')==std::string::npos,"entity strings are sanitised");
    field("idEncounterTrigger_Commit",0x1000,"owners",0x300,24,"idList < idManagedClassPtr < idEncounterManager > >"); endFields("idEncounterTrigger_Commit");
    field("idManagedClassPtr < idEncounterManager >",32,"ptr",8,8,"idEncounterManager*");
    field("idManagedClassPtr < idEncounterManager >",32,"spawnId",0,4,"idSpawnId"); endFields("idManagedClassPtr < idEncounterManager >");
    field("idManagedClass",32,"objectNumber",16,4,"int"); endFields("idManagedClass");
    MeathookHudApi api{}; api.executeCommand=executeCommand; api.findClass=findClass; api.entityName=entityName; api.addText=addText; api.removeText=removeText;
    for(const GameProfile* selected:kGameProfiles) {
        const auto& profile=*selected;
        auto* base=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x7000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        expect(base!=nullptr,"engine fixture allocation"); uintptr_t b=reinterpret_cast<uintptr_t>(base);
        std::vector<unsigned char> system(0x60),map(0x62000),player(0x50000),physics(0x220),havok(0x210c0),entity(0x1000),manager(0x240);
        const std::string mapName="game/sp/e1m1_intro/e1m1_intro";
        uintptr_t root=0x123456;
        put(base,profile.gameSystemRva,reinterpret_cast<uintptr_t>(system.data())); put(system.data(),0x44,uint32_t(2));
        put(system.data(),0x50,reinterpret_cast<uintptr_t>(map.data()));
        put(map.data(),0x20,reinterpret_cast<uintptr_t>(mapName.c_str())); put(map.data(),0x28,uint32_t(mapName.size()));
        put(map.data(),profile.mapPlayerOffset,reinterpret_cast<uintptr_t>(player.data()));
        put(player.data(),0,b+profile.playerVtableRva); put(player.data(),0x1d0,reinterpret_cast<uintptr_t>(physics.data()));
        put(physics.data(),0x80,reinterpret_cast<uintptr_t>(havok.data())); put(havok.data(),0x210b0,reinterpret_cast<uintptr_t>(&root));
        struct Slot { uint32_t id=11,generation=22; uintptr_t entity=0,previous=0,next=0; } slot;
        slot.entity=reinterpret_cast<uintptr_t>(entity.data());
        put(map.data(),profile.mapEntitiesOffset,reinterpret_cast<uintptr_t>(&slot)); put(map.data(),profile.mapEntitiesOffset+8,uint32_t(1));
        put(entity.data(),0x1d0,reinterpret_cast<uintptr_t>(physics.data())); put(physics.data(),0x44,uint32_t(0x4000));
        put(physics.data(),0x14,uint32_t(2)); put(physics.data(),0x88,uint32_t(1));
        put(entity.data(),0x40,"volume%08x^1");
        // A reflected owners list points to a real, generation-validated manager.
        std::array<unsigned char,32> ownerLink{};
        put(ownerLink.data(),0,uint32_t(33)); put(ownerLink.data(),8,reinterpret_cast<uintptr_t>(manager.data()));
        put(entity.data(),0x300,reinterpret_cast<uintptr_t>(ownerLink.data())); put(entity.data(),0x308,uint32_t(1));
        put(manager.data(),16,uint32_t(33)); put(manager.data(),0x40,"barge_encounter_manager_barge_arena");
        put(manager.data(),0,b+0x200000); put(base,0x200000+16,b+0x200100);
        const unsigned char getter[]{0x48,0x8d,0x05,0,0,0,0,0xc3}; memcpy(base+0x200100,getter,8);
        put(base,0x200103,int32_t(0x200200-0x200107)); put(base,0x200200,"idEncounterManager");
        InspectorPacket packet; packet.enabled=true; packet.base=b; packet.profile=selected; packet.updated=GetTickCount64();
        packet.level=readLevelIdentity(WallMemory{},b,profile); packet.distance=4;
        packet.entity={slot.entity,reinterpret_cast<uintptr_t>(map.data()),reinterpret_cast<uintptr_t>(&slot),11,22,"idEncounterTrigger_Commit"};
        expect(bool(packet.level),"valid engine level identity");
        nativeHudEnabled=false;
        const unsigned beforeCommands=visibilityCommands;
        MeathookHud hud(&api); hud.publish(packet); hud.onGameFrame();
        expect(nativeHudEnabled&&visibilityCommands==beforeCommands+1,"default com_debugHUD=0 is activated automatically");
        expect(hudLines[300]=="Name: volume?08x?1"&&hudLines[320]=="Class: idEncounterTrigger_Commit","native HUD gets sanitised name and class");
        expect(hudLines[340]=="ID: 11 / generation 22"&&hudLines[360]=="State: Enabled","native HUD gets identity and collision state");
        expect(hudLines[400].find("Barge")!=std::string::npos&&hudLines[420].find("barge_encounter_manager_barge_arena")!=std::string::npos,"linked manager resolves to document name without renaming the selected trigger");
        put(physics.data(),0x44,uint32_t(0x40000000)); hud.onGameFrame(); expect(hudLines[360]=="State: Disabled","HUD refreshes disabled state independently of geometry snapshot");
        expect(visibilityCommands==beforeCommands+1,"visibility command is not executed every frame");
        put(physics.data(),0x44,uint32_t(0)); hud.onGameFrame(); expect(hudLines[360]=="State: Disabled","missing trigger contents is disabled");
        put(physics.data(),0x44,uint32_t(0x4000)); put(physics.data(),0x14,uint32_t(7));
        hud.onGameFrame(); expect(hudLines[360]=="State: Disabled","rigid volume outside physics world is disabled");
        put(physics.data(),0x14,uint32_t(2));
        const unsigned calls=nameCalls; ++slot.generation; hud.onGameFrame();
        expect(nameCalls==calls&&hudLines[360].find("Removed")!=std::string::npos,"reused slot never reaches entity API"); --slot.generation;
        packet.historicalGeometry=true; packet.geometryState=EntityStatus::Removed; packet.geometryAt=GetTickCount64()-1200;
        ++slot.generation; hud.publish(packet); hud.onGameFrame();
        expect(hudLines[460].find("last snapshot")!=std::string::npos,"historical geometry is explicitly labelled in the native HUD");
        --slot.generation; packet.historicalGeometry=false; packet.geometryAt=0; hud.publish(packet);
        put(player.data(),profile.playerHudOffset+profile.hudPausedOffset,static_cast<unsigned char>(1)); hud.onGameFrame(); expect(hudLines.empty(),"pause clears all native HUD rows");
        put(player.data(),profile.playerHudOffset+profile.hudPausedOffset,static_cast<unsigned char>(0));
        nativeHudEnabled=false; // A menu/console or another tool can change it.
        hud.onGameFrame(); expect(!hudLines.empty(),"HUD resumes after pause");
        expect(nativeHudEnabled&&visibilityCommands==beforeCommands+2,"resuming activates HUD again after external visibility changes");
        packet.updated=GetTickCount64()-501; hud.publish(packet); hud.onGameFrame(); expect(hudLines.empty(),"stale selection clears instead of lingering on screen");
        packet.updated=GetTickCount64(); hud.publish(packet); hud.onGameFrame(); hud.publish({}); hud.onGameFrame(); expect(hudLines.empty(),"disabling inspector removes its persistent text");
        expect(nativeHudEnabled,"inspector does not turn off the shared HUD used by performance metrics and other tools");
        VirtualFree(base,0,MEM_RELEASE);
    }
    puts("PASS: exact ray selection, 188 scoped aliases, reflected encounter owners, native HUD lifecycle, stale slots and collision states on all 22 profiles. Engine HUD calls used a recording backend; no game launched.");
}
