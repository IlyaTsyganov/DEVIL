#include "../src/vulkan_layer.cpp"
#include <cstdarg>
static void expect(bool condition,const char* message) { if(!condition) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
template<class T> static void put(void* memory,size_t at,T value) { memcpy(static_cast<unsigned char*>(memory)+at,&value,sizeof(value)); }
static std::unordered_map<unsigned,std::string> surfaceRows;
static unsigned surfaceRequests=0;
static void surfaceCommand(const char* text) { expect(strcmp(text,"com_debugHUD 1")==0,"HUD uses only its visibility request"); ++surfaceRequests; }
static void surfaceText(unsigned,unsigned y,unsigned,float,const char* format,...) { char text[512]; va_list args; va_start(args,format); vsprintf_s(text,format,args); va_end(args); surfaceRows[y]=text; }
static void surfaceRemove(unsigned,unsigned y) { surfaceRows.erase(y); }
static InspectorField motionFixtureField(const std::string& type,const char* name) {
    const std::string key=type+"::"+name;
    const std::unordered_map<std::string,InspectorField> fields{
        {"idEntity::physics",{0x1d0,8,"idPhysics*"}},{"idPhysics::type",{0x14,4,"physicsType_t"}},{"idPhysics::mapInstance",{0x70,8,"idMapInstance*"}},
        {"idHavokPhysics_Base::bodyOrigin",{0xb0,12,"idVec3"}},{"idHavokPhysics_Base::bodyAxis",{0x8c,36,"idMat3"}},
        {"idHavokPhysics_Base::bodyBoundsLocal",{0xc8,24,"idBounds"}},{"idHavokPhysics_Base::bodyBoundsWorld",{0xe0,24,"idBounds"}},
        {"idHavokPhysics_Base::bodyLinearVelocity",{0xf8,12,"idVec3"}},{"idHavokPhysics_Base::shape",{0x1c0,8,"idHavokShapePointer"}},
        {"idHavokShapePointer::pointer",{0,8,"idHavokShape*"}},{"idHavokShape::shape",{0,8,"hknpShape*"}},
        {"idHavokPhysics_Player::currentHavokBodyShapeIndex",{0x3cc4,4,"int"}},{"idHavokPhysics_Player::gravityNormal",{0x4668,12,"idVec3"}},
        {"idHavokPhysics_Player::current",{0x3f28,184,"playerPState_t"}},{"playerPState_t::groundPlane",{0xa0,1,"bool"}},
        {"idHavokPhysics_Player::groundTrace",{0x41cc,128,"trace_t"}},{"trace_t::c",{0x34,76,"contactInfo_t"}},{"trace_t::fraction",{0,4,"float"}},
        {"contactInfo_t::point",{4,12,"idVec3"}},{"contactInfo_t::normal",{16,12,"idVec3"}}
    };
    auto found=fields.find(key); return found==fields.end()?InspectorField{}:found->second;
}
int main() {
    const auto layout=resolveMotionLayout(motionFixtureField); expect(bool(layout),"motion layout resolves from reflected types");
    auto unsupported=resolveMotionLayout([](const std::string& type,const char* name) { return type=="idHavokShape"?InspectorField{}:motionFixtureField(type,name); });
    expect(!unsupported,"unrecognized shape layout does not fabricate a collider");
    PlayerMotion valid;
    for(const auto* selected:kGameProfiles) {
        const auto& profile=*selected;
        auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,profile.gameSystemRva+8,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        uintptr_t base=reinterpret_cast<uintptr_t>(image);
        std::vector<unsigned char> system(0x60),map(profile.mapPlayerOffset+8),player(0x240),physics(0x4900),shape(0x200);
        put(image,profile.gameSystemRva,reinterpret_cast<uintptr_t>(system.data())); put(system.data(),0x50,reinterpret_cast<uintptr_t>(map.data()));
        put(map.data(),profile.mapPlayerOffset,reinterpret_cast<uintptr_t>(player.data())); put(player.data(),0,base+profile.playerVtableRva);
        put(player.data(),layout.physics,reinterpret_cast<uintptr_t>(physics.data())); put(physics.data(),layout.type,uint32_t(5)); put(physics.data(),layout.map,reinterpret_cast<uintptr_t>(map.data()));
        float axis[]{1.f,0,0,0,1,0,0,0,1},local[]{-.4f,-.4f,0,.4f,.4f,1.8f},world[]{9.6f,19.6f,30,10.4f,20.4f,31.8f};
        memcpy(physics.data()+layout.axis,axis,36); memcpy(physics.data()+layout.localBounds,local,24); memcpy(physics.data()+layout.worldBounds,world,24);
        put(physics.data(),layout.origin,Vec3{10,20,30}); put(physics.data(),layout.velocity,Vec3{3,4,2}); put(physics.data(),layout.up,Vec3{0,0,-1});
        put(physics.data(),layout.grounded,uint8_t(1)); put(physics.data(),layout.fraction,.5f); put(physics.data(),layout.point,Vec3{10,20,30}); put(physics.data(),layout.normal,Vec3{0,0,1});
        uintptr_t shapePointer=reinterpret_cast<uintptr_t>(shape.data());
        put(physics.data(),layout.shape,reinterpret_cast<uintptr_t>(&shapePointer));
        put(shape.data(),0x20,uint16_t(2)); put(shape.data(),0x22,uint8_t(3)); put(shape.data(),0x28,.3f);
        put(shape.data(),0x42,uint16_t(8)); put(shape.data(),0x44,uint16_t(0x70-0x42)); put(shape.data(),0x54,uint16_t(6)); put(shape.data(),0x56,uint16_t(0xf0-0x54));
        put(shape.data(),0x58,uint16_t(24)); put(shape.data(),0x5a,uint16_t(0x108-0x58));
        for(unsigned i=0;i<8;++i) { float v[]{i&1?.1f:-.1f,i&2?.1f:-.1f,i&4?1.5f:.3f,0}; memcpy(shape.data()+0x70+i*16,v,16); }
        const uint8_t corners[]{0,1,3,2,4,6,7,5,0,4,5,1,2,3,7,6,0,2,6,4,1,5,7,3}; memcpy(shape.data()+0x108,corners,24);
        for(unsigned i=0;i<6;++i) { put(shape.data(),0xf0+i*4,uint16_t(i*4)); shape[0xf0+i*4+2]=4; }
        PlayerMotionReader reader; auto result=reader.capture(WallMemory{},base,profile,layout);
        expect(result.valid&&result.collider&&result.gravity&&result.contact&&result.core->triangles.size()==36&&result.velocity.x==3,"player body/hull/velocity/support use selected profile and reflected physics layout");
        expect(result.wire->lines.size()>100&&fabsf(result.radius-.3f)<1e-6f,"rounded hull includes skin rather than only the tiny convex core");
        float lower=FLT_MAX,upper=-FLT_MAX; for(auto p:result.wire->lines) { lower=std::min(lower,p.z); upper=std::max(upper,p.z); }
        expect(fabsf(lower)<1e-4f&&fabsf(upper-1.8f)<1e-4f,"rounded wire extents agree with actual body bounds");
        // Between two 20Hz snapshots the frame reader must follow real physics,
        // while continuing to borrow the same decoded wire.
        auto source=result; PlayerMotion frame;
        put(physics.data(),layout.origin,Vec3{11,20,30}); world[0]+=1; world[3]+=1; memcpy(physics.data()+layout.worldBounds,world,24);
        expect(refreshPlayerMotionPose(WallMemory{},base,profile,layout,source,frame)&&frame.position.x==11&&source.position.x==10&&frame.wire==source.wire,"frame pose follows movement independently of buffered telemetry and reuses wire");
        put(physics.data(),layout.shapeIndex,int32_t(1));
        expect(refreshPlayerMotionPose(WallMemory{},base,profile,layout,source,frame)&&!frame.collider&&!frame.wire,"new pose uses current bounds until the background decoder resolves its new shape");
        put(physics.data(),layout.shapeIndex,int32_t(0));
        put(physics.data(),layout.velocity,Vec3{NAN,0,0}); expect(!refreshPlayerMotionPose(WallMemory{},base,profile,layout,source,frame),"frame reader rejects invalid velocity"); put(physics.data(),layout.velocity,Vec3{3,4,2});
        put(physics.data(),layout.origin,Vec3{10,20,30}); world[0]-=1; world[3]-=1; memcpy(physics.data()+layout.worldBounds,world,24);
        if(selected==kGameProfiles[std::size(kGameProfiles)-1]) {
            constexpr unsigned iterations=10000; auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<iterations;++i) { if(i%100==0) source.tick=GetTickCount64(); expect(refreshPlayerMotionPose(WallMemory{},base,profile,layout,source,frame),"frame benchmark valid"); }
            printf("Frame pose reader local fixture: %.3f us/call (%u calls), no shape decoding or file publication.\n",std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/iterations,iterations);
        }
        put(physics.data(),layout.grounded,uint8_t(0)); expect(!reader.capture(WallMemory{},base,profile,layout).contact,"airborne state does not replay an old support trace"); put(physics.data(),layout.grounded,uint8_t(1));
        put(shape.data(),0x28,.1f); result=reader.capture(WallMemory{},base,profile,layout);
        expect(result.valid&&!result.collider,"radius/core mismatch exposes bounds-only fallback"); put(shape.data(),0x28,.3f);
        put(physics.data(),layout.velocity,Vec3{NAN,0,0}); expect(!reader.capture(WallMemory{},base,profile,layout).valid,"invalid velocity cannot enter a recording"); put(physics.data(),layout.velocity,Vec3{3,4,2});
        put(physics.data(),layout.worldBounds,999.f); expect(!reader.capture(WallMemory{},base,profile,layout).valid,"torn world/local bounds are rejected"); memcpy(physics.data()+layout.worldBounds,world,24);
        player[0xa4]=1; expect(!reader.capture(WallMemory{},base,profile,layout).valid,"removed player cannot retain old collider"); player[0xa4]=0;
        valid=reader.capture(WallMemory{},base,profile,layout); valid.epoch=1234567; valid.sequence=1; valid.segment=1;
        VirtualFree(image,0,MEM_RELEASE);
    }
    Vec3 a[]{ {0,0,0},{2,0,0},{0,2,0} },b[]{ {0,0,2},{2,0,2},{0,2,2} },crossing[]{ {.5f,.5f,-1},{.5f,.5f,1},{1,.5f,0} };
    expect(fabsf(motionTriangleDistance(a,b)-2)<1e-5f&&motionTriangleDistance(a,crossing)<1e-5f,"distance handles parallel and intersecting triangles");
    const float half=sqrtf(.5f);
    expect(!std::isfinite(motionCutAngle({0,0,1},0,1))&&fabsf(motionCutAngle({0,0,1},0,2))<1e-5f,"flat floor has no unique XY line and horizontal XZ cut");
    expect(fabsf(motionCutAngle({half,0,half},0,2)+45)<1e-4f&&fabsf(motionCutAngle({0,half,half},1,2)+45)<1e-4f,"plane cut angles preserve signed slopes in world axes");
    expect(fabsf(motionCutAngle({1,0,0},0,2)-90)<1e-4f&&!std::isfinite(motionCutAngle({1,0,0},1,2)),"vertical support has a vertical XZ cut and coincident YZ plane");
    MeathookHudApi hudApi{}; hudApi.executeCommand=surfaceCommand; hudApi.addText=surfaceText; hudApi.removeText=surfaceRemove;
    SupportPlaneHud hud; surfaceRows[280]="Inspector"; valid.tick=1000; hud.update(hudApi,&valid,true,1000);
    expect(surfaceRequests==1&&surfaceRows[351].find("no unique")!=std::string::npos&&surfaceRows[368].find("0.00")!=std::string::npos,"surface HUD distinguishes plane cuts from normal angles within visible HUD coordinates");
    hud.update(hudApi,&valid,true,1050); expect(surfaceRequests==1,"surface HUD does not spam visibility commands");
    auto airborne=valid; airborne.contact=false; airborne.tick=1200; hud.update(hudApi,&airborne,true,1200);
    expect(surfaceRows[317].find("No current")!=std::string::npos&&surfaceRows.count(351)==0,"airborne HUD clears old angle values");
    hud.update(hudApi,nullptr,false,1400); expect(surfaceRows.size()==1&&surfaceRows[280]=="Inspector","surface HUD hides only its own rows");
    Mesh target{}; target.entity={1,1,1,12,3,"idTrigger"}; auto geometry=std::make_shared<Geometry>(); geometry->triangles={{8,18,32.8f},{12,18,32.8f},{10,22,32.8f}}; target.geometry=geometry;
    measureMotionTarget(valid,target); expect(fabsf(valid.originDistance-2.8f)<1e-4f&&fabsf(valid.skinGap-1)<1e-4f,"origin distance and rounded-skin gap are distinct surface measurements");
    geometry->triangles={{9.98f,19.98f,30.9f},{10.02f,19.98f,30.9f},{10,20.02f,30.9f}};
    measureMotionTarget(valid,target); expect(valid.skinGap<=0,"a boundary entirely inside the convex core has no positive clearance");
    auto temporary=std::filesystem::temp_directory_path()/(L"DEViL-Motion-Test-"+std::to_wstring(GetCurrentProcessId())); std::filesystem::create_directories(temporary);
    auto file=temporary/L"motion.tsv"; std::deque<PlayerMotion> samples{valid};
    expect(writeMotionSamples(file,1234,1234567,"live",{1,2,"map_a"},"profile_a",samples),"telemetry publishes atomically");
    { std::ifstream input(file); std::string text((std::istreambuf_iterator<char>(input)),{}); expect(text.find("#DEVIL_MOTION_V1\t1234") == 0&&text.find("\nS\t1\t1234567")!=std::string::npos,"native protocol contains physical values and sequence"); }
    { std::ofstream output(temporary/L"overlays.tsv"); output<<"#DEVIL_MOTION_OVERLAY_V1\t1234\tmap_a\tprofile_a\n0\t1\t1\t1\t2\t3\n1\t1\t1\t4\t5\t6\n"; }
    auto overlay=readMotionOverlay(temporary/L"overlays.tsv",1234); expect(overlay->points.size()==2&&overlay->points[0].trial!=overlay->points[1].trial,"same-outcome attempts keep separate route identities");
    expect(readMotionOverlay(temporary/L"overlays.tsv",4321)->points.empty(),"overlay is scoped to selected game PID");
    GameCamera camera{}; camera.matrix[0]=camera.matrix[5]=camera.matrix[10]=camera.matrix[15]=1;
    auto frame=std::make_shared<PlayerMotion>(); frame->valid=frame->collider=true; frame->tick=GetTickCount64(); frame->axis[0]=frame->axis[4]=frame->axis[8]=1;
    auto wire=std::make_shared<Geometry>(); wire->lines={{-.1f,0,.2f},{.1f,0,.2f}}; frame->wire=wire;
    std::vector<Vertex> vertices; appendMovementFrame(camera,vertices,frame,nullptr,true,false,"map_a","profile_a");
    expect(vertices.size()==2&&vertices[0].color[1]==1,"actual vertex preparation draws the player wire independently of world geometry");
    vertices.clear(); frame->tick-=300; appendMovementFrame(camera,vertices,frame,nullptr,true,false,"map_a","profile_a"); expect(vertices.empty(),"stale player wire disappears");
    auto routes=std::make_shared<MotionOverlay>(); routes->level="map_a"; routes->profile="profile_a";
    routes->points={{{-.3f,0,.2f},1,0,0},{{-.1f,0,.2f},1,0,0},{{.1f,0,.2f},2,0,0},{{.3f,0,.2f},2,1,0}};
    appendMovementFrame(camera,vertices,nullptr,routes,false,true,"map_a","profile_a"); expect(vertices.size()==2,"path segments never connect across gaps or different attempts");
    vertices.clear(); appendMovementFrame(camera,vertices,nullptr,routes,false,true,"map_b","profile_a"); expect(vertices.empty(),"other-map attempts are not rendered");
    std::filesystem::remove(file); std::filesystem::remove(temporary/L"overlays.tsv"); std::filesystem::remove(temporary);
    puts("PASS: verified rounded player hull, bounds fallback, velocity/support, invalid/torn state, boundary/skin distances, telemetry and separated overlays on 22 profiles.");
}
