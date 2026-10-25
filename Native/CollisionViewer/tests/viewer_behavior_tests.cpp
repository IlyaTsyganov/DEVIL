#include "../src/vulkan_layer.cpp"
static void expect(bool value,const char* message) { if(!value) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
template<class T> static void put(void* memory,size_t offset,T value) { memcpy(static_cast<unsigned char*>(memory)+offset,&value,sizeof(value)); }
static void writeSettings(const std::wstring& path,const wchar_t* body) {
    std::wstring content=L"\xfeff[Viewer]\r\n"+std::wstring(body);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    expect(file!=INVALID_HANDLE_VALUE,"settings fixture file");
    DWORD written=0;
    BOOL ok=WriteFile(file,content.data(),static_cast<DWORD>(content.size()*sizeof(wchar_t)),&written,nullptr);
    CloseHandle(file);
    expect(ok&&written==content.size()*sizeof(wchar_t),"write settings fixture");
}
int main() {
    {
        Device device{}; device.families.resize(2); device.families[0].queueFlags=VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT;
        device.families[1].queueFlags=VK_QUEUE_COMPUTE_BIT;
        VkQueue graphics=reinterpret_cast<VkQueue>(uintptr_t(1)),present=reinterpret_cast<VkQueue>(uintptr_t(2));
        queues[graphics]={&device,0}; queues[present]={&device,1};
        Swapchain swap{}; swap.device=&device; uint32_t family=~0u;
        expect(overlayQueue(swap,graphics,0,family)==graphics&&family==0,"same-family presentation keeps original graphics path");
        expect(overlayQueue(swap,present,1,family)==graphics&&family==0,"compute-only presentation uses graphics queue");
        swap.sharing=VK_SHARING_MODE_CONCURRENT; swap.sharingFamilies={1};
        expect(!overlayQueue(swap,present,1,family),"unshared graphics family is not used");
        swap.sharingFamilies.push_back(0);
        expect(overlayQueue(swap,present,1,family)==graphics&&family==0,"shared graphics queue selected for concurrent images");
        queues.clear();
    }
    expect(overlayDepthTestEnabled(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)&&
           overlayDepthWriteEnabled(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)&&
           !overlayDepthTestEnabled(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)&&
           !overlayDepthWriteEnabled(VK_PRIMITIVE_TOPOLOGY_LINE_LIST),
           "wire contours are not hidden by the viewer's translucent fill depth");
    wchar_t temp[MAX_PATH]{}; DWORD tempLength=GetTempPathW(MAX_PATH,temp);
    expect(tempLength&&tempLength<MAX_PATH,"temporary directory");
    std::wstring temporaryRoot=std::wstring(temp)+L"DEViLSettingsTest_"+std::to_wstring(GetCurrentProcessId());
    std::wstring settingsDirectory=temporaryRoot+L"\\DEViL";
    CreateDirectoryW(temporaryRoot.c_str(),nullptr); CreateDirectoryW(settingsDirectory.c_str(),nullptr);
    std::wstring settingsFile=settingsDirectory+L"\\viewer.ini";
    expect(SetEnvironmentVariableW(L"LOCALAPPDATA",temporaryRoot.c_str()),"isolated settings path");
    writeSettings(settingsFile,L"Enabled=0\r\nShowTriggers=0\r\nShowCollision=1\r\nShowWalls=0\r\nCollisionMode=0\r\nDistance=100\r\n");
    updateViewerSettings();
    expect(!enabled&&!showTriggers&&showCollision&&!showWalls&&collisionMode==0&&radius==100,"settings file controls viewer without hotkeys");
    Sleep(260);
    writeSettings(settingsFile,L"Enabled=1\r\nShowTriggers=1\r\nShowCollision=0\r\nShowWalls=1\r\nCollisionMode=1\r\nDistance=175\r\n");
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,settingsFile.c_str());
    updateViewerSettings();
    expect(enabled&&showTriggers&&!showCollision&&showWalls&&collisionMode==1&&radius==175,"live settings update");
    enabled=true; showTriggers=true; showCollision=true; showWalls=true; collisionMode=1; radius=40;
    Mesh priorityMeshes[3]{}; priorityMeshes[0].kind=0; priorityMeshes[1].kind=1; priorityMeshes[2].kind=2;
    std::vector<Candidate> priorityCandidates{{&priorityMeshes[0],1},{&priorityMeshes[1],9},{&priorityMeshes[1],2},{&priorityMeshes[2],0}};
    sortVisibleCandidates(priorityCandidates);
    expect(priorityCandidates[0].mesh->kind==1&&priorityCandidates[0].distance==2&&priorityCandidates[1].mesh->kind==1&&
           priorityCandidates[2].mesh->kind==0&&priorityCandidates[3].mesh->kind==2,"collision geometry gets budget priority, nearest first");
    std::vector<Candidate> centerCandidates{{&priorityMeshes[1],9.0f,.0f},{&priorityMeshes[1],1.0f,.25f}};
    sortVisibleCandidates(centerCandidates);
    expect(centerCandidates[0].screenDistance==0.0f&&centerCandidates[0].distance==9.0f,
           "screen-center geometry gets priority over nearer off-center geometry");
    // Synthetic scene exercises the real renderer preparation for all three groups.
    GameCamera camera{}; camera.matrix[0]=camera.matrix[5]=camera.matrix[10]=camera.matrix[15]=1;
    for(uint32_t kind=0;kind<3;++kind) {
        auto g=std::make_shared<Geometry>(); g->kind=kind;
        g->triangles={{-.2f,-.2f,.2f},{.2f,-.2f,.2f},{0,.2f,.2f}}; g->lines={{-.2f,-.2f,.2f},{.2f,-.2f,.2f}};
        Mesh m{}; m.kind=kind; m.geometry=g;
        float box[6]{-.2f,-.2f,.2f,.2f,.2f,.2f}; memcpy(m.bounds,box,24);
        if(kind<2) meshes.push_back(m); else liveWalls.meshes.push_back(m);
    }
    spatialOrder={0,1}; buildSpatialNode(0,2);
    // Radius uses the nearest point on the object bounds, so a mesh crossing
    // the radius is selected whole even when its center lies outside it.
    auto crossingGeometry=std::make_shared<Geometry>(); crossingGeometry->kind=1;
    crossingGeometry->triangles={{.51f,0,0},{.8f,0,0},{.7f,.1f,0}};
    Mesh crossing{}; crossing.kind=1; crossing.geometry=crossingGeometry;
    float crossingBounds[6]{.5f,-.1f,-.1f,.8f,.1f,.1f}; memcpy(crossing.bounds,crossingBounds,sizeof(crossingBounds));
    auto savedMeshes=meshes; auto savedOrder=spatialOrder; auto savedNodes=spatialNodes;
    meshes={crossing}; spatialOrder={0}; spatialNodes.clear(); buildSpatialNode(0,1);
    const bool savedShowWalls=showWalls; const float savedRadius=radius; showWalls=false;
    GameCamera radiusCamera=camera; radiusCamera.matrix[0]=.5f;
    std::vector<Candidate> radiusVisible; radius=.55f; gatherVisible(radiusCamera,radiusVisible);
    expect(radiusVisible.size()==1,"whole mesh selected when bounds intersect radius");
    crossingBounds[0]=.8f; crossingBounds[3]=1.0f; memcpy(meshes[0].bounds,crossingBounds,sizeof(crossingBounds));
    spatialNodes.clear(); buildSpatialNode(0,1); gatherVisible(radiusCamera,radiusVisible);
    expect(radiusVisible.size()==1,"radius cushion prevents whole mesh popping at the configured edge");
    crossingBounds[0]=.5f; crossingBounds[3]=.8f; memcpy(meshes[0].bounds,crossingBounds,sizeof(crossingBounds));
    spatialNodes.clear(); buildSpatialNode(0,1);
    collisionMode=1; std::vector<Vertex> radiusFill; uint32_t radiusFillVertices=collectVertices(radiusCamera,radiusFill);
    expect(radiusFillVertices==3&&radiusFill.size()==3,"full mesh fill emitted when its bounds intersect radius");
    crossingBounds[0]=1.1f; crossingBounds[3]=1.4f; memcpy(meshes[0].bounds,crossingBounds,sizeof(crossingBounds));
    expect(ViewFilter(camera).distanceSquared(meshes[0].bounds)>radius*radius,"outside mesh bounds distance exceeds radius");
    spatialNodes.clear(); buildSpatialNode(0,1); gatherVisible(radiusCamera,radiusVisible);
    expect(radiusVisible.empty(),"mesh rejected when bounds are outside radius");
    expect(collectVertices(radiusCamera,radiusFill)==0&&radiusFill.empty(),"fill rejected with mesh outside radius and cushion");
    meshes=std::move(savedMeshes); spatialOrder=std::move(savedOrder); spatialNodes=std::move(savedNodes); showWalls=savedShowWalls; radius=savedRadius;
    {
        auto previousMeshes=std::move(meshes); auto previousOrder=std::move(spatialOrder); auto previousNodes=std::move(spatialNodes);
        auto previousLiveWalls=std::move(liveWalls.meshes); const bool previousCollision=showCollision; const bool previousWalls=showWalls;
        const float previousRadius=radius; const uint32_t previousMode=collisionMode;
        auto bulkGeometry=std::make_shared<Geometry>(); bulkGeometry->kind=1;
        bulkGeometry->triangles={{-.2f,-.2f,.2f},{.2f,-.2f,.2f},{0,.2f,.2f}};
        bulkGeometry->lines={{-.2f,-.2f,.2f},{.2f,-.2f,.2f}};
        const float bulkBounds[6]{-.2f,-.2f,.2f,.2f,.2f,.2f};
        constexpr uint32_t bulkCount=60000;
        meshes.resize(bulkCount); spatialOrder.resize(bulkCount); std::iota(spatialOrder.begin(),spatialOrder.end(),0u);
        for(auto& mesh:meshes) { mesh.kind=1; mesh.geometry=bulkGeometry; memcpy(mesh.bounds,bulkBounds,sizeof(bulkBounds)); }
        spatialNodes.clear(); buildSpatialNode(0,bulkCount); liveWalls.meshes.clear(); showCollision=true; showWalls=false; radius=40; collisionMode=1;
        std::vector<Vertex> bulkOutput;
        uint32_t bulkFillVertices=collectVertices(camera,bulkOutput);
        expect(bulkFillVertices==bulkCount*3&&bulkOutput.size()==bulkCount*5,
               "visible geometry is not dropped at the former per-frame vertex budget");
        meshes=std::move(previousMeshes); spatialOrder=std::move(previousOrder); spatialNodes=std::move(previousNodes);
        liveWalls.meshes=std::move(previousLiveWalls); showCollision=previousCollision; showWalls=previousWalls; radius=previousRadius; collisionMode=previousMode;
    }
    std::vector<Vertex> vertices;
    collisionMode=0; expect(collectVertices(camera,vertices)==0&&vertices.size()==6,"contours for all groups");
    collisionMode=1; expect(collectVertices(camera,vertices)==9&&vertices.size()==15,"fill plus contours for all groups");
    for(unsigned i=0;i<9;++i) expect(vertices[i].color[3]==.09f,"equal fill opacity");
    expect(vertices[13].color[0]==1&&vertices[13].color[1]==.45f,"orange walls");
    for(bool* toggle:{&showTriggers,&showCollision,&showWalls}) {
        *toggle=false; expect(collectVertices(camera,vertices)==6&&vertices.size()==10,"independent group toggle"); *toggle=true;
    }
    // Exercise the same readers and filters with all audited retail layouts.
    for(const GameProfile* selected:{&kProfile11,&kProfile10,&kProfile31,&kProfile51}) {
    const GameProfile& profile=*selected;
    auto* base=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x7000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    expect(base!=nullptr,"fixture allocation");
    std::vector<unsigned char> system(0x60),map(0x61d00),player(0x4c8a0),menu(0xc8),entity(0x240),physics(0x220),poly(0x200);
    uintptr_t b=reinterpret_cast<uintptr_t>(base),menuAddress=reinterpret_cast<uintptr_t>(menu.data());
    IMAGE_DOS_HEADER dos{}; dos.e_magic=IMAGE_DOS_SIGNATURE; dos.e_lfanew=0x80;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature=IMAGE_NT_SIGNATURE; nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.TimeDateStamp=profile.timestamp; nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC; nt.OptionalHeader.SizeOfImage=profile.imageSize;
    memcpy(base,&dos,sizeof(dos)); memcpy(base+dos.e_lfanew,&nt,sizeof(nt));
    memcpy(base+profile.fingerprintRva,kProfileCode,sizeof(kProfileCode));
    expect(detectGameProfile(WallMemory{},b)==selected,"profile selected from PE metadata and executable code");
    base[profile.fingerprintRva]^=1; expect(!detectGameProfile(WallMemory{},b),"mismatched code rejected"); base[profile.fingerprintRva]^=1;
    ++nt.FileHeader.TimeDateStamp; memcpy(base+dos.e_lfanew,&nt,sizeof(nt));
    expect(!detectGameProfile(WallMemory{},b),"unknown executable rejected"); --nt.FileHeader.TimeDateStamp; memcpy(base+dos.e_lfanew,&nt,sizeof(nt));
    std::vector<unsigned char> renderer(0x5a71b0),view(0x21970);
    uintptr_t viewPointer=reinterpret_cast<uintptr_t>(view.data());
    put(base,profile.rendererRva,reinterpret_cast<uintptr_t>(renderer.data()));
    put(renderer.data(),profile.viewsOffset+8,int(1)); put(renderer.data(),profile.viewsOffset,reinterpret_cast<uintptr_t>(&viewPointer));
    GameCamera expectedCamera{}; expectedCamera.matrix[0]=expectedCamera.matrix[5]=expectedCamera.matrix[10]=expectedCamera.matrix[14]=1;
    expectedCamera.matrix[11]=-.05f; memcpy(view.data()+profile.viewMatrixOffset,expectedCamera.matrix,64);
    GameCamera observedCamera{};
    expect(readGameCamera(WallMemory{},b,profile,observedCamera),"camera uses selected renderer global");
    expect(!memcmp(observedCamera.matrix,expectedCamera.matrix,64),"camera matrix copied without changing projection");
    // The view position moved independently of the matrix between releases.
    // Use a translated camera so reading an unrelated zero field cannot pass.
    const float translatedPosition[]{10,20,30};
    memcpy(view.data()+profile.viewPositionOffset,translatedPosition,sizeof(translatedPosition));
    expectedCamera.matrix[15]=-30;
    memcpy(view.data()+profile.viewMatrixOffset,expectedCamera.matrix,64);
    expect(readGameCamera(WallMemory{},b,profile,observedCamera)&&!memcmp(observedCamera.position,translatedPosition,sizeof(translatedPosition)),"translated camera reads the selected position member");
    memset(view.data()+profile.viewPositionOffset,0,sizeof(translatedPosition));
    expectedCamera.matrix[15]=0;
    memcpy(view.data()+profile.viewMatrixOffset,expectedCamera.matrix,64);
    expect(readGameCamera(WallMemory{},b,profile,observedCamera),"origin camera restored");
    const float onScreen[]{-.1f,-.1f,1,.1f,.1f,2},offScreen[]{4,-.1f,1,5,.1f,2},behindCamera[]{-.1f,-.1f,-2,.1f,.1f,-1};
    expect(ViewFilter(observedCamera).inView(onScreen)&&!ViewFilter(observedCamera).inView(offScreen)&&!ViewFilter(observedCamera).inView(behindCamera),"off-screen and rear geometry rejected for both profiles");
    put(base,profile.gameSystemRva,reinterpret_cast<uintptr_t>(system.data())); put(system.data(),0x44,uint32_t(2));
    put(system.data(),0x50,reinterpret_cast<uintptr_t>(map.data())); put(map.data(),profile.mapPlayerOffset,reinterpret_cast<uintptr_t>(player.data()));
    put(player.data(),0,b+profile.playerVtableRva); put(player.data(),profile.playerHudOffset+profile.hudMenusOffset,reinterpret_cast<uintptr_t>(&menuAddress)); put(player.data(),profile.playerHudOffset+profile.hudMenusOffset+8,uint32_t(1));
    put(menu.data(),0x90,int(-1)); put(menu.data(),0x94,int(-1));
    WallMemory memory;
    put(base,profile.consoleRva,b+profile.consoleVtableRva); put(base,profile.consoleRva+0x6080fc,.5f);
    expect(readConsoleFraction(memory,b,profile)==.5f&&consoleClipTop(.5f,100)==52,"console fraction and border");
    base[profile.consoleRva+0x608108]=1; expect(readConsoleFraction(memory,b,profile)==1,"forced full console"); base[profile.consoleRva+0x608108]=0;
    expect(consoleClipTop(0,100)==0&&consoleClipTop(1,100)==100&&consoleClipTop(-1,100)==0&&consoleClipTop(.1f,100)==12,"console clipping bounds");
    expect(gameplayVisible(memory,b,profile),"gameplay visible");
    put(player.data(),0,b+0x123456); expect(!gameplayVisible(memory,b,profile),"incorrect player type hidden"); put(player.data(),0,b+profile.playerVtableRva);
    player[profile.playerHudOffset+profile.hudPausedOffset]=1; expect(!gameplayVisible(memory,b,profile),"pause hidden"); player[profile.playerHudOffset+profile.hudPausedOffset]=0;
    put(menu.data(),0x90,int(2)); expect(!gameplayVisible(memory,b,profile),"HUD menu hidden"); put(menu.data(),0x90,int(-1));
    put(menu.data(),0x94,int(2)); expect(!gameplayVisible(memory,b,profile),"menu transition hidden"); put(menu.data(),0x94,int(-1));
    put(menu.data(),0x80,int(1)); expect(!gameplayVisible(memory,b,profile),"popup hidden"); put(menu.data(),0x80,int(0));
    put(system.data(),0x44,uint32_t(0)); expect(!gameplayVisible(memory,b,profile),"main menu/loading hidden"); put(system.data(),0x44,uint32_t(2));
    put(map.data(),profile.mapPlayerOffset,uintptr_t(0)); expect(!gameplayVisible(memory,b,profile),"no level player hidden");
    // A live convex wall: activation, removal, respawn and movement use physics state.
    put(poly.data(),0x22,uint8_t(1));
    put(poly.data(),0x20,uint16_t(2));
    put(poly.data(),0x42,uint16_t(8)); put(poly.data(),0x44,uint16_t(0x70-0x42));
    put(poly.data(),0x54,uint16_t(6)); put(poly.data(),0x56,uint16_t(0xf0-0x54));
    put(poly.data(),0x58,uint16_t(24)); put(poly.data(),0x5a,uint16_t(0x108-0x58));
    for(unsigned i=0;i<8;++i) { float point[4]{i&1?1.f:-1.f,i&2?2.f:-2.f,i&4?3.f:-3.f,0}; memcpy(poly.data()+0x70+16*i,point,16); }
    const uint8_t corners[24]{0,1,3,2,4,6,7,5,0,4,5,1,2,3,7,6,0,2,6,4,1,5,7,3};
    memcpy(poly.data()+0x108,corners,24);
    for(unsigned i=0;i<6;++i) { put(poly.data(),0xf0+i*4,uint16_t(i*4)); poly[0xf0+i*4+2]=4; }
    uintptr_t root=reinterpret_cast<uintptr_t>(poly.data());
    put(entity.data(),0,b+profile.wallVtableRva); put(entity.data(),0x1d0,reinterpret_cast<uintptr_t>(physics.data()));
    put(physics.data(),0x14,uint32_t(2)); put(physics.data(),0x44,uint32_t(8)); put(physics.data(),0x88,uint32_t(1));
    put(physics.data(),0x1c0,reinterpret_cast<uintptr_t>(&root));
    float axis[9]{1,0,0,0,1,0,0,0,1},origin[3]{10,20,30}; memcpy(physics.data()+0x8c,axis,36); memcpy(physics.data()+0xb0,origin,12);
    uintptr_t row[4]{1,reinterpret_cast<uintptr_t>(entity.data()),0,0};
    put(map.data(),profile.mapEntitiesOffset,reinterpret_cast<uintptr_t>(row)); put(map.data(),profile.mapEntitiesOffset+8,uint32_t(1));
    LiveWalls walls; walls.refresh(b,profile);
    expect(walls.meshes.size()==1&&walls.meshes[0].geometry->triangles.size()==36&&walls.meshes[0].geometry->lines.size()==24,"exact live convex topology");
    expect(walls.meshes[0].bounds[0]==9&&walls.meshes[0].bounds[5]==33,"live world transform");
    put(physics.data(),0x44,uint32_t(0x40000000)); walls.refresh(b,profile); expect(walls.meshes.empty(),"disabled collision disappears");
    put(physics.data(),0x44,uint32_t(8)); entity[0xa4]=1; walls.refresh(b,profile); expect(walls.meshes.empty(),"deleted entity disappears"); entity[0xa4]=0;
    put(map.data(),profile.mapEntitiesOffset+8,uint32_t(0)); walls.refresh(b,profile); expect(walls.meshes.empty(),"despawned slot disappears");
    put(map.data(),profile.mapEntitiesOffset+8,uint32_t(1)); put(physics.data(),0xb0,12.f); walls.refresh(b,profile); expect(walls.meshes.size()==1&&walls.meshes[0].bounds[0]==11,"respawn/movement refreshed");
    put(physics.data(),0x14,uint32_t(7)); walls.refresh(b,profile); expect(walls.meshes.empty(),"rigid wall outside world hidden");
    physics[0x210]=1; walls.refresh(b,profile); expect(walls.meshes.size()==1,"rigid wall added to world");
    const uintptr_t triggerVtable=b+0x200000,triggerGetter=b+0x210000,triggerInfo=b+0x220000;
    put(base,0x200000+16,triggerGetter);
    const unsigned char getterCode[]{0x48,0x8d,0x05,0,0,0,0,0xc3};
    memcpy(base+0x210000,getterCode,sizeof(getterCode));
    put(base,0x210000+3,static_cast<int32_t>(triggerInfo-(triggerGetter+7)));
    const char triggerName[]="idTrigger_Test"; put(base,0x220000,reinterpret_cast<uintptr_t>(triggerName));
    put(entity.data(),0,triggerVtable); put(physics.data(),0x44,uint32_t(0x4000));
    LiveWalls triggers; triggers.refresh(b,profile,true);
    expect(triggers.triggers==1&&triggers.meshes.size()==1&&triggers.meshes[0].kind==0,"active trigger decoded in both profiles");
    put(physics.data(),0x44,uint32_t(0x40004000)); triggers.refresh(b,profile,true);
    expect(triggers.meshes.empty(),"disabled trigger filtered in both profiles");
    put(entity.data(),0,b+profile.wallVtableRva); put(physics.data(),0x44,uint32_t(8));
    // Discover the map through the same live chain as production; no filename list.
    std::vector<unsigned char> havok(0x210c0);
    const std::string mapA="game/dlc2/e5m1_spear/e5m1_spear",mapB="game/dlc/e4m1_rig/e4m1_rig";
    put(map.data(),0x20,reinterpret_cast<uintptr_t>(mapA.c_str())); put(map.data(),0x28,uint32_t(mapA.size()));
    put(map.data(),profile.mapPlayerOffset,reinterpret_cast<uintptr_t>(player.data())); put(player.data(),0x1d0,reinterpret_cast<uintptr_t>(physics.data()));
    put(physics.data(),0x80,reinterpret_cast<uintptr_t>(havok.data())); put(havok.data(),0x210b0,reinterpret_cast<uintptr_t>(&root));
    LevelIdentity levelA=readLevelIdentity(memory,b,profile); expect(levelA.name==mapA&&levelA.world==root,"current map identity");
    RuntimeSceneBuilder builder; auto worldA=builder.build(levelA);
    expect(worldA&&worldA->meshes.size()==1&&worldA->triangles==12&&worldA->unsupported==0,"runtime world decoding and BVH");
    // Static Havok leaves carry gameplay contents in a validated idHavokShape table.
    // Projectile/query-only leaves are skipped; solid and playerclip leaves remain.
    std::vector<unsigned char> contentsWrapper(0x58),contentsRows(0x58);
    uintptr_t wrapperAddress=reinterpret_cast<uintptr_t>(contentsWrapper.data());
    uintptr_t rowsAddress=reinterpret_cast<uintptr_t>(contentsRows.data());
    put(contentsWrapper.data(),0x08,rowsAddress); put(contentsWrapper.data(),0x10,uint32_t(1));
    put(contentsRows.data(),0x00,wrapperAddress); put(poly.data(),0x30,rowsAddress);
    put(contentsRows.data(),0x08,uint32_t(0x2));
    auto queryOnly=builder.build(levelA);
    expect(queryOnly&&queryOnly->meshes.empty()&&queryOnly->nonBlocking==1,"query-only world shape filtered");
    put(contentsRows.data(),0x08,uint32_t(0x40000001));
    queryOnly=builder.build(levelA);
    expect(queryOnly&&queryOnly->meshes.empty(),"disabled world collision filtered");
    put(contentsRows.data(),0x08,uint32_t(0x1));
    worldA=builder.build(levelA);
    expect(worldA&&worldA->meshes.size()==1&&worldA->nonBlocking==0,"player-solid world shape retained");
    put(contentsRows.data(),0x08,uint32_t(0x8));
    worldA=builder.build(levelA);
    expect(worldA&&worldA->meshes.size()==1,"playerclip world shape retained");
    builder.cancelled=[] { return true; }; expect(!builder.build(levelA),"obsolete build cancellation");
    RuntimeSceneManager manager; manager.request(levelA,b,profile);
    std::shared_ptr<const RuntimeScene> ready; std::shared_ptr<const RuntimeEntities> live;
    ULONGLONG deadline=GetTickCount64()+3000;
    do { manager.snapshot(levelA,ready,live); if(ready) break; Sleep(1); } while(GetTickCount64()<deadline);
    expect(ready&&ready->level==levelA,"background publication for current map");
    LevelIdentity levelB=levelA; levelB.name=mapB;
    manager.snapshot(levelB,ready,live); expect(!ready&&!live,"never return another map's snapshot");
    // Queue a new level while the old scene still exists; stale jobs cannot publish.
    manager.request(levelB,b,profile); manager.snapshot(levelA,ready,live); expect(!ready&&!live,"old map removed immediately on transition");
    manager.request(levelA,b,profile); deadline=GetTickCount64()+3000;
    do { manager.snapshot(levelA,ready,live); if(ready) break; Sleep(1); } while(GetTickCount64()<deadline);
    expect(ready&&ready->level==levelA,"rapid level switch resolves to latest request"); manager.stop();
    runtimeRendering=true; runtimeWorld.reset(); runtimeEntities.reset();
    std::vector<Candidate> visible; gatherVisible(camera,visible); expect(visible.empty(),"no legacy e1m1 fallback while a level loads");
    runtimeWorld=worldA; gatherVisible(camera,visible); expect(visible.size()==1,"runtime scene replaces legacy map");
    runtimeRendering=false; runtimeWorld.reset();
    put(map.data(),0x20,reinterpret_cast<uintptr_t>(mapB.c_str())); put(map.data(),0x28,uint32_t(mapB.size()));
    expect(readLevelIdentity(memory,b,profile)!=levelA,"map name detects transition even if pointers are reused");
    VirtualFree(base,0,MEM_RELEASE);
    printf("PASS: %s physical collision, level transitions, menus and console.\n",profile.name);
    }
    std::filesystem::remove_all(temporaryRoot);
    puts("PASS: UI settings and live update, modes/groups/opacity, UI/console clipping, live wall activation/movement, map discovery, world decoding/BVH, async cancellation, rapid switching, no stale map fallback.");
}
