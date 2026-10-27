#pragma once

static Vec3 motionClosestTriangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    Vec3 ab=motionSub(b,a),ac=motionSub(c,a),ap=motionSub(p,a);
    float d1=motionDot(ab,ap),d2=motionDot(ac,ap); if(d1<=0&&d2<=0) return a;
    Vec3 bp=motionSub(p,b); float d3=motionDot(ab,bp),d4=motionDot(ac,bp); if(d3>=0&&d4<=d3) return b;
    float vc=d1*d4-d3*d2; if(vc<=0&&d1>=0&&d3<=0) return motionAdd(a,motionScale(ab,d1/(d1-d3)));
    Vec3 cp=motionSub(p,c); float d5=motionDot(ab,cp),d6=motionDot(ac,cp); if(d6>=0&&d5<=d6) return c;
    float vb=d5*d2-d1*d6; if(vb<=0&&d2>=0&&d6<=0) return motionAdd(a,motionScale(ac,d2/(d2-d6)));
    float va=d3*d6-d5*d4; if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0) return motionAdd(b,motionScale(motionSub(c,b),(d4-d3)/((d4-d3)+(d5-d6))));
    float sum=va+vb+vc;
    if(fabsf(sum)<1e-15f) return a;
    return motionAdd(a,motionAdd(motionScale(ab,vb/sum),motionScale(ac,vc/sum)));
}
static float motionSegmentDistance(Vec3 a,Vec3 b,Vec3 c,Vec3 d) {
    Vec3 u=motionSub(b,a),v=motionSub(d,c),w=motionSub(a,c);
    float aa=motionDot(u,u),bb=motionDot(u,v),cc=motionDot(v,v),dd=motionDot(u,w),ee=motionDot(v,w);
    if(aa<1e-12f&&cc<1e-12f) return motionLength(w);
    float s=0,t=0;
    if(aa<1e-12f) t=std::clamp(ee/cc,0.f,1.f);
    else {
        if(cc<1e-12f) s=std::clamp(-dd/aa,0.f,1.f);
        else {
            float den=aa*cc-bb*bb; if(den>1e-12f) s=std::clamp((bb*ee-cc*dd)/den,0.f,1.f);
            t=(bb*s+ee)/cc;
            if(t<0) { t=0; s=std::clamp(-dd/aa,0.f,1.f); }
            else if(t>1) { t=1; s=std::clamp((bb-dd)/aa,0.f,1.f); }
        }
    }
    return motionLength(motionSub(motionAdd(a,motionScale(u,s)),motionAdd(c,motionScale(v,t))));
}
static bool motionSegmentHit(Vec3 a,Vec3 b,Vec3 p,Vec3 q,Vec3 r) {
    Vec3 direction=motionSub(b,a),e1=motionSub(q,p),e2=motionSub(r,p),h=motionCross(direction,e2);
    float det=motionDot(e1,h); if(fabsf(det)<1e-10f) return false;
    Vec3 s=motionSub(a,p); float u=motionDot(s,h)/det; if(u<0||u>1) return false;
    Vec3 v=motionCross(s,e1); float t=motionDot(e2,v)/det,weight=motionDot(direction,v)/det;
    return weight>=0&&u+weight<=1&&t>=0&&t<=1;
}
static float motionTriangleDistance(const Vec3* a,const Vec3* b) {
    float distance=FLT_MAX;
    for(unsigned i=0;i<3;++i) {
        if(motionSegmentHit(a[i],a[(i+1)%3],b[0],b[1],b[2])||motionSegmentHit(b[i],b[(i+1)%3],a[0],a[1],a[2])) return 0;
        distance=std::min(distance,motionLength(motionSub(a[i],motionClosestTriangle(a[i],b[0],b[1],b[2]))));
        distance=std::min(distance,motionLength(motionSub(b[i],motionClosestTriangle(b[i],a[0],a[1],a[2]))));
        for(unsigned j=0;j<3;++j) distance=std::min(distance,motionSegmentDistance(a[i],a[(i+1)%3],b[j],b[(j+1)%3]));
    }
    return distance;
}
static void measureMotionTarget(PlayerMotion& player,const Mesh& target) {
    if(!player.valid||!target.geometry||target.historical||target.geometry->triangles.empty()) return;
    player.target=target.entity; player.measurementOrigin=player.position; float distance=FLT_MAX,skin=FLT_MAX;
    const auto& triangles=target.geometry->triangles;
    bool measureSkin=player.collider&&player.core&&triangles.size()/3*(player.core->triangles.size()/3)<=50000;
    std::vector<Vec3> corePoints; Vec3 center{};
    if(measureSkin) {
        for(auto p:player.core->triangles) { auto world=player.transform(p); corePoints.push_back(world); center=motionAdd(center,world); }
        center=motionScale(center,1.f/static_cast<float>(corePoints.size()));
    }
    auto insideCore=[&](Vec3 p) {
        for(size_t j=0;j+2<corePoints.size();j+=3) {
            Vec3 a=corePoints[j],n=motionCross(motionSub(corePoints[j+1],a),motionSub(corePoints[j+2],a));
            if(motionDot(n,motionSub(a,center))<0) n=motionScale(n,-1);
            if(motionDot(n,motionSub(p,a))>.000001f*std::max(1.f,motionLength(n))) return false;
        }
        return !corePoints.empty();
    };
    for(size_t i=0;i+2<triangles.size();i+=3) {
        Vec3 t[]{target.world(triangles[i]),target.world(triangles[i+1]),target.world(triangles[i+2])};
        if(motionLength(motionCross(motionSub(t[1],t[0]),motionSub(t[2],t[0])))<1e-8f) continue;
        Vec3 point=motionClosestTriangle(player.position,t[0],t[1],t[2]); float d=motionLength(motionSub(player.position,point));
        if(d<distance) { distance=d; player.closest=point; }
        if(measureSkin) {
            if(insideCore(t[0])||insideCore(t[1])||insideCore(t[2])) skin=0;
            for(size_t j=0;skin>0&&j+2<corePoints.size();j+=3) skin=std::min(skin,motionTriangleDistance(corePoints.data()+j,t));
        }
    }
    if(distance<FLT_MAX) player.originDistance=distance;
    if(skin<FLT_MAX) player.skinGap=skin-player.radius;
}
struct MotionOverlayPoint { Vec3 position; uint64_t segment=0; unsigned trial=0,color=0; };
struct MotionOverlay { std::string level,profile; std::vector<MotionOverlayPoint> points; };
static std::array<float,4> motionColor(unsigned trial) {
    static const std::array<float,4> colors[]{{.25f,1,.45f,.95f},{1,.3f,.4f,.95f},{.25f,.7f,1,.95f},{1,.8f,.2f,.95f},{.8f,.45f,1,.95f},{.2f,1,1,.95f}};
    return colors[trial%6];
}
static std::shared_ptr<const MotionOverlay> readMotionOverlay(const std::filesystem::path& path,DWORD process) {
    auto result=std::make_shared<MotionOverlay>(); std::error_code error;
    if(std::filesystem::file_size(path,error)>4000000||error) return result;
    std::ifstream input(path); std::string line;
    if(!std::getline(input,line)||line.size()>2048) return result;
    auto split=[](const std::string& text) { std::vector<std::string> cells; std::istringstream stream(text); std::string cell; while(std::getline(stream,cell,'\t')) cells.push_back(cell); return cells; };
    auto header=split(line);
    if(header.size()!=4||header[0]!="#DEVIL_MOTION_OVERLAY_V1"||header[1]!=std::to_string(process)) return result;
    result->level=header[2]; result->profile=header[3];
    for(size_t count=0;count<48000&&std::getline(input,line);++count) {
        if(line.size()>256) return std::make_shared<MotionOverlay>();
        std::istringstream row(line); row.imbue(std::locale::classic()); MotionOverlayPoint point; std::string extra;
        if(!(row>>point.trial>>point.color>>point.segment>>point.position.x>>point.position.y>>point.position.z)||row>>extra||point.trial>5||point.color>5||!motionFinite(point.position)) return std::make_shared<MotionOverlay>();
        result->points.push_back(point);
    }
    if(!input.eof()) return std::make_shared<MotionOverlay>();
    return result;
}
static bool writeMotionSamples(const std::filesystem::path& path,DWORD process,uint64_t epoch,const std::string& phase,const LevelIdentity& level,
    const std::string& profile,const std::deque<PlayerMotion>& samples) {
    auto temporary=path; temporary+=L".tmp";
    try {
        std::filesystem::create_directories(path.parent_path()); std::ofstream out(temporary,std::ios::binary|std::ios::trunc); out.imbue(std::locale::classic()); out.precision(9);
        out<<"#DEVIL_MOTION_V1\t"<<process<<'\t'<<epoch<<'\t'<<phase<<'\t'<<level.name<<'\t'<<profile<<'\t'<<level.map<<':'<<level.world<<'\n';
        for(const auto& p:samples) {
            out<<"S\t"<<p.sequence<<'\t'<<p.epoch<<'\t'<<p.segment<<'\t'<<p.position.x<<'\t'<<p.position.y<<'\t'<<p.position.z<<'\t'<<p.velocity.x<<'\t'<<p.velocity.y<<'\t'<<p.velocity.z;
            for(float v:p.local) out<<'\t'<<v;
            out<<'\t'<<p.collider<<'\t'<<p.shapeIndex<<'\t'<<p.radius<<'\t'<<p.contact<<'\t'<<p.point.x<<'\t'<<p.point.y<<'\t'<<p.point.z<<'\t'<<p.normal.x<<'\t'<<p.normal.y<<'\t'<<p.normal.z
                <<'\t'<<p.up.x<<'\t'<<p.up.y<<'\t'<<p.up.z<<'\t'<<p.target.id<<'\t'<<p.target.generation<<'\t'<<p.originDistance<<'\t';
            if(p.skinGap<FLT_MAX) out<<p.skinGap;
            out<<'\n';
        }
        out.close(); return bool(out)&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
    } catch(...) { return false; }
}
class MovementLabBridge {
    std::mutex mutex;
    bool started=false,show=false,paths=false,follow=true;
    DWORD process=0;
    const GameProfile* profile=nullptr;
    std::shared_ptr<const PlayerMotion> latest;
    std::shared_ptr<const MotionOverlay> overlays;
    EntityReference target;
    LevelIdentity targetLevel;
    void run() {
        PlayerMotionReader reader; std::deque<PlayerMotion> samples;
        uint64_t sequence=0,segment=0; ULONGLONG lastSample=0,lastWrite=0,lastOverlay=0;
        LevelIdentity previous; bool interrupted=true;
        auto directory=researchDirectory(); auto path=directory/(L"motion-"+std::to_wstring(GetCurrentProcessId())+L".tsv");
        for(;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            bool collider=false,lines=false; DWORD wanted=0; const GameProfile* selected=nullptr; EntityReference aimed; LevelIdentity aimedLevel;
            { std::lock_guard<std::mutex> lock(mutex); collider=show; lines=paths; wanted=process; selected=profile; aimed=target; aimedLevel=targetLevel; }
            uint64_t epoch=encounterEpochMillis();
            bool requested=false;
            try { std::ifstream request(directory/(L"motion-"+std::to_wstring(GetCurrentProcessId())+L".request")); uint64_t time=0; request>>time; requested=time<=epoch+5000&&time+4000>=epoch; } catch(...) {}
            if(!selected||wanted!=GetCurrentProcessId()||!labAppAvailable()||(!collider&&!requested&&!lines&&!showSupportHud.load())) {
                interrupted=true; std::lock_guard<std::mutex> lock(mutex); latest.reset(); overlays.reset(); continue;
            }
            try {
                auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); auto level=readLevelIdentity(WallMemory{},base,*selected);
                if(level!=previous) { samples.clear(); previous=level; interrupted=true; }
                auto layout=std::atomic_load(&motionMetadata);
                std::string phase=!layout||!bool(*layout)?"metadata_unavailable":!level?"loading":!gameplayVisible(WallMemory{},base,*selected)?"paused":"live";
                PlayerMotion player;
                if(phase=="live") {
                    player=reader.capture(WallMemory{},base,*selected,*layout); player.epoch=epoch;
                    if(!player.valid||player.map!=level.map||readLevelIdentity(WallMemory{},base,*selected)!=level) phase="read_error";
                    else {
                        if(interrupted||!lastSample||player.tick-lastSample>250||(!samples.empty()&&motionLength(motionSub(player.position,samples.back().position))>50)) ++segment;
                        player.segment=segment; player.sequence=++sequence; lastSample=player.tick; interrupted=false;
                        if(aimed&&aimedLevel==level) {
                            std::shared_ptr<const RuntimeScene> world; std::shared_ptr<const RuntimeEntities> entities;
                            runtimeManager.snapshot(level,world,entities);
                            if(entities) for(const auto& mesh:entities->meshes) if(mesh.entity.address==aimed.address&&mesh.entity.id==aimed.id&&mesh.entity.generation==aimed.generation&&
                                !mesh.historical&&(readEntityStatus(WallMemory{},mesh.entity,base,*selected)==EntityStatus::Enabled||readEntityStatus(WallMemory{},mesh.entity,base,*selected)==EntityStatus::Disabled)) {
                                measureMotionTarget(player,mesh); break;
                            }
                        }
                        samples.push_back(player); if(samples.size()>256) samples.pop_front();
                    }
                }
                if(phase!="live") interrupted=true;
                std::shared_ptr<const MotionOverlay> nextOverlay;
                bool updateOverlay=GetTickCount64()-lastOverlay>=500;
                if(updateOverlay) { lastOverlay=GetTickCount64(); if(lines) nextOverlay=readMotionOverlay(directory/L"motion-overlays.tsv",GetCurrentProcessId()); }
                { std::lock_guard<std::mutex> lock(mutex); latest=phase=="live"?std::make_shared<PlayerMotion>(std::move(player)):nullptr; if(updateOverlay) overlays=std::move(nextOverlay); }
                if(GetTickCount64()-lastWrite>=200) { lastWrite=GetTickCount64(); writeMotionSamples(path,GetCurrentProcessId(),epoch,phase,level,selected->name,samples); }
            } catch(...) { interrupted=true; std::lock_guard<std::mutex> lock(mutex); latest.reset(); }
        }
    }
