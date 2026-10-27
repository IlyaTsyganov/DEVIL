#include "../src/vulkan_layer.cpp"
#include <tlhelp32.h>

// Read-only integration probe: runs the production readers/decoder against an
// existing game without injecting a DLL, writing memory or changing game state.
int wmain(int argc,wchar_t** argv) {
    bool cvarsOnly=argc==4&&wcscmp(argv[3],L"--cvars-only")==0;
    bool graphOnly=argc==5&&wcscmp(argv[2],L"--graph-only")==0;
    bool triggerPathsOnly=argc==5&&wcscmp(argv[2],L"--trigger-paths")==0;
    bool playerOnly=argc==5&&wcscmp(argv[2],L"--player")==0;
    bool stateOnly=argc==5&&wcscmp(argv[2],L"--level-state")==0;
    bool pointOnly=argc==5&&wcscmp(argv[2],L"--point")==0;
    bool practiceOnly=argc==3&&wcscmp(argv[2],L"--live-practice")==0;
    bool practiceFiles=argc==4&&wcscmp(argv[2],L"--practice-files")==0;
    if(argc!=2&&argc!=3&&argc!=7&&!cvarsOnly&&!graphOnly&&!triggerPathsOnly&&!playerOnly&&!stateOnly&&!pointOnly&&!practiceFiles) { fprintf(stderr,"Usage: RetailProfileProbe <game-pid> --graph-only/--trigger-paths/--player/--level-state/--point <type-address-file> <output-directory>\n"); return 2; }
    DWORD pid=static_cast<DWORD>(_wtoi(argv[1]));
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if(modules==INVALID_HANDLE_VALUE||!Module32FirstW(modules,&entry)) { fprintf(stderr,"Cannot locate game image.\n"); if(modules!=INVALID_HANDLE_VALUE) CloseHandle(modules); return 3; }
    uintptr_t base=reinterpret_cast<uintptr_t>(entry.modBaseAddr); CloseHandle(modules);
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
    if(!process) { fprintf(stderr,"Cannot open game for read-only inspection.\n"); return 4; }
    int result=0;
    try {
        WallMemory memory{process}; const GameProfile* profile=detectGameProfile(memory,base);
        if(!profile) throw std::runtime_error("No supported executable profile");
        GameCamera camera{}; bool cameraValid=readGameCamera(memory,base,*profile,camera);
        LevelIdentity level=readLevelIdentity(memory,base,*profile);
        printf("Profile: %s\nCamera valid: %d\nGameplay visible: %d\nConsole fraction: %.3f\nLevel: %s\n",profile->name,cameraValid,gameplayVisible(memory,base,*profile),readConsoleFraction(memory,base,*profile),level.name.c_str());
        Vec3 playerPosition{}; bool playerValid=readResearchPosition(memory,base,*profile,playerPosition);
        printf("Player body origin valid=%d position=(%.3f, %.3f, %.3f)\n",playerValid,playerPosition.x,playerPosition.y,playerPosition.z);
        if(practiceOnly||practiceFiles) {
            auto state=readPracticeRuntime(memory,base,profile);
            printf("Live practice read-only guard: valid=%d user=%u index=%u folder=%s map=%s error=%s\n",bool(state),state.user,state.index,state.folder.c_str(),state.level.c_str(),state.error.c_str());
            bool signatures=practiceLoaderMatches(memory,base); printf("Audited loader signatures: %d\n",signatures);
            if(!state||!signatures) throw std::runtime_error("Live-practice read-only validation failed");
            if(practiceFiles) {
                auto files=readPracticeFiles(std::filesystem::path(argv[3])/L"DLC1-AUTOSAVE5");
                printf("Read-only production file reader: %zu / %zu / %zu / %zu bytes. No disk copy or replacement performed.\n",files[0].size(),files[1].size(),files[2].size(),files[3].size());
            }
            puts(practiceFiles?"PASS: real paused TAG1 slot 3 and its four files read without engine/Steam calls or save changes.":"PASS: real paused TAG1 slot 3 passes production reader and loader fingerprints. No engine/Steam calls and no save-file access.");
            CloseHandle(process); return 0;
        }
        if(graphOnly||triggerPathsOnly||playerOnly||stateOnly||pointOnly) {
            std::ifstream input{std::filesystem::path(argv[3])}; std::string line;
            if(!std::getline(input,line)||line!="#DEVIL_GRAPH_LAYOUTS_V1\t"+std::to_string(pid)) throw std::runtime_error("Graph metadata address file belongs to a different game process");
            std::unordered_map<std::string,uintptr_t> addresses;
            while(std::getline(input,line)) {
                auto split=line.find('\t'); if(split==std::string::npos||split>255||addresses.size()>100000) throw std::runtime_error("Invalid metadata address row");
                addresses.emplace(line.substr(0,split),std::stoull(line.substr(split+1),nullptr,0));
            }
            std::unordered_map<std::string,GraphType> layouts; std::unordered_set<std::string> missing,failed;
            auto lookup=[&](const std::string& type)->const GraphType& {
                auto cached=layouts.find(type); if(cached!=layouts.end()) return cached->second;
                auto found=addresses.find(type);
                if(found==addresses.end()) { missing.insert(type); static const GraphType empty; return empty; }
                auto schema=readGraphType(memory,found->second,type);
                if(!schema.complete) failed.insert(type);
                return layouts.emplace(type,std::move(schema)).first->second;
            };
            if(pointOnly) {
                auto fields=[&](const std::string& type,const char* wanted) {
                    auto found=addresses.find(type); return found==addresses.end()?InspectorField{}:readInspectorField(memory,found->second,wanted);
                };
                auto value=captureBookmark(memory,base,*profile,std::string(32,'f'),resolveCheckpointLayout(fields)); value.process=pid;
                if(value.phase!="ready"||!writeBookmark(value,std::filesystem::path(argv[4])/(L"research-point-"+std::to_wstring(pid)+L".tsv"))) throw std::runtime_error("Could not capture/publish research point");
                printf("Point: map=%s body=(%.3f %.3f %.3f) eye=(%.3f %.3f %.3f) forward=(%.4f %.4f %.4f) loadedCheckpoint=%s:%s\n",value.level.name.c_str(),value.body.x,value.body.y,value.body.z,value.eye.x,value.eye.y,value.eye.z,value.forward.x,value.forward.y,value.forward.z,value.checkpoint.status.c_str(),value.checkpoint.name.c_str());
                puts("PASS: point captured read-only; no teleport, commands, save writes or clipboard modification."); CloseHandle(process); return 0;
            }
            if(stateOnly) {
                auto fields=[&](const std::string& type,const char* wanted) {
                    auto found=addresses.find(type); return found==addresses.end()?InspectorField{}:readInspectorField(memory,found->second,wanted);
                };
                auto started=GetTickCount64(); LevelStateReader reader;
                auto capture=reader.capture(memory,base,*profile,std::string(32,'a'),resolveEncounterFields(fields),resolveCheckpointLayout(fields),resolveTriggerPathLayout(fields)); capture.process=pid;
                if(!writeLevelState(capture,std::filesystem::path(argv[4])/(L"level-state-"+std::to_wstring(pid)+L"-before.tsv"))) throw std::runtime_error("Could not publish level-state protocol");
                printf("Level state: phase=%s members=%zu volumes=%zu managers=%zu managerSource=%s checkpoint=%s:%s unavailable=%u elapsedMs=%llu\n",capture.phase.c_str(),capture.members.size(),capture.volumes.size(),capture.encounters.rows.size(),capture.encounters.phase.c_str(),capture.checkpoint.status.c_str(),capture.checkpoint.name.c_str(),capture.unavailable,GetTickCount64()-started);
                if(capture.phase!="ready"||!capture.membershipComplete||capture.volumes.empty()||capture.encounters.rows.empty()||capture.checkpoint.status!="known") throw std::runtime_error("Level-state capture is incomplete");
                puts("PASS: read-only level state captured with production readers; no engine commands or memory writes.");
                CloseHandle(process); return 0;
            }
            if(playerOnly) {
                auto fields=[&](const std::string& type,const char* wanted) {
                    auto found=addresses.find(type); return found==addresses.end()?InspectorField{}:readInspectorField(memory,found->second,wanted);
                };
                auto layout=resolveMotionLayout(fields);
                printf("Motion layout: valid=%d physics=0x%x bytes=0x%x origin=0x%x shape=0x%x index=0x%x\n",bool(layout),layout.physics,layout.bytes,layout.origin,layout.shape,layout.shapeIndex);
                PlayerMotionReader reader; auto player=reader.capture(memory,base,*profile,layout);
                printf("Motion: valid=%d collider=%d shapeIndex=%d radius=%.6f size=(%.3f %.3f %.3f) velocity=(%.5f %.5f %.5f) contact=%d gravity=%d normal=(%.3f %.3f %.3f) coreTriangles=%zu wireSegments=%zu\n",player.valid,player.collider,player.shapeIndex,player.radius,
                    player.local[3]-player.local[0],player.local[4]-player.local[1],player.local[5]-player.local[2],player.velocity.x,player.velocity.y,player.velocity.z,
                    player.contact,player.gravity,player.normal.x,player.normal.y,player.normal.z,player.core?player.core->triangles.size()/3:0,player.wire?player.wire->lines.size()/2:0);
                if(!player.valid||!player.collider||!player.gravity) throw std::runtime_error("Player collider validation failed");
                PlayerMotion frame;
                if(!refreshPlayerMotionPose(memory,base,*profile,layout,player,frame)||!frame.valid) throw std::runtime_error("Presentation pose reader could not validate current player");
                printf("Frame pose: position=(%.3f %.3f %.3f) cachedWireReused=%d; no shape decode in frame reader.\n",frame.position.x,frame.position.y,frame.position.z,frame.wire==player.wire);
                std::deque<PlayerMotion> samples;
                for(unsigned i=0;i<20;++i) {
                    auto point=reader.capture(memory,base,*profile,layout); point.epoch=encounterEpochMillis(); point.sequence=i+1; point.segment=1;
                    if(point.valid) samples.push_back(point);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if(samples.empty()||readLevelIdentity(memory,base,*profile)!=level||!writeMotionSamples(std::filesystem::path(argv[4])/(L"motion-"+std::to_wstring(pid)+L".tsv"),pid,encounterEpochMillis(),"live",level,profile->name,samples))
                    throw std::runtime_error("Could not publish read-only player samples");
                printf("Motion protocol: %zu copied physical samples saved for companion validation.\n",samples.size());
                puts("PASS: real player convex core + radius matches reflected body bounds; physical velocity and support data read without game methods.");
                CloseHandle(process); return 0;
            }
            if(triggerPathsOnly) {
                auto fields=[&](const std::string& type,const char* wanted) {
                    const auto& schema=lookup(type);
                    for(const auto& field:schema.fields) if(field.name==wanted) return InspectorField{field.offset,field.size,field.type+field.ops};
                    return InspectorField{};
                };
                auto paths=resolveTriggerPathLayout(fields);
                if(paths.namePointer==~0u||paths.definition==~0u) throw std::runtime_error("Trigger instance/definition-name metadata unavailable");
                LiveWalls scanner; scanner.memory=memory;
                scanner.refresh(base,*profile,true,true,true,GetTickCount64(),paths);
                auto directory=std::filesystem::path(argv[4]);
                TriggerPathDiscovery discovery(directory/L"trigger-paths.tsv"); discovery.publish(scanner.observedTriggerPaths);
                unsigned named=0,props=0,interact=0,definitions=0,hidden=0,resourceOnly=0;
                hiddenTriggerPaths={"idTrigger|trigger/props/"};
                for(const auto& mesh:scanner.meshes) if(mesh.kind==0) {
                    if(mesh.triggerNameKnown) ++named;
                    if(!mesh.triggerDefinitionPath.empty()) { ++definitions; if(!mesh.triggerNameKnown) ++resourceOnly; }
                    if(mesh.entity.className=="idTrigger"&&(mesh.triggerPath.rfind("trigger/props/",0)==0||mesh.triggerDefinitionPath.rfind("trigger/props/",0)==0)) { ++props; if(!triggerClassShown(mesh)) ++hidden; }
                    if(mesh.entity.className=="idTrigger"&&mesh.triggerPath.rfind("trigger/interact/",0)==0) { ++interact; if(!triggerClassShown(mesh)) throw std::runtime_error("Props filter also hid interact triggers"); }
                }
                printf("Trigger paths: namePointer=0x%x entityDef=0x%x definitionName=0x%x groups=%zu namedMeshes=%u definitions=%u resourceOnly=%u props=%u interact=%u hiddenProps=%u\n",paths.namePointer,paths.definition,paths.definitionNamePointer,scanner.observedTriggerPaths.size(),named,definitions,resourceOnly,props,interact,hidden);
                if(!named||!definitions||!resourceOnly||!props||props!=hidden||!interact) throw std::runtime_error("Trigger path reader/filter integration failed");
                for(const auto& mesh:scanner.meshes) if(mesh.kind==0&&!mesh.triggerNameKnown&&!mesh.triggerDefinitionPath.empty()) {
                    hiddenTriggerPaths={mesh.entity.className+'|'+mesh.triggerDefinitionPath};
                    if(triggerClassShown(mesh)) throw std::runtime_error("Resource-only category did not filter a flat instance name");
                }
                puts("PASS: read-only instance/definition path discovery; props hidden, interact retained, flat instance names filtered by resource category. Game untouched.");
                CloseHandle(process); return 0;
            }
            for(const char* type:{"idEntity","idStr","idManagedClass"}) {
                const auto& schema=lookup(type);
                printf("Graph metadata %s: complete=%d size=%u parent='%s' fields=%zu\n",type,schema.complete,schema.size,schema.parent.c_str(),schema.fields.size());
            }
            auto started=GetTickCount64(); EntityGraphReader reader;
            auto snapshot=reader.capture(memory,base,*profile,level,lookup); snapshot.process=pid; snapshot.pending=static_cast<uint32_t>(missing.size());
            printf("Graph: phase=%s nodes=%zu edges=%zu unavailable=%u missingTypes=%zu failedMetadata=%zu limits=%u elapsedMs=%llu\n",snapshot.phase.c_str(),snapshot.nodes.size(),snapshot.edges.size(),snapshot.unavailable,missing.size(),failed.size(),snapshot.limited,GetTickCount64()-started);
            unsigned shown=0;
            for(const auto& edge:snapshot.edges) if(snapshot.nodes[edge.from].ref.className.rfind("idEncounterTrigger",0)==0) {
                printf("Graph link: %s -> %s | %s | %s\n",snapshot.nodes[edge.from].name.c_str(),snapshot.nodes[edge.to].name.c_str(),edge.field.c_str(),edge.evidence.c_str());
                if(++shown>=8) break;
            }
            if(!writeEntityGraph(snapshot,std::filesystem::path(argv[4]))) throw std::runtime_error("Could not save graph validation snapshot");
            std::ofstream diagnostics(std::filesystem::path(argv[4])/L"metadata-unavailable.txt");
            for(const auto& type:missing) diagnostics<<"missing\t"<<type<<'\n';
            for(const auto& type:failed) diagnostics<<"rejected\t"<<type<<'\n';
            if(snapshot.phase!="live"||snapshot.nodes.empty()||snapshot.edges.empty()) throw std::runtime_error("Graph reader did not capture entities and references");
            CloseHandle(process); return 0;
        }
        if(argc>=3) {
            auto commandSystem=resolveLabCommandSystem(memory,_wcstoui64(argv[2],nullptr,0),base,*profile);
            printf("lab_ command ABI validated (read-only, no registration): %d\n",bool(commandSystem));
            if(!commandSystem) throw std::runtime_error("Command-system ABI validation failed");
            auto cvarSystem=resolveLabCvarSystem(memory,commandSystem,base,*profile);
            auto cvarFactory=resolveLabCvarFactory(memory,cvarSystem,base,*profile);
            printf("lab_ native cvar factory validated (read-only, no creation): %d\n",bool(cvarFactory));
            if(cvarsOnly) {
                if(!cvarFactory) throw std::runtime_error("Native cvar factory ABI validation failed");
                CloseHandle(process); return 0;
            }
        }
        if(argc==7) {
            const char* classes[]{"idBloatedEntity","idEntity","idStr","idEncounterManager"};
            auto fields=resolveEncounterFields([&](const std::string& type,const char* field) {
                for(unsigned i=0;i<4;++i) if(type==classes[i]) return readInspectorField(memory,_wcstoui64(argv[3+i],nullptr,0),field);
                return InspectorField{};
            });
            ActiveEncounterReader reader; auto snapshot=reader.capture(memory,base,*profile,level,fields);
            unsigned active=0,mapped=0;
            for(const auto& row:snapshot.rows) if(!row.label.empty()) {
                if(++mapped<=2) printf("Encounter reference match: %s | %s\n",row.label.c_str(),row.name.c_str());
            }
            for(const auto& row:snapshot.rows) if(row.active()) { ++active; printf("Active encounter: %s | %s | script=%d commands=%d\n",row.label.c_str(),row.name.c_str(),row.script,row.commands); }
            printf("Active encounter snapshot: phase=%s managers=%zu active=%u referenceMatches=%u unreadable=%u\n",snapshot.phase.c_str(),snapshot.rows.size(),active,mapped,snapshot.unreadable);
            if(snapshot.phase!="live") throw std::runtime_error("Active encounter reader could not produce a live snapshot");
        }
        if(!cameraValid||!level) throw std::runtime_error("Enter a loaded game level before decoding");
        LiveWalls scanner; scanner.memory=memory; scanner.refresh(base,*profile,true,true,true);
        printf("Entities: examined=%u activeTriggers=%u activeWalls=%u disabled=%u removed=%u unavailable=%u missingCurrentGeometry=%u unsupportedActive=%u meshes=%zu scanMs=%.2f\n",scanner.entitiesExamined,scanner.triggers,scanner.active,scanner.disabled,scanner.removed,scanner.unavailable,scanner.missingGeometry,scanner.unsupported,scanner.meshes.size(),scanner.milliseconds);
        unsigned samples=0;
        for(const auto& mesh:scanner.meshes) if(mesh.volumeState==EntityStatus::Disabled&&samples++<8)
            printf("Disabled sample: id=%u class=%s historical=%d\n",mesh.entity.id,mesh.entity.className.c_str(),mesh.historical);
        RuntimeSceneBuilder builder; builder.memory=memory; auto world=builder.build(level);
        if(!world||readLevelIdentity(memory,base,*profile)!=level) throw std::runtime_error("World changed during snapshot; retry in a loaded level");
        printf("World: instances=%zu unique=%u triangles=%llu nonBlocking=%u unsupported=%u buildMs=%.2f\n",world->meshes.size(),world->unique,world->triangles,world->nonBlocking,world->unsupported,world->milliseconds);
        if(world->meshes.empty()) throw std::runtime_error("No player-blocking world geometry decoded");
        runtimeRendering=true; runtimeWorld=world;
        auto entities=std::make_shared<RuntimeEntities>(); entities->level=level; entities->meshes=std::move(scanner.meshes); runtimeEntities=entities;
        std::vector<Vertex> vertices; uint32_t triangles=collectVertices(camera,vertices);
        printf("Current view: triangleVertices=%u lineVertices=%zu radius=%.0f\n",triangles,vertices.size()-triangles,radius);
        for(const auto& error:builder.errors) printf("Decode diagnostic: %s = %u\n",error.first.c_str(),error.second);
        runtimeWorld.reset(); runtimeEntities.reset();
    } catch(const std::exception& error) { fprintf(stderr,"FAIL: %s\n",error.what()); result=1; }
    CloseHandle(process); return result;
}
