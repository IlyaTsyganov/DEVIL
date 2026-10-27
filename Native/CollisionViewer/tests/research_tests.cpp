#include "../src/vulkan_layer.cpp"
static void expect(bool value,const char* message) { if(!value) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
template<class T> static void put(void* memory,size_t offset,T value) { memcpy(static_cast<unsigned char*>(memory)+offset,&value,sizeof(value)); }
static size_t count(const ResearchSession& session,const char* kind) {
    return std::count_if(session.events().begin(),session.events().end(),[&](const ResearchRow& row) { return row.kind==kind; });
}
int main() {
    auto temporary=std::filesystem::temp_directory_path()/(L"DEViL-Research-Test-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(temporary);
    ResearchSession session(temporary); ResearchControl control{}; Vec3 position{0,0,0};
    Mesh volume{}; volume.kind=0; volume.entity={1,1,1,12,3,"idEncounterTrigger_Commit"};
    std::vector<Mesh> volumes{volume};
    session.update(control,1,2,"map_a",&position,&volumes,1000);
    expect(session.events().empty(),"recording is manual and initially off");
    control.recording=true; control.process=GetCurrentProcessId()+1;
    session.update(control,1,2,"map_a",&position,&volumes,1000);
    expect(session.events().empty(),"recording does not carry into another game process");
    control.process=GetCurrentProcessId(); session.update(control,1,2,"map_a",&position,&volumes,1000);
    expect(session.active()&&session.route()->points.size()==1,"manual recording starts with validated position");
    expect(count(session,"volume_state")==0&&count(session,"volume_discovered")==0,"initial baseline is not an activation");
    position.x=1; session.update(control,1,2,"map_a",&position,&volumes,1100);
    for(ULONGLONG time=1200;time<=2200;time+=100) session.update(control,1,2,"map_a",&position,&volumes,time);
    expect(session.route()->points.size()==2&&count(session,"path_break")==0,"stationary samples do not create points or false gaps");
    volumes[0].volumeState=EntityStatus::Disabled; session.update(control,1,2,"map_a",&position,&volumes,2300);
    expect(count(session,"volume_state")==1,"observed state transition produces one event");
    session.update(control,1,2,"map_a",nullptr,&volumes,2400); session.update(control,1,2,"map_a",nullptr,&volumes,2500);
    expect(count(session,"sampling_paused")==1,"pause is reported once");
    position.x=10; session.update(control,1,2,"map_a",&position,&volumes,2600);
    expect(session.route()->points[1].segment!=session.route()->points[2].segment,"resume breaks the path");
    control.recording=false; session.update(control,1,2,"map_a",&position,&volumes,2700);
    auto stopped=session.route(); position.x=20; session.update(control,1,2,"map_a",&position,&volumes,2800);
    expect(!session.active()&&session.route()->points.size()==stopped->points.size(),"stop retains the old path without recording movement");
    control.recording=true; session.update(control,1,2,"map_a",&position,&volumes,3000);
    position.x=100; session.update(control,1,2,"map_a",&position,&volumes,3100);
    expect(count(session,"path_break")==1,"large jumps do not draw connecting lines");
    control.marker=1; control.label="checkpoint note";
    session.update(control,1,2,"map_a",&position,&volumes,3200); session.update(control,1,2,"map_a",&position,&volumes,3300);
    expect(count(session,"marker")==1,"manual marker tokens are consumed exactly once");
    control.reset=42; session.update(control,1,2,"map_a",&position,&volumes,3400);
    expect(session.route()->points.size()==1&&count(session,"session_reset")==1,"new session clears the displayed path and baseline");
    session.update(control,3,4,"map_b",&position,&volumes,3500);
    expect(session.route()->points.size()==1&&session.route()->level=="map_b","map change cannot connect routes from different levels");
    control.recording=false; session.update(control,3,4,"map_b",nullptr,nullptr,3600);
    std::ifstream pointer(temporary/L"research-current.txt"); std::string filename; std::getline(pointer,filename); pointer.close();
    std::ifstream file(temporary/L"research"/filename); std::string data((std::istreambuf_iterator<char>(file)),{}); file.close();
    expect(data.find("#DEVIL_RESEARCH_V1\t") == 0&&data.find("recording_stopped")!=std::string::npos,"atomic session snapshots remain readable after stop");
    ResearchSession bounded(std::filesystem::path{}); control.recording=true; control.reset=0; control.marker=0;
    for(unsigned i=0;i<16020;++i) { position.x=float(i); bounded.update(control,1,2,"map_a",&position,nullptr,1000+100*i); }
    expect(bounded.route()->points.size()==ResearchSession::maxPoints&&bounded.events().size()==ResearchSession::maxRows,"recording memory is bounded");

    for(const auto* selected:kGameProfiles) {
        const auto& profile=*selected;
        auto* base=static_cast<unsigned char*>(VirtualAlloc(nullptr,profile.gameSystemRva+8,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        std::vector<unsigned char> system(0x60),map(profile.mapPlayerOffset+8),player(0x200),physics(0x220);
        uintptr_t b=reinterpret_cast<uintptr_t>(base);
        put(base,profile.gameSystemRva,reinterpret_cast<uintptr_t>(system.data())); put(system.data(),0x50,reinterpret_cast<uintptr_t>(map.data()));
        put(map.data(),profile.mapPlayerOffset,reinterpret_cast<uintptr_t>(player.data())); put(player.data(),0,b+profile.playerVtableRva);
        put(player.data(),0x1d0,reinterpret_cast<uintptr_t>(physics.data())); Vec3 expected{12,23,34},actual{}; memcpy(physics.data()+0xb0,&expected,12);
        expect(readResearchPosition(WallMemory{},b,profile,actual)&&actual.x==12&&actual.z==34,"player-body origin uses the selected profile");
        put(physics.data(),0xb0,std::numeric_limits<float>::quiet_NaN()); expect(!readResearchPosition(WallMemory{},b,profile,actual),"invalid positions are rejected");
        VirtualFree(base,0,MEM_RELEASE);
    }
    // Real vertex preparation: two segments, no connection through the pause.
    showTriggers=showWalls=showCollision=false; showResearchPath=true; runtimeRendering=true;
    runtimeLevel={1,2,"map_a"}; auto route=std::make_shared<ResearchRoute>(); route->map=1; route->world=2; route->level="map_a";
    route->points={{{-.4f,0,.2f},1000,1},{{-.2f,0,.2f},1100,1},{{.2f,0,.2f},1200,2},{{.4f,0,.2f},1300,2}};
    runtimeRoute=route; GameCamera camera{}; camera.matrix[0]=camera.matrix[5]=camera.matrix[10]=camera.matrix[15]=1;
    std::vector<Vertex> vertices; expect(collectVertices(camera,vertices)==0&&vertices.size()==4,"route renders only valid contiguous line segments");
    showResearchPath=false; collectVertices(camera,vertices); expect(vertices.empty(),"path visibility is independent from recording");
    showResearchPath=true; runtimeLevel.name="map_b"; collectVertices(camera,vertices); expect(vertices.empty(),"old-map route is hidden");

    expect(validLabArguments({"lab_trigger","idTrigger", "0"})&&!validLabArguments({"lab_trigger","idTrigger","2"}),"class command is validated");
    expect(validLabArguments({"lab_distance","200"})&&!validLabArguments({"lab_distance","201"})&&!validLabArguments({"lab_distance","20;quit"}),"console numbers never become engine command text");
    expect(!validLabArguments({"quit","1"})&&validLabArguments({"lab_clear"})&&!validLabArguments({"lab_clear","1"}),"only explicit lab commands are accepted");
    std::wstring presenceName=L"Local\\DEViL-Lab-Test-"+std::to_wstring(GetCurrentProcessId());
    expect(!labAppAvailable(presenceName.c_str()),"lab commands are unavailable without the companion presence");
    HANDLE presence=CreateEventW(nullptr,TRUE,TRUE,presenceName.c_str());
    expect(presence&&labAppAvailable(presenceName.c_str()),"live companion presence permits lab commands");
    CloseHandle(presence); expect(!labAppAvailable(presenceName.c_str()),"lab commands become inactive when the companion closes");
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    uintptr_t b=reinterpret_cast<uintptr_t>(block); GameProfile testProfile=*kGameProfiles[0]; testProfile.imageSize=0x20000;
    const unsigned char thunk[]{0x48,0x89,0xca,0x48,0x8b,0x05,0,0,0,0,0x48,0x8b,0x08,0x48,0x8b,0x01,0x48,0x8b,0x40,0x40,0x48,0xff,0xe0};
    memcpy(block+0x100,thunk,23); put(block,0x106,int32_t(0x400-0x10a)); put(block,0x400,b+0x308); put(block,0x308,b+0x500);
    put(block,0x500,b+0x1000); put(block,0xff8,b+0x2000); put(block,0x2000,uint32_t(1)); put(block,0x200c,uint32_t(0x3000)); put(block,0x2014,uint32_t(0x2000));
    memcpy(block+0x3010,".?AVidCmdSystemLocal@@",22);
    put(block,0x1018,b+0x10000); put(block,0x1028,b+0x10010); put(block,0x1040,b+0x10020);
    DWORD old=0; VirtualProtect(block+0x10000,4096,PAGE_EXECUTE_READ,&old);
    expect(bool(resolveLabCommandSystem(WallMemory{},b+0x100,b,testProfile)),"recognized command thunk and correct engine RTTI resolve the ABI");
    put(block,0x300,b+0x600); put(block,0x600,b+0x1200); put(block,0x11f8,b+0x2200);
    put(block,0x2200,uint32_t(1)); put(block,0x220c,uint32_t(0x3200)); put(block,0x2214,uint32_t(0x2200));
    memcpy(block+0x3210,".?AVidCVarSystemLocal@@",sizeof(".?AVidCVarSystemLocal@@")); put(block,0x1220,b+0x10040);
    auto commandSystem=resolveLabCommandSystem(WallMemory{},b+0x100,b,testProfile);
    expect(bool(resolveLabCvarSystem(WallMemory{},commandSystem,b,testProfile)),"native cvar-system ABI is identified independently by RTTI");
    block[0x3214]='x'; expect(!resolveLabCvarSystem(WallMemory{},commandSystem,b,testProfile),"wrong cvar-system type is rejected before creating variables"); block[0x3214]='i';
    block[0x3014]='x'; expect(!resolveLabCommandSystem(WallMemory{},b+0x100,b,testProfile),"wrong command-system type withholds registration"); block[0x3014]='i';
    block[0x100]=0; expect(!resolveLabCommandSystem(WallMemory{},b+0x100,b,testProfile),"unknown meathook thunk is rejected"); VirtualFree(block,0,MEM_RELEASE);
    expect(std::filesystem::equivalent(temporary.parent_path(),std::filesystem::temp_directory_path()),"test cleanup stays inside its named temporary directory");
    session.finish(3700);
    std::filesystem::remove_all(temporary);
    puts("PASS: manual recording, PID scope, paused/stopped paths, map changes, markers, bounded snapshots, route vertices, player origins on 22 profiles and fail-closed lab command ABI validation.");
}
