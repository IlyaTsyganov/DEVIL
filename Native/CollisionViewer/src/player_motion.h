#pragma once

struct MotionLayout {
    uint32_t physics=~0u,type=~0u,map=~0u,origin=~0u,axis=~0u,localBounds=~0u,worldBounds=~0u,velocity=~0u;
    uint32_t shape=~0u,shapePointer=~0u,shapeObject=~0u,shapeIndex=~0u,up=~0u,grounded=~0u;
    uint32_t fraction=~0u,point=~0u,normal=~0u,bytes=0;
    explicit operator bool() const { return physics!=~0u&&type!=~0u&&map!=~0u&&origin!=~0u&&axis!=~0u&&localBounds!=~0u&&
        worldBounds!=~0u&&velocity!=~0u&&shape!=~0u&&shapePointer!=~0u&&shapeObject!=~0u&&shapeIndex!=~0u&&bytes&&bytes<=0x200000; }
};
static std::shared_ptr<const MotionLayout> motionMetadata;
static Vec3 motionAdd(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
static Vec3 motionSub(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static Vec3 motionScale(Vec3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
static float motionDot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static Vec3 motionCross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
static float motionLength(Vec3 a) { return sqrtf(motionDot(a,a)); }
static bool motionFinite(Vec3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&fabsf(a.x)<1000000&&fabsf(a.y)<1000000&&fabsf(a.z)<1000000; }
struct PlayerMotion {
    bool valid=false,collider=false,contact=false,gravity=false;
    uintptr_t player=0,map=0,shape=0,physics=0;
    ULONGLONG tick=0;
    uint64_t epoch=0,sequence=0,segment=0;
    Vec3 position{},velocity{},up{},point{},normal{};
    float local[6]{},world[6]{},radius=0,axis[9]{};
    int32_t shapeIndex=-1;
    float originDistance=-1,skinGap=FLT_MAX;
    EntityReference target;
    Vec3 closest{};
    Vec3 measurementOrigin{};
    std::shared_ptr<const Geometry> core,wire;
    Vec3 transform(Vec3 p) const {
        return {position.x+axis[0]*p.x+axis[3]*p.y+axis[6]*p.z,
            position.y+axis[1]*p.x+axis[4]*p.y+axis[7]*p.z,position.z+axis[2]*p.x+axis[5]*p.y+axis[8]*p.z};
    }
};
// The body is a convex core swept by Havok's convex radius. Retain the core
// for distance queries, and tessellate the rounded surface only for its outline.
static std::shared_ptr<const Geometry> motionWire(const Geometry& core,float skinRadius) {
    auto wire=std::make_shared<Geometry>(); wire->kind=3;
    if(skinRadius<1e-6f) { wire->lines=core.lines; return wire; }
    std::vector<Vec3> vertices,normals; Vec3 center{};
    for(auto v:core.triangles) if(std::none_of(vertices.begin(),vertices.end(),[&](Vec3 p) { return motionLength(motionSub(p,v))<1e-5f; })) vertices.push_back(v);
    for(auto v:vertices) center=motionAdd(center,v);
    if(vertices.empty()) return wire;
    center=motionScale(center,1.f/static_cast<float>(vertices.size()));
    for(size_t i=0;i+2<core.triangles.size();i+=3) {
        Vec3 a=core.triangles[i],n=motionCross(motionSub(core.triangles[i+1],a),motionSub(core.triangles[i+2],a));
        float length=motionLength(n); if(length<1e-7f) continue; n=motionScale(n,1/length);
        if(motionDot(n,motionSub(a,center))<0) n=motionScale(n,-1);
        if(std::none_of(normals.begin(),normals.end(),[&](Vec3 p) { return motionDot(p,n)>.99999f; })) normals.push_back(n);
    }
    auto supports=[&](Vec3 vertex,Vec3 n) { return std::all_of(vertices.begin(),vertices.end(),[&](Vec3 p) { return motionDot(motionSub(p,vertex),n)<.00002f; }); };
    auto line=[&](Vec3 a,Vec3 b) { if(motionLength(motionSub(a,b))>1e-6f) { wire->lines.push_back(a); wire->lines.push_back(b); } };
    for(size_t i=0;i+1<core.lines.size();i+=2) {
        Vec3 a=core.lines[i],b=core.lines[i+1]; std::vector<Vec3> shared;
        for(auto n:normals) if(supports(a,n)&&supports(b,n)) { shared.push_back(n); line(motionAdd(a,motionScale(n,skinRadius)),motionAdd(b,motionScale(n,skinRadius))); }
        if(shared.size()==2) {
            Vec3 previous=shared[0];
            for(unsigned step=1;step<=12;++step) {
                float t=step/12.f; Vec3 n=motionAdd(motionScale(shared[0],1-t),motionScale(shared[1],t));
                float length=motionLength(n); if(length<1e-6f) continue; n=motionScale(n,1/length);
                for(auto p:{a,b}) line(motionAdd(p,motionScale(previous,skinRadius)),motionAdd(p,motionScale(n,skinRadius)));
                if(step%3==0) line(motionAdd(a,motionScale(n,skinRadius)),motionAdd(b,motionScale(n,skinRadius)));
                previous=n;
            }
        }
    }
    auto direction=[](int latitude,int longitude) { float lat=-1.570796327f+latitude*3.141592654f/16,lon=longitude*6.283185307f/32;
        return Vec3{cosf(lat)*cosf(lon),cosf(lat)*sinf(lon),sinf(lat)}; };
    for(auto v:vertices) for(int lat=0;lat<=16;++lat) for(int lon=0;lon<32;++lon) {
        Vec3 a=direction(lat,lon); if(!supports(v,a)) continue;
        for(auto b:{direction(lat,lon+1),direction(std::min(lat+1,16),lon)})
            if(supports(v,b)) line(motionAdd(v,motionScale(a,skinRadius)),motionAdd(v,motionScale(b,skinRadius)));
    }
    return wire;
}
class PlayerMotionReader {
    uintptr_t cachedShape=0,cachedPlayer=0;
    int32_t cachedIndex=-1;
    float cachedRadius=-1,cachedBounds[6]{};
    ULONGLONG shapeAttempt=0;
    std::shared_ptr<const Geometry> core,wire;
public:
    PlayerMotion capture(const WallMemory& memory,uintptr_t base,const GameProfile& profile,const MotionLayout& layout) {
        PlayerMotion result; result.tick=GetTickCount64();
        if(!layout) return result;
        uintptr_t system=0,physics=0,vtable=0,check=0;
        unsigned char deleted=1;
        if(!memory.get(base+profile.gameSystemRva,system)||!memory.get(system+0x50,result.map)||!result.map||
           !memory.get(result.map+profile.mapPlayerOffset,result.player)||!memory.get(result.player,vtable)||vtable!=base+profile.playerVtableRva||
           !memory.get(result.player+0xa4,deleted)||deleted||!memory.get(result.player+layout.physics,physics)||!physics) return result;
        std::vector<unsigned char> data(layout.bytes);
        if(!memory.read(physics,data.data(),data.size())) return result;
        auto get=[&](uint32_t at,auto& value) { if(uint64_t(at)+sizeof(value)>data.size()) return false; memcpy(&value,data.data()+at,sizeof(value)); return true; };
        uint32_t type=0; uintptr_t owner=0; Vec3 positionCheck{};
        if(!get(layout.type,type)||type!=5||!get(layout.map,owner)||owner!=result.map||!get(layout.origin,result.position)||
           !get(layout.axis,result.axis)||!get(layout.localBounds,result.local)||!get(layout.worldBounds,result.world)||
           !get(layout.velocity,result.velocity)||!get(layout.shapeIndex,result.shapeIndex)||!motionFinite(result.position)||!motionFinite(result.velocity)||
           result.shapeIndex<0||result.shapeIndex>8) return result;
        for(unsigned i=0;i<6;++i) if(!std::isfinite(result.local[i])||!std::isfinite(result.world[i])) return result;
        for(unsigned i=0;i<3;++i) if(result.local[i]>=result.local[i+3]||result.local[i+3]-result.local[i]>100||result.world[i]>result.world[i+3]) return result;
        for(float v:result.axis) if(!std::isfinite(v)||fabsf(v)>1.01f) return result;
        for(int i=0;i<3;++i) for(int j=i;j<3;++j) {
            float product=0; for(int k=0;k<3;++k) product+=result.axis[i*3+k]*result.axis[j*3+k];
            if(fabsf(product-(i==j?1.f:0.f))>.01f) return result;
        }
        float worldBounds[6]{FLT_MAX,FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for(unsigned corner=0;corner<8;++corner) {
            auto p=result.transform({result.local[corner&1?3:0],result.local[corner&2?4:1],result.local[corner&4?5:2]}); float xyz[]{p.x,p.y,p.z};
            for(int k=0;k<3;++k) { worldBounds[k]=std::min(worldBounds[k],xyz[k]); worldBounds[k+3]=std::max(worldBounds[k+3],xyz[k]); }
        }
        for(unsigned i=0;i<6;++i) if(fabsf(worldBounds[i]-result.world[i])>.025f) return result;
        uintptr_t wrapper=0;
        if(get(layout.shape+layout.shapePointer,wrapper)&&memory.get(wrapper+layout.shapeObject,result.shape)) {
            unsigned char header[112]{};
            if(memory.read(result.shape,header,sizeof(header))) {
                float skinRadius=0; uint16_t flags=0; memcpy(&skinRadius,header+0x28,4); memcpy(&flags,header+0x20,2);
                if((flags&2)&&std::isfinite(skinRadius)&&skinRadius>=0&&skinRadius<10) {
                    bool changed=cachedShape!=result.shape||cachedPlayer!=result.player||cachedIndex!=result.shapeIndex||cachedRadius!=skinRadius||memcmp(cachedBounds,result.local,24)!=0||result.tick-shapeAttempt>=1000;
                    if(changed) {
                        shapeAttempt=result.tick;
                        core.reset(); wire.reset(); LiveWalls decoder; decoder.memory=memory;
                        auto decoded=std::make_shared<Geometry>();
                        if(decoder.shape(result.shape,WallTransform{},*decoded)&&!decoded->triangles.empty()) {
                            float bounds[6]{FLT_MAX,FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX};
                            for(auto v:decoded->triangles) { float xyz[]{v.x,v.y,v.z}; for(int k=0;k<3;++k) { bounds[k]=std::min(bounds[k],xyz[k]); bounds[k+3]=std::max(bounds[k+3],xyz[k]); } }
                            bool matches=true;
                            for(int k=0;k<3;++k) matches=matches&&fabsf(bounds[k]-skinRadius-result.local[k])<.015f&&fabsf(bounds[k+3]+skinRadius-result.local[k+3])<.015f;
                            if(matches) { core=decoded; wire=motionWire(*core,skinRadius); }
                        }
                        cachedShape=result.shape; cachedPlayer=result.player; cachedIndex=result.shapeIndex; cachedRadius=skinRadius; memcpy(cachedBounds,result.local,24);
                    }
                    result.radius=skinRadius; result.core=core; result.wire=wire; result.collider=core&&wire&&!wire->lines.empty();
                }
            }
        }
        Vec3 gravity{};
        if(get(layout.up,gravity)&&motionFinite(gravity)&&motionLength(gravity)>.9f&&motionLength(gravity)<1.1f) { result.up=motionScale(gravity,-1/motionLength(gravity)); result.gravity=true; }
        unsigned char grounded=0; float fraction=1;
        if(get(layout.grounded,grounded)&&grounded==1&&get(layout.fraction,fraction)&&std::isfinite(fraction)&&fraction>=0&&fraction<1&&
           get(layout.point,result.point)&&get(layout.normal,result.normal)&&motionFinite(result.point)&&motionFinite(result.normal)&&
           motionLength(result.normal)>.9f&&motionLength(result.normal)<1.1f) { result.normal=motionScale(result.normal,1/motionLength(result.normal)); result.contact=true; }
        if(!result.contact) { result.point={}; result.normal={}; }
        uintptr_t currentWrapper=0,currentShape=0; int32_t currentIndex=-1;
        if(!memory.get(physics+layout.shapeIndex,currentIndex)||currentIndex!=result.shapeIndex||
           !memory.get(physics+layout.shape+layout.shapePointer,currentWrapper)||currentWrapper!=wrapper||
           (result.collider&&(!memory.get(currentWrapper+layout.shapeObject,currentShape)||currentShape!=result.shape))) return PlayerMotion{};
        if(!memory.get(result.player+layout.physics,check)||check!=physics||!memory.get(physics+layout.origin,positionCheck)||memcmp(&positionCheck,&result.position,12)!=0||
           !memory.get(result.map+profile.mapPlayerOffset,check)||check!=result.player||!memory.get(system+0x50,check)||check!=result.map||
           !memory.get(result.player+0xa4,deleted)||deleted) return PlayerMotion{};
        result.physics=physics; result.valid=true; return result;
    }
};

// Presentation-rate pose refresh. This never decodes/tessellates a shape,
// measures a target, scans entities, allocates a field buffer or writes files.
static bool refreshPlayerMotionPose(const WallMemory& memory,uintptr_t base,const GameProfile& profile,const MotionLayout& layout,
    const PlayerMotion& sample,PlayerMotion& frame) {
    if(!layout||!sample.valid||!sample.physics||GetTickCount64()-sample.tick>=250) return false;
    uintptr_t system=0,map=0,player=0,physics=0,vtable=0,owner=0,wrapper=0,shape=0,check=0;
    uint32_t type=0; unsigned char deleted=1;
    if(!memory.get(base+profile.gameSystemRva,system)||!memory.get(system+0x50,map)||map!=sample.map||
       !memory.get(map+profile.mapPlayerOffset,player)||player!=sample.player||!memory.get(player,vtable)||vtable!=base+profile.playerVtableRva||
       !memory.get(player+0xa4,deleted)||deleted||!memory.get(player+layout.physics,physics)||physics!=sample.physics||
       !memory.get(physics+layout.type,type)||type!=5||!memory.get(physics+layout.map,owner)||owner!=map) return false;
    frame=sample;
    if(!memory.get(physics+layout.origin,frame.position)||!memory.get(physics+layout.axis,frame.axis)||
       !memory.get(physics+layout.localBounds,frame.local)||!memory.get(physics+layout.worldBounds,frame.world)||
       !memory.get(physics+layout.velocity,frame.velocity)||!memory.get(physics+layout.shapeIndex,frame.shapeIndex)||
       !motionFinite(frame.position)||!motionFinite(frame.velocity)||frame.shapeIndex<0||frame.shapeIndex>8) return false;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(frame.local[i])||!std::isfinite(frame.local[i+3])||frame.local[i]>=frame.local[i+3]||frame.local[i+3]-frame.local[i]>100) return false;
        for(int j=i;j<3;++j) {
            float product=0;
            for(int k=0;k<3;++k) { float a=frame.axis[i*3+k],b=frame.axis[j*3+k]; if(!std::isfinite(a)||!std::isfinite(b)) return false; product+=a*b; }
            if(fabsf(product-(i==j?1.f:0.f))>.01f) return false;
        }
    }
    float bounds[6]{FLT_MAX,FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for(unsigned corner=0;corner<8;++corner) {
        Vec3 p=frame.transform({frame.local[corner&1?3:0],frame.local[corner&2?4:1],frame.local[corner&4?5:2]}); float xyz[]{p.x,p.y,p.z};
        for(int k=0;k<3;++k) { bounds[k]=std::min(bounds[k],xyz[k]); bounds[k+3]=std::max(bounds[k+3],xyz[k]); }
    }
    for(unsigned i=0;i<6;++i) if(!std::isfinite(frame.world[i])||fabsf(bounds[i]-frame.world[i])>.025f) return false;
    float skinRadius=-1;
    bool shapeRead=memory.get(physics+layout.shape+layout.shapePointer,wrapper)&&memory.get(wrapper+layout.shapeObject,shape)&&memory.get(shape+0x28,skinRadius);
    if(!shapeRead||shape!=sample.shape||frame.shapeIndex!=sample.shapeIndex||memcmp(frame.local,sample.local,24)!=0||skinRadius!=sample.radius) {
        frame.collider=false; frame.core.reset(); frame.wire.reset();
    }
    frame.contact=false; frame.gravity=false; frame.point={}; frame.normal={}; frame.up={};
    Vec3 gravity{},point{},normal{}; unsigned char grounded=0; float fraction=1;
    if(layout.up!=~0u&&memory.get(physics+layout.up,gravity)&&motionFinite(gravity)&&motionLength(gravity)>.9f&&motionLength(gravity)<1.1f) {
        frame.up=motionScale(gravity,-1/motionLength(gravity)); frame.gravity=true;
    }
    if(layout.grounded!=~0u&&layout.fraction!=~0u&&layout.point!=~0u&&layout.normal!=~0u&&
       memory.get(physics+layout.grounded,grounded)&&grounded==1&&memory.get(physics+layout.fraction,fraction)&&std::isfinite(fraction)&&fraction>=0&&fraction<1&&
       memory.get(physics+layout.point,point)&&memory.get(physics+layout.normal,normal)&&motionFinite(point)&&motionFinite(normal)&&motionLength(normal)>.9f&&motionLength(normal)<1.1f) {
        frame.point=point; frame.normal=motionScale(normal,1/motionLength(normal)); frame.contact=true;
    }
    Vec3 finalPosition{}; int32_t finalIndex=-1; uintptr_t finalWrapper=0;
    if(!memory.get(physics+layout.origin,finalPosition)||memcmp(&finalPosition,&frame.position,12)!=0||
       !memory.get(physics+layout.shapeIndex,finalIndex)||finalIndex!=frame.shapeIndex||
       !memory.get(physics+layout.shape+layout.shapePointer,finalWrapper)||(shapeRead&&finalWrapper!=wrapper)||
       !memory.get(player+layout.physics,check)||check!=physics||!memory.get(map+profile.mapPlayerOffset,check)||check!=player||
       !memory.get(system+0x50,check)||check!=map||!memory.get(player+0xa4,deleted)||deleted) return false;
    frame.tick=GetTickCount64(); return true;
}
static float motionCutAngle(Vec3 normal,unsigned first,unsigned second) {
    float n[]{normal.x,normal.y,normal.z}; float x=n[second],y=-n[first];
    if(x*x+y*y<1e-10f) return std::numeric_limits<float>::quiet_NaN();
    if(x<0||(fabsf(x)<1e-7f&&y<0)) { x=-x; y=-y; }
    return atan2f(y,x)*180.f/3.141592654f;
}
