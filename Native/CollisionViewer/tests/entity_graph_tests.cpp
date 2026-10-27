#include "../src/vulkan_layer.cpp"
static void expect(bool ok,const char* message) { if(!ok) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
template<class T> static void put(void* memory,size_t offset,T value) { memcpy(static_cast<unsigned char*>(memory)+offset,&value,sizeof(value)); }
int main() {
    const std::string managed="idManagedClassPtr < idEncounterManager >",listType="idList < idManagedClassPtr < idEncounterManager > , TAG_IDLIST , false >";
    std::unordered_map<std::string,GraphType> schemas;
    schemas["idEntity"]={512,"idManagedClass",{{"name","idStr","",0x40,48}},true};
    schemas["idStr"]={48,"",{{"data","char","*",8,8}},true};
    schemas["idManagedClass"]={64,"",{{"objectNumber","unsigned int","",0x34,4}},true};
    schemas["idEncounterTrigger_Commit"]={2048,"idEntity",{{"owners",listType,"",0x100,24},{"integerThatLooksLikePointer","uint64_t","",0x128,8}},true};
    schemas["idEncounterManager"]={2048,"idEntity",{{"target","idEntity","*",0x110,8}},true};
    schemas[listType]={24,"",{{"list",managed,"*",0,8},{"num","int","",8,4}},true};
    schemas[managed]={32,"",{{"spawnId","idSpawnId","",0,4},{"ptr","idEncounterManager","*",8,8}},true};
    auto lookup=[&](const std::string& type)->const GraphType& { static const GraphType empty; auto found=schemas.find(type); return found==schemas.end()?empty:found->second; };
    unsigned profiles=0;
    for(const auto* selected:kGameProfiles) {
        const auto& profile=*selected; ++profiles;
        auto* base=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x7000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        expect(base!=nullptr,"base fixture allocation"); uintptr_t b=reinterpret_cast<uintptr_t>(base);
        std::vector<unsigned char> system(0x60),map(std::max(profile.mapEntitiesOffset+16,profile.mapPlayerOffset+8)),player(0x200),physics(0x220),havok(0x210b8);
        uintptr_t world=42;
        put(base,profile.gameSystemRva,reinterpret_cast<uintptr_t>(system.data())); put(system.data(),0x44,uint32_t(2));
        put(system.data(),0x50,reinterpret_cast<uintptr_t>(map.data())); const char* mapName="game/dlc/e4m1_rig/e4m1_rig";
        put(map.data(),0x20,reinterpret_cast<uintptr_t>(mapName)); put(map.data(),0x28,uint32_t(strlen(mapName)));
        put(map.data(),profile.mapPlayerOffset,reinterpret_cast<uintptr_t>(player.data())); put(player.data(),0x1d0,reinterpret_cast<uintptr_t>(physics.data()));
        put(physics.data(),0x80,reinterpret_cast<uintptr_t>(havok.data())); put(havok.data(),0x210b0,reinterpret_cast<uintptr_t>(&world));
        struct Entry { uint32_t id,generation; uintptr_t entity,previous,next; };
        std::array<std::array<unsigned char,2048>,3> entities{}; std::array<Entry,3> entries{};
        const char* names[]{"arena_trigger","starting_platform_encounter_manager_1","gate_entity"};
        const char* types[]{"idEncounterTrigger_Commit","idEncounterManager","idEntity"};
        for(size_t i=0;i<3;++i) {
            auto vtable=0x200000+i*0x100,getter=0x210000+i*0x100,info=0x220000+i*0x100;
            unsigned char code[]{0x48,0x8d,0x05,0,0,0,0,0xc3}; memcpy(base+getter,code,8);
            put(base,getter+3,int32_t(info-getter-7)); put(base,vtable+16,b+getter); put(base,info,reinterpret_cast<uintptr_t>(types[i]));
            put(entities[i].data(),0,b+vtable); put(entities[i].data(),0x48,reinterpret_cast<uintptr_t>(names[i])); put(entities[i].data(),0x34,uint32_t(100+i));
            entries[i]={uint32_t(100+i),3,reinterpret_cast<uintptr_t>(entities[i].data()),0,0};
        }
        put(map.data(),profile.mapEntitiesOffset,reinterpret_cast<uintptr_t>(entries.data())); put(map.data(),profile.mapEntitiesOffset+8,uint32_t(3));
        std::array<unsigned char,32> owner{}; put(owner.data(),0,uint32_t(91)); put(owner.data(),8,entries[1].entity);
        put(entities[0].data(),0x100,reinterpret_cast<uintptr_t>(owner.data())); put(entities[0].data(),0x108,int32_t(1));
        put(entities[0].data(),0x128,entries[2].entity); put(entities[1].data(),0x110,entries[2].entity);
        auto level=readLevelIdentity(WallMemory{},b,profile); expect(bool(level),"level fixture uses production map/world reader");
        EntityGraphReader reader; InspectorPacket aim; aim.enabled=true; aim.updated=GetTickCount64(); aim.level=level;
        aim.entity={entries[0].entity,level.map,reinterpret_cast<uintptr_t>(entries.data()),100,3,types[0]};
        auto graph=reader.capture(WallMemory{},b,profile,level,lookup,aim);
        expect(graph.phase=="live"&&graph.nodes.size()==3&&graph.edges.size()==2,"managed owners and direct entity links form a two-hop graph");
        expect(graph.edges[0].from==0&&graph.edges[0].to==1&&graph.edges[0].field=="owners[0]"&&graph.edges[0].evidence=="Managed pointer / map slot checked","owner edge retains its exact path with different reference-token and object-number domains");
        expect(graph.edges[1].from==1&&graph.edges[1].to==2&&graph.edges[1].field=="target","manager points to the declared entity target");
        expect(graph.selected&&graph.selectedId==100&&graph.nodes[1].label.find("Landing Pad")!=std::string::npos,"crosshair identity and scoped encounter labels are retained");
        put(owner.data(),8,uintptr_t(0x12345)); graph=reader.capture(WallMemory{},b,profile,level,lookup);
        expect(graph.edges.size()==1&&graph.unavailable>0,"managed pointers outside the current map entity table are rejected");
        put(owner.data(),8,entries[1].entity); entities[1][0xa4]=1; graph=reader.capture(WallMemory{},b,profile,level,lookup);
        expect(graph.nodes.size()==2&&graph.edges.empty(),"removed managers cannot remain graph endpoints"); entities[1][0xa4]=0;
        entries[0].generation=4; graph=reader.capture(WallMemory{},b,profile,level,lookup,aim);
        expect(!graph.selected&&graph.nodes[0].ref.generation==4,"old crosshair selection cannot attach to a reused generation"); entries[0].generation=3;
        put(entities[0].data(),0x108,int32_t(-1)); graph=reader.capture(WallMemory{},b,profile,level,lookup);
        expect(graph.edges.size()==1&&graph.unavailable>0,"invalid list counts are unavailable, not fabricated edges"); put(entities[0].data(),0x108,int32_t(1));
        const std::string rawList="idList < idEntity * , TAG_IDLIST , false >";
        schemas[rawList]={24,"",{{"list","idEntity","**",0,8},{"num","int","",8,4}},true};
        schemas["idGraphLogic"]={32,"",{{"gate","idEntity","*",0,8}},true};
        schemas["idEncounterManager"].fields.push_back({"targets",rawList,"",0x140,24});
        schemas["idEncounterManager"].fields.push_back({"logic","idGraphLogic","",0x180,32});
        std::array<uintptr_t,300> targets{}; targets[0]=entries[0].entity; targets[299]=entries[2].entity;
        put(entities[1].data(),0x140,reinterpret_cast<uintptr_t>(targets.data())); put(entities[1].data(),0x148,int32_t(targets.size()));
        put(entities[1].data(),0x180,entries[2].entity);
        graph=reader.capture(WallMemory{},b,profile,level,lookup);
        expect(graph.edges.size()==4&&graph.limited>0,"inline structs and raw pointer lists are traversed with explicit list limits");
        expect(std::any_of(graph.edges.begin(),graph.edges.end(),[](const GraphEdge& edge) { return edge.field=="targets[0]"; })&&
            std::any_of(graph.edges.begin(),graph.edges.end(),[](const GraphEdge& edge) { return edge.field=="logic.gate"; }),"nested and list edges retain exact field paths");
        schemas["idEncounterManager"].fields.resize(1);
        schemas["idEncounterManager"].fields.push_back({"self","idEntity","*",0x1c0,8}); put(entities[1].data(),0x1c0,entries[1].entity);
        graph=reader.capture(WallMemory{},b,profile,level,lookup);
        expect(graph.edges.size()==2,"internal self references are omitted from the entity relationship graph");
        schemas["idEncounterManager"].fields.resize(1);
        bool changed=false;
        auto changingLookup=[&](const std::string& type)->const GraphType& {
            if(type=="idEncounterManager"&&!changed) { changed=true; entities[1][0xa4]=1; }
            return lookup(type);
        };
        graph=reader.capture(WallMemory{},b,profile,level,changingLookup);
        expect(graph.nodes.size()==2&&graph.edges.empty(),"endpoints removed during the scan are discarded with their edges"); entities[1][0xa4]=0;
        auto mapChanged=level; ++mapChanged.world;
        expect(reader.capture(WallMemory{},b,profile,mapChanged,lookup).phase=="loading","world identity changes discard snapshots");
        expect(reader.capture(WallMemory{},b,profile,level,[](const std::string&)->const GraphType& { static const GraphType empty; return empty; }).phase=="metadata_pending","missing metadata is explicit");
        if(profiles==1) {
            auto graphFolder=std::filesystem::temp_directory_path()/(L"DEViL-Graph-Test-"+std::to_wstring(GetCurrentProcessId()));
            graph=reader.capture(WallMemory{},b,profile,level,lookup,aim);
            expect(writeEntityGraph(graph,graphFolder),"atomic graph publication succeeds");
            std::ifstream input(graphFolder/(L"entity-graph-"+std::to_wstring(GetCurrentProcessId())+L".tsv")); std::string text((std::istreambuf_iterator<char>(input)),{}); input.close();
            expect(text.find("#DEVIL_GRAPH_V1\t")==0&&text.find("E\t100:3\t101:3\towners[0]")!=std::string::npos,"published edges use map-scoped generation identities");
            auto request=graphFolder/(L"entity-graph-"+std::to_wstring(GetCurrentProcessId())+L".request");
            { std::ofstream file(request); file<<10000; }
            expect(graphRequestFresh(graphFolder,11000)&&!graphRequestFresh(graphFolder,15000)&&!graphRequestFresh(graphFolder,1000),"requests expire and future timestamps are rejected");
            std::filesystem::remove_all(graphFolder);
        }
        VirtualFree(base,0,MEM_RELEASE);
    }
    // Parse the same reflected ABI used in production, including a malformed row.
    std::array<unsigned char,72> info{}; std::array<unsigned char,144> rows{};
    const char* type="idFixture",*field="target",*fieldType="idEntity",*ops="*";
    put(info.data(),0,reinterpret_cast<uintptr_t>(type)); put(info.data(),16,uint32_t(128)); put(info.data(),32,reinterpret_cast<uintptr_t>(rows.data()));
    put(rows.data(),0,reinterpret_cast<uintptr_t>(fieldType)); put(rows.data(),8,reinterpret_cast<uintptr_t>(ops)); put(rows.data(),16,reinterpret_cast<uintptr_t>(field));
    put(rows.data(),24,int32_t(64)); put(rows.data(),28,int32_t(8));
    auto schema=readGraphType(WallMemory{},reinterpret_cast<uintptr_t>(info.data()),type);
    expect(schema.complete&&schema.fields.size()==1&&schema.fields[0].ops=="*","reflected type/ops remain separate");
    const char* empty="";
    put(info.data(),8,reinterpret_cast<uintptr_t>(empty));
    schema=readGraphType(WallMemory{},reinterpret_cast<uintptr_t>(info.data()),type);
    expect(schema.complete&&schema.parent.empty()&&schema.fields.size()==1,"retail nonnull empty superType is valid for root/value types");
    put(rows.data(),72,reinterpret_cast<uintptr_t>(fieldType)); // name remains null
    schema=readGraphType(WallMemory{},reinterpret_cast<uintptr_t>(info.data()),type);
    expect(schema.complete&&schema.fields.size()==1,"field-name sentinel ends metadata even when its type pointer is populated");
    put(info.data(),8,uintptr_t(1));
    expect(!readGraphType(WallMemory{},reinterpret_cast<uintptr_t>(info.data()),type).complete,"unreadable parent pointer is still rejected");
    put(info.data(),8,reinterpret_cast<uintptr_t>(empty));
    put(rows.data(),24,int32_t(124)); schema=readGraphType(WallMemory{},reinterpret_cast<uintptr_t>(info.data()),type);
    expect(schema.complete&&schema.fields.empty(),"out-of-bounds reflected fields are skipped");
    expect(!readGraphType(WallMemory{},reinterpret_cast<uintptr_t>(info.data()),"wrongClass").complete,"foreign class metadata is rejected");
    printf("PASS: entity graph references, exact fields, retail root metadata, stable managed pointers, removed entities, generation reuse, map changes, publication and request expiry on %u profiles.\n",profiles);
}
