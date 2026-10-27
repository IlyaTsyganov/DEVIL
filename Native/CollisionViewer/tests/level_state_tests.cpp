#include "../src/vulkan_layer.cpp"
static void expect(bool condition,const char* message) { if(!condition) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
template<class T> static void put(void* memory,size_t offset,T value) { memcpy(static_cast<unsigned char*>(memory)+offset,&value,sizeof(value)); }
struct StateFixture {
    const GameProfile& profile;
    unsigned char* image=nullptr; uintptr_t base=0;
    std::vector<unsigned char> system,map,player,playerPhysics,havok,trigger,wall,manager,triggerPhysics,wallPhysics,shape;
    struct Entry { uint32_t id,generation; uintptr_t entity,previous,next; };
    std::array<Entry,4> entries{};
    uintptr_t world=1,root=0;
    const char* level="game/test/state_lab";
    CheckpointLayout checkpoint{0x10,0x10d0,1072,8,16};
    EncounterFields encounter;
    StateFixture(const GameProfile& selected):profile(selected),system(0x60),map(selected.mapEntitiesOffset+16),player(0x240),playerPhysics(0x220),havok(0x210b8),
        trigger(0x240),wall(0x240),manager(0x400),triggerPhysics(0x220),wallPhysics(0x220),shape(0x200) {
        image=static_cast<unsigned char*>(VirtualAlloc(nullptr,std::max(profile.gameSystemRva,profile.rendererRva)+8,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); expect(image!=nullptr,"state fixture allocation"); base=reinterpret_cast<uintptr_t>(image);
        put(image,profile.gameSystemRva,reinterpret_cast<uintptr_t>(system.data())); put(system.data(),0x44,uint32_t(2)); put(system.data(),0x50,reinterpret_cast<uintptr_t>(map.data()));
        put(map.data(),profile.mapPlayerOffset,reinterpret_cast<uintptr_t>(player.data())); put(player.data(),0,base+profile.playerVtableRva); put(player.data(),0x1d0,reinterpret_cast<uintptr_t>(playerPhysics.data()));
        put(playerPhysics.data(),0x80,reinterpret_cast<uintptr_t>(havok.data())); put(havok.data(),0x210b0,reinterpret_cast<uintptr_t>(&world));
        put(map.data(),0x20,reinterpret_cast<uintptr_t>(map.data()+0x48)); put(map.data(),0x28,uint32_t(strlen(level))); memcpy(map.data()+0x48,level,strlen(level)+1);
        setCheckpoint("cp_before");
        auto type=[&](uintptr_t vtable,size_t getter,size_t info,const char* name) {
            put(image,vtable+16,base+getter); const unsigned char code[]{0x48,0x8d,0x05,0,0,0,0,0xc3}; memcpy(image+getter,code,8);
            put(image,getter+3,static_cast<int32_t>(info-getter-7)); put(image,info,reinterpret_cast<uintptr_t>(name));
        };
        type(0x10000,0x1000,0x2000,"idTrigger_Test"); type(profile.wallVtableRva,0x1100,0x2100,"idDynamicEntity"); type(profile.playerVtableRva,0x1200,0x2200,"idPlayer"); type(0x11000,0x1300,0x2300,"idEncounterManager");
        put(trigger.data(),0,base+0x10000); put(wall.data(),0,base+profile.wallVtableRva); put(manager.data(),0,base+0x11000);
        put(trigger.data(),0x48,reinterpret_cast<uintptr_t>("trigger/test/volume")); put(wall.data(),0x48,reinterpret_cast<uintptr_t>("test_wall")); put(manager.data(),0x48,reinterpret_cast<uintptr_t>("test_manager"));
        root=reinterpret_cast<uintptr_t>(shape.data()); put(shape.data(),0x20,uint16_t(2)); put(shape.data(),0x22,uint8_t(6));
        put(shape.data(),0x42,uint16_t(8)); put(shape.data(),0x44,uint16_t(0x70-0x42)); put(shape.data(),0x54,uint16_t(6)); put(shape.data(),0x56,uint16_t(0xf0-0x54));
        put(shape.data(),0x58,uint16_t(24)); put(shape.data(),0x5a,uint16_t(0x108-0x58));
        for(unsigned i=0;i<8;++i) { float point[]{i&1?.1f:-.1f,i&2?.1f:-.1f,i&4?.1f:-.1f,0}; memcpy(shape.data()+0x70+i*16,point,16); }
        const uint8_t corners[]{0,1,3,2,4,6,7,5,0,4,5,1,2,3,7,6,0,2,6,4,1,5,7,3}; memcpy(shape.data()+0x108,corners,24);
        for(unsigned i=0;i<6;++i) { put(shape.data(),0xf0+i*4,uint16_t(i*4)); shape[0xf0+i*4+2]=4; }
        auto physics=[&](std::vector<unsigned char>& data,std::vector<unsigned char>& entity,uint32_t contents,float x) {
            put(entity.data(),0x1d0,reinterpret_cast<uintptr_t>(data.data())); put(data.data(),0x14,uint32_t(2)); put(data.data(),0x44,contents); put(data.data(),0x88,uint32_t(1));
            put(data.data(),0x1c0,reinterpret_cast<uintptr_t>(&root)); float axis[]{1.f,0,0,0,1,0,0,0,1}; memcpy(data.data()+0x8c,axis,36); put(data.data(),0xb0,Vec3{x,0,.3f});
        };
        physics(triggerPhysics,trigger,0x4000,100); physics(wallPhysics,wall,8,102);
        entries={Entry{1,3,reinterpret_cast<uintptr_t>(player.data()),0,0},Entry{2,3,reinterpret_cast<uintptr_t>(trigger.data()),0,0},Entry{3,3,reinterpret_cast<uintptr_t>(wall.data()),0,0},Entry{4,3,reinterpret_cast<uintptr_t>(manager.data()),0,0}};
        put(map.data(),profile.mapEntitiesOffset,reinterpret_cast<uintptr_t>(entries.data())); put(map.data(),profile.mapEntitiesOffset+8,uint32_t(entries.size()));
        encounter.think={0x200,4,"int"}; encounter.name={0x40,48,"idStr"}; encounter.stringData={8,8,"char*"};
        encounter.enabled={0x300,1,"bool"}; encounter.paused={0x301,1,"bool"}; encounter.spawnPaused={0x302,1,"bool"}; encounter.waitingExit={0x303,1,"bool"};
        encounter.script={0x308,4,"int"}; encounter.commands={0x310,24,"idList < int >"}; put(manager.data(),0x200,int32_t(1)); manager[0x300]=1; put(manager.data(),0x318,uint32_t(1));
    }
    void setCheckpoint(const char* text) {
        size_t owner=0x20-checkpoint.data-checkpoint.mapName+checkpoint.checkpoint;
        put(map.data(),owner+8,reinterpret_cast<uintptr_t>(map.data()+owner+48)); put(map.data(),owner+16,static_cast<int32_t>(strlen(text))); memcpy(map.data()+owner+48,text,strlen(text)+1);
    }
    ~StateFixture() { if(image) VirtualFree(image,0,MEM_RELEASE); }
};
int main() {
    expect(validStateToken(std::string(32,'a'))&&!validStateToken("../bad")&&!validStateToken(std::string(32,'z')),"capture tokens cannot encode paths or malformed identities");
    LevelStateSnapshot saved;
    for(const auto* selected:kGameProfiles) {
        StateFixture fixture(*selected); LevelStateReader reader;
        auto before=reader.capture(WallMemory{},fixture.base,fixture.profile,std::string(32,'a'),fixture.encounter,fixture.checkpoint,{0x48,~0u,~0u});
        {
            std::vector<unsigned char> renderer(fixture.profile.viewsOffset+16),view(fixture.profile.viewMatrixOffset+64);
            uintptr_t viewPointer=reinterpret_cast<uintptr_t>(view.data());
            put(fixture.image,fixture.profile.rendererRva,reinterpret_cast<uintptr_t>(renderer.data()));
            put(renderer.data(),fixture.profile.viewsOffset,reinterpret_cast<uintptr_t>(&viewPointer)); put(renderer.data(),fixture.profile.viewsOffset+8,int32_t(1));
            GameCamera camera{}; camera.position[0]=1; camera.position[1]=2; camera.position[2]=3;
            camera.matrix[0]=1; camera.matrix[3]=-1; camera.matrix[6]=1; camera.matrix[7]=-3; camera.matrix[13]=1; camera.matrix[15]=-2;
            memcpy(view.data()+fixture.profile.viewPositionOffset,camera.position,12); memcpy(view.data()+fixture.profile.viewMatrixOffset,camera.matrix,64);
            InspectorPacket aimed; aimed.entity=before.volumes[0].ref; aimed.level=before.level; aimed.enabled=true; aimed.updated=GetTickCount64();
            auto point=captureBookmark(WallMemory{},fixture.base,fixture.profile,std::string(32,'f'),fixture.checkpoint,aimed,{0x48,~0u,~0u});
            expect(point.phase=="ready"&&point.eye.x==1&&point.eye.z==3&&point.body.z==0&&fabsf(point.forward.y-1)<.0001f&&point.checkpoint.name=="cp_before"&&point.entity.id==2,"bookmark keeps physical body separate from rendered eye/aim, checkpoint and map-scoped aimed entity");
            ++aimed.level.world;
            expect(!captureBookmark(WallMemory{},fixture.base,fixture.profile,std::string(32,'f'),fixture.checkpoint,aimed).entity,"bookmark rejects selection from a previous map instance");
            put(view.data(),fixture.profile.viewMatrixOffset,std::numeric_limits<float>::quiet_NaN());
            expect(captureBookmark(WallMemory{},fixture.base,fixture.profile,std::string(32,'f'),fixture.checkpoint).phase=="read_error","invalid rendered view cannot become a bookmark");
            put(fixture.image,fixture.profile.rendererRva,uintptr_t(0));
        }
        expect(before.phase=="ready"&&before.membershipComplete&&before.members.size()==4&&before.volumes.size()==2&&before.encounters.rows.size()==1,"state reader captures complete membership, volumes and managers on every profile");
        expect(before.checkpoint.status=="known"&&before.checkpoint.name=="cp_before"&&before.volumes[0].geometry=="exact","checkpoint is anchored to validated inline map/spawn strings, with captured exact geometry");
        fixture.entries[1].entity=0; put(fixture.wallPhysics.data(),0x44,uint32_t(0x40000000)); put(fixture.wallPhysics.data(),0xb0,Vec3{104,0,.3f}); put(fixture.manager.data(),0x200,int32_t(0)); fixture.setCheckpoint("cp_after");
        auto after=reader.capture(WallMemory{},fixture.base,fixture.profile,std::string(32,'b'),fixture.encounter,fixture.checkpoint,{0x48,~0u,~0u});
        expect(after.phase=="ready"&&after.members.size()==3&&after.volumes.size()==1&&after.volumes[0].status=="disabled"&&after.encounters.rows[0].known&&!after.encounters.rows[0].active()&&after.checkpoint.name=="cp_after","removal, previously known disabled wall, manager flag and checkpoint are independently captured");
        expect(before.volumes[0].mesh.geometry&&!before.volumes[0].mesh.geometry->lines.empty(),"Before retains deleted geometry independently of the live history");
        fixture.entries[1].entity=reinterpret_cast<uintptr_t>(fixture.trigger.data()); fixture.entries[1].generation=7;
        auto reused=reader.capture(WallMemory{},fixture.base,fixture.profile,std::string(32,'c'),fixture.encounter,fixture.checkpoint,{0x48,~0u,~0u});
        expect(reused.volumes[0].ref.generation==7,"reused entity slot keeps its new generation");
        put(fixture.trigger.data(),0x1d0,uintptr_t(0));
        auto unknown=reader.capture(WallMemory{},fixture.base,fixture.profile,std::string(32,'d'),fixture.encounter,fixture.checkpoint,{0x48,~0u,~0u});
        expect(unknown.phase=="ready"&&unknown.volumes[0].status=="unavailable"&&unknown.members.size()==4,"unreadable physics remains a present identity, not a deletion");
        put(fixture.map.data(),0x10d8+8,reinterpret_cast<uintptr_t>(fixture.map.data()));
        expect(readLoadedCheckpoint(WallMemory{},reinterpret_cast<uintptr_t>(fixture.map.data()),fixture.level,fixture.checkpoint).status=="read_error","foreign/unbounded checkpoint string is rejected");
        put(fixture.map.data(),fixture.profile.mapEntitiesOffset+8,uint32_t(32769));
        expect(reader.capture(WallMemory{},fixture.base,fixture.profile,std::string(32,'e'),fixture.encounter,fixture.checkpoint).phase=="read_error","invalid entity table cannot masquerade as an empty snapshot");
        GameCamera camera{}; camera.matrix[0]=camera.matrix[5]=camera.matrix[10]=.001f; camera.matrix[15]=1;
        StateHighlight draw; draw.level=before.level; draw.expires=encounterEpochMillis()+1000; draw.ignoreDistance=true;
        draw.outlines.push_back({before.volumes[0].mesh,false,true,{1,0,0,1}}); std::vector<Vertex> vertices;
        appendStateHighlight(camera,vertices,draw,before.level,encounterEpochMillis()); expect(!vertices.empty(),"captured deleted outline renders independently of normal filters and distance");
        vertices.clear(); draw.ignoreDistance=false; appendStateHighlight(camera,vertices,draw,before.level,encounterEpochMillis()); expect(vertices.empty(),"highlight distance option is respected");
        draw.ignoreDistance=true; draw.expires=0; appendStateHighlight(camera,vertices,draw,before.level,encounterEpochMillis()); expect(vertices.empty(),"expired highlight is not drawn");
        draw.expires=encounterEpochMillis()+1000; auto other=before.level; ++other.world; appendStateHighlight(camera,vertices,draw,other,encounterEpochMillis()); expect(vertices.empty(),"captured outline never crosses world identity");
        saved=std::move(before);
    }
    auto directory=std::filesystem::temp_directory_path()/(L"DEViL-LevelState-Test-"+std::to_wstring(GetCurrentProcessId())); std::filesystem::create_directories(directory);
    expect(writeLevelState(saved,directory/L"state.tsv"),"state protocol publishes atomically");
    std::ifstream input(directory/L"state.tsv"); std::string text((std::istreambuf_iterator<char>(input)),{}); input.close();
    expect(text.find("#DEVIL_LEVEL_STATE_V1\t")==0&&text.find("\nI\t2\t3\n")!=std::string::npos&&text.find("\nV\t2\t3\ttrigger/test/volume")!=std::string::npos&&text.find("\nM\t4\t3\ttest_manager")!=std::string::npos,"protocol includes scope, membership and copied values");
    std::filesystem::remove(directory/L"state.tsv"); std::filesystem::remove(directory);
    puts("PASS: complete state membership, disabled/deleted/unavailable/generation distinctions, loaded checkpoint validation, retained outlines/expiry/world scope and protocol on 22 profiles.");
}