public:
    void control(const GameProfile* selected,DWORD wanted,bool collider,bool comparisons,bool followTarget) {
        std::lock_guard<std::mutex> lock(mutex); profile=selected; process=wanted; show=collider; paths=comparisons; follow=followTarget;
        if(!started&&selected&&wanted==GetCurrentProcessId()) { started=true; std::thread([this] { run(); }).detach(); }
    }
    void aim(const Mesh* mesh,const LevelIdentity& level) {
        std::lock_guard<std::mutex> lock(mutex); if(follow&&mesh&&!mesh->historical) { target=mesh->entity; targetLevel=level; }
    }
    void snapshot(std::shared_ptr<const PlayerMotion>& player,std::shared_ptr<const MotionOverlay>& pathsOut) { std::lock_guard<std::mutex> lock(mutex); player=latest; pathsOut=overlays; }
};
// Engine callbacks and the sampler live until process exit, like the other lab bridges.
static MovementLabBridge& movementLab() { static auto* bridge=new MovementLabBridge; return *bridge; }
static void appendMovementFrame(const GameCamera& camera,std::vector<Vertex>& output,const std::shared_ptr<const PlayerMotion>& player,
    const std::shared_ptr<const MotionOverlay>& overlay,bool drawPlayer,bool drawPaths,const std::string& level,const std::string& profile) {
    ViewFilter filter(camera);
    auto line=[&](Vec3 a,Vec3 b,std::array<float,4> color) {
        float bounds[]{std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z),std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};
        if(filter.distanceSquared(bounds)>cullingRadius()*cullingRadius()||!filter.inView(bounds)) return;
        for(auto p:{a,b}) { Vertex vertex{}; vertex.position=p; memcpy(vertex.color,color.data(),16); output.push_back(vertex); }
    };
    if(drawPlayer&&player&&player->valid&&GetTickCount64()-player->tick<250) {
        auto color=std::array<float,4>{.2f,1,1,.9f};
        if(player->collider&&player->wire) for(size_t i=0;i+1<player->wire->lines.size();i+=2) line(player->transform(player->wire->lines[i]),player->transform(player->wire->lines[i+1]),color);
        else for(unsigned i=0;i<8;++i) for(unsigned bit:{1u,2u,4u}) if(!(i&bit)) {
            auto point=[&](unsigned corner) { return Vec3{player->world[corner&1?3:0],player->world[corner&2?4:1],player->world[corner&4?5:2]}; };
            line(point(i),point(i|bit),{1,.8f,.2f,.8f});
        }
        if(player->contact) { line(player->point,motionAdd(player->point,motionScale(player->normal,.8f)),{1,.55f,.15f,1});
            for(auto offset:{Vec3{.08f,0,0},Vec3{0,.08f,0},Vec3{0,0,.08f}}) line(motionSub(player->point,offset),motionAdd(player->point,offset),{1,.55f,.15f,1}); }
        if(player->originDistance>=0) line(player->measurementOrigin,player->closest,{1,.8f,.2f,.9f});
    }
    if(drawPaths&&overlay&&overlay->level==level&&overlay->profile==profile)
        for(size_t i=1;i<overlay->points.size();++i) {
            const auto& a=overlay->points[i-1]; const auto& b=overlay->points[i];
            if(a.trial==b.trial&&a.segment==b.segment&&motionLength(motionSub(a.position,b.position))<=50) line(a.position,b.position,motionColor(b.color));
        }
}
static void appendMovementVertices(const GameCamera& camera,std::vector<Vertex>& output) {
    std::shared_ptr<const PlayerMotion> player; std::shared_ptr<const MotionOverlay> overlay; movementLab().snapshot(player,overlay);
    const auto* profile=currentGameProfile();
    PlayerMotion frame;
    auto layout=std::atomic_load(&motionMetadata);
    if(showPlayerCollider&&player&&profile&&layout) {
        auto began=std::chrono::steady_clock::now();
        bool valid=refreshPlayerMotionPose(WallMemory{},reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile,*layout,*player,frame);
        static uint64_t frames=0,failed=0; static double sum=0,maximum=0; static ULONGLONG reportAt=GetTickCount64();
        double microseconds=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-began).count();
        ++frames; if(!valid) ++failed; sum+=microseconds; maximum=std::max(maximum,microseconds);
        if(GetTickCount64()-reportAt>=5000) {
            char message[220]; sprintf_s(message,"Player collider frame pose: frames=%llu rejected=%llu CPU reader avg=%.3fus max=%.3fus; geometry/recording remain on background sampler.",frames,failed,sum/static_cast<double>(frames),maximum);
            note(message); frames=failed=0; sum=maximum=0; reportAt=GetTickCount64();
        }
        // Borrow the stack pose for this draw using the existing owner's control block.
        if(valid) player=std::shared_ptr<const PlayerMotion>(player,&frame); else player.reset();
    } else if(showPlayerCollider) player.reset();
    appendMovementFrame(camera,output,player,overlay,showPlayerCollider,showMotionPaths,runtimeLevel.name,profile?profile->name:"");
}
class SupportPlaneHud {
    bool visible=false,requested=false;
    ULONGLONG lastFormat=0;
    std::array<std::string,6> previous;
    // Keep the compact block above y=480: the old y=500 anchor could be clipped
    // by the debug HUD's coordinate area. Place it beside the inspector.
    static constexpr unsigned x=360,y=300,height=17;
    std::atomic<uint64_t> callbacks{0},submitted{0},visibilityRequests{0};
    std::atomic<unsigned> state{0};
    ULONGLONG reportAt=0;
public:
    void update(const MeathookHudApi& api,const PlayerMotion* player,bool wanted,ULONGLONG now) {
        ++callbacks;
        if(!api.executeCommand||!api.addText||!api.removeText) return;
        if(!wanted) {
            state=0;
            if(visible) for(unsigned i=0;i<previous.size();++i) api.removeText(x,y+i*height);
            visible=requested=false; previous={}; return;
        }
        if(visible&&now-lastFormat<100) return;
        lastFormat=now;
        if(!requested) { api.executeCommand("com_debugHUD 1"); requested=true; ++visibilityRequests; }
        state=1;
        std::array<std::string,6> lines{}; lines[0]="DEViL | Support plane";
        if(!player||!player->valid||now-player->tick>=250) lines[1]="Waiting for current support data";
        else if(!player->contact) lines[1]="No current ground-trace contact";
        else {
            state=2;
            char text[160]; sprintf_s(text,"n: %+.2f %+.2f %+.2f",player->normal.x,player->normal.y,player->normal.z); lines[1]=text;
            if(player->gravity) {
                float slope=acosf(std::clamp(motionDot(player->normal,player->up),-1.f,1.f))*180.f/3.141592654f;
                sprintf_s(text,"Slope: %.2f deg",slope); lines[2]=text;
            } else lines[2]="Gravity direction unavailable";
            unsigned first[]{0,0,1},second[]{1,2,2}; const char* names[]{"XY","XZ","YZ"};
            for(unsigned i=0;i<3;++i) {
                float angle=motionCutAngle(player->normal,first[i],second[i]);
                if(std::isfinite(angle)) sprintf_s(text,"%s tangent: %+.2f deg",names[i],angle);
                else sprintf_s(text,"%s: coincident (no unique line)",names[i]);
                lines[3+i]=text;
            }
        }
        for(unsigned i=0;i<lines.size();++i) if(!visible||previous[i]!=lines[i]) {
            if(lines[i].empty()) api.removeText(x,y+i*height);
            else { api.addText(x,y+i*height,i?0xffeeeeeeu:0xffffc040u,.7f,"%s",lines[i].c_str()); ++submitted; }
        }
        previous=std::move(lines); visible=true;
    }
    void report() {
        if(GetTickCount64()<reportAt) return; reportAt=GetTickCount64()+5000;
        char text[260]; sprintf_s(text,"Support plane HUD: callbacks=%llu submitted=%llu visibilityRequests=%llu anchor=(%u,%u) state=%s; companion/gameplay/console gates apply.",
            callbacks.load(),submitted.load(),visibilityRequests.load(),x,y,state.load()==2?"angles":state.load()==1?"waiting/no support":"hidden");
        note(text);
    }
};
static SupportPlaneHud& supportPlaneHud() { static auto* hud=new SupportPlaneHud; return *hud; }
static void updateSupportPlaneHud(const MeathookHudApi& api,const GameProfile* profile) {
    bool wanted=showSupportHud.load()&&profile&&labAppAvailable();
    auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(wanted) wanted=gameplayVisible(WallMemory{},base,*profile)&&readConsoleFraction(WallMemory{},base,*profile)<=0;
    std::shared_ptr<const PlayerMotion> player; std::shared_ptr<const MotionOverlay> ignored;
    if(wanted) movementLab().snapshot(player,ignored);
    if(player&&profile) {
        auto level=readLevelIdentity(WallMemory{},base,*profile); uintptr_t currentPlayer=0;
        if(level.map!=player->map||!WallMemory{}.get(level.map+profile->mapPlayerOffset,currentPlayer)||currentPlayer!=player->player) player.reset();
    }
    supportPlaneHud().update(api,player.get(),wanted,GetTickCount64());
}
