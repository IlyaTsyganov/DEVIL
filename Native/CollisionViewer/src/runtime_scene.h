#pragma once
struct LevelIdentity {
    uintptr_t map=0,world=0;
    std::string name;
    bool operator==(const LevelIdentity& other) const { return map==other.map&&world==other.world&&name==other.name; }
    bool operator!=(const LevelIdentity& other) const { return !(*this==other); }
    explicit operator bool() const { return map&&world&&!name.empty(); }
};
static LevelIdentity readLevelIdentity(const WallMemory& memory,uintptr_t base,const GameProfile& profile) {
    LevelIdentity result;
    uintptr_t system=0,player=0,physics=0,havok=0,wrapper=0,text=0; uint32_t length=0,state=0;
    if(!memory.get(base+profile.gameSystemRva,system)||!memory.get(system+0x44,state)||state!=2||
       !memory.get(system+0x50,result.map)||!memory.get(result.map+0x20,text)||!memory.get(result.map+0x28,length)||!length||length>511) return {};
    char name[512]{};
    if(!memory.read(text,name,length)) return {};
    for(uint32_t i=0;i<length;++i) if(name[i]<32||name[i]>=127) return {};
    result.name.assign(name,length);
    if(!memory.get(result.map+0x1ad0,player)||!memory.get(player+0x1d0,physics)||!memory.get(physics+0x80,havok)||
       !memory.get(havok+0x210b0,wrapper)||!memory.get(wrapper,result.world)||!result.world) return {};
    return result;
}
struct RuntimeScene {
    LevelIdentity level;
    std::vector<Mesh> meshes;
    std::vector<SpatialNode> nodes;
    std::vector<uint32_t> order;
    uint32_t unique=0,unsupported=0,nonBlocking=0;
    uint64_t triangles=0;
    double milliseconds=0;
};
struct RuntimeSceneBuilder {
    WallMemory memory;
    std::function<bool()> cancelled=[] { return false; };
    struct Cached { std::shared_ptr<const Geometry> geometry; float bounds[6]; };
    std::unordered_map<uintptr_t,Cached> cache;
    std::unordered_map<uint8_t,uint32_t> unsupportedTypes;
    std::unordered_map<std::string,uint32_t> errors;
    ShapeContentsFilter contents;
    bool failed=false;
    bool walk(uintptr_t address,const WallTransform& transform,RuntimeScene& scene,unsigned depth=0) {
        if(cancelled()) return false;
        if(depth>64||scene.meshes.size()>2000000) { failed=true; return false; }
        unsigned char header[112]{};
        if(!memory.read(address,header,sizeof(header))) { failed=true; return false; }
        if(header[0x22]==13) {
            uintptr_t data=0; uint32_t count=0;
            memcpy(&data,header+0x50,8); memcpy(&count,header+0x58,4);
            if(count>1000000) { failed=true; return false; }
            // Read bounded chunks, allowing cancellation during large compound walks.
            std::array<unsigned char,112*256> rows;
            for(uint32_t begin=0;begin<count;begin+=256) {
                uint32_t batch=std::min(256u,count-begin);
                if(cancelled()) return false;
                if(!memory.read(data+size_t(begin)*112,rows.data(),size_t(batch)*112)) { failed=true; return false; }
                for(uint32_t i=0;i<batch;++i) {
                    const auto* row=rows.data()+size_t(i)*112;
                    if(row[0x5c]) continue;
                    uintptr_t child=0; float columns[16],scale[3];
                    memcpy(&child,row+0x50,8); memcpy(columns,row,64); memcpy(scale,row+0x40,12);
                    WallTransform local;
                    for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
                        float v=c<3?columns[c*4+r]*scale[c]:columns[12+r];
                        if(!std::isfinite(v)||fabsf(v)>1000000) { failed=true; return false; }
                        local.m[r*4+c]=v;
                    }
                    if(!walk(child,transform.compose(local),scene,depth+1)) return false;
                }
            }
            return true;
        }
        if(!contents.allowsShape(address)) { ++scene.nonBlocking; return true; }
        auto found=cache.find(address);
        if(found==cache.end()) {
            LiveWalls decoder; decoder.memory=memory;
            auto geometry=std::make_shared<Geometry>(); geometry->kind=1;
            bool decoded=decoder.shape(address,WallTransform{},*geometry,0,&contents);
            if(decoded&&geometry->triangles.empty()) { ++errors["empty shape"]; return true; }
            if(!decoded) {
                ++scene.unsupported; ++unsupportedTypes[header[0x22]]; ++errors[decoder.shapeError]; return true;
            }
            Cached item{}; item.geometry=geometry;
            for(int k=0;k<3;++k) { item.bounds[k]=FLT_MAX; item.bounds[k+3]=-FLT_MAX; }
            for(Vec3 point:geometry->triangles) {
                float xyz[3]{point.x,point.y,point.z};
                for(int k=0;k<3;++k) { item.bounds[k]=std::min(item.bounds[k],xyz[k]); item.bounds[k+3]=std::max(item.bounds[k+3],xyz[k]); }
            }
            found=cache.emplace(address,std::move(item)).first;
        }
        Mesh mesh{}; mesh.kind=1; mesh.geometry=found->second.geometry; memcpy(mesh.matrix,transform.m,48);
        for(int k=0;k<3;++k) { mesh.bounds[k]=FLT_MAX; mesh.bounds[k+3]=-FLT_MAX; }
        const float* bounds=found->second.bounds;
        for(unsigned corner=0;corner<8;++corner) {
            Vec3 p=mesh.world({bounds[corner&1?3:0],bounds[corner&2?4:1],bounds[corner&4?5:2]}); float xyz[3]{p.x,p.y,p.z};
            for(int k=0;k<3;++k) { mesh.bounds[k]=std::min(mesh.bounds[k],xyz[k]); mesh.bounds[k+3]=std::max(mesh.bounds[k+3],xyz[k]); }
        }
        scene.triangles+=mesh.geometry->triangles.size()/3;
        scene.meshes.push_back(std::move(mesh));
        return true;
    }
    std::shared_ptr<RuntimeScene> build(const LevelIdentity& level) {
        auto start=std::chrono::steady_clock::now();
        auto scene=std::make_shared<RuntimeScene>(); scene->level=level;
        cache.clear(); unsupportedTypes.clear(); errors.clear(); contents.memory=memory; contents.clear(); failed=false;
        if(!walk(level.world,WallTransform{},*scene)||failed||cancelled()) return {};
        scene->unique=static_cast<uint32_t>(cache.size()); cache.clear();
        scene->order.resize(scene->meshes.size()); std::iota(scene->order.begin(),scene->order.end(),0u);
        if(!scene->meshes.empty()) buildSpatialNodeFor(scene->meshes,scene->nodes,scene->order,0,static_cast<uint32_t>(scene->meshes.size()));
        if(cancelled()) return {};
        scene->milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        return scene;
    }
};

struct RuntimeEntities {
    LevelIdentity level;
    std::vector<Mesh> meshes;
    uint32_t triggers=0,walls=0,unsupported=0;
    double milliseconds=0;
};
// The Vulkan presentation thread only exchanges completed snapshots. All map
// traversal, geometry decoding, BVH construction and large frees run here.
class RuntimeSceneManager {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::atomic<uint64_t> generation{0};
    std::atomic<bool> stopping{false};
    LevelIdentity requested;
    const GameProfile* requestedProfile=nullptr;
    uintptr_t base=0;
    WallMemory memory;
    std::shared_ptr<const RuntimeScene> world;
    std::shared_ptr<const RuntimeEntities> entities;
    std::vector<std::shared_ptr<const RuntimeScene>> retired;
    void run() {
        uint64_t version=0; LevelIdentity level; uintptr_t image=0; bool built=false; const GameProfile* profile=nullptr;
        ULONGLONG retryAt=0;
        LiveWalls scanner; scanner.memory=memory;
        while(!stopping.load()) {
            std::vector<std::shared_ptr<const RuntimeScene>> garbage;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock,std::chrono::milliseconds(100),[&] { return stopping.load()||generation.load()!=version||!retired.empty(); });
                if(stopping.load()) break;
                garbage.swap(retired);
                if(generation.load()!=version) { version=generation.load(); level=requested; image=base; profile=requestedProfile; built=false; retryAt=0; }
            }
            garbage.clear();
            if(!level||!profile) continue;
            try {
                if(readLevelIdentity(memory,image,*profile)!=level) continue;
                scanner.refresh(image,*profile,true);
                auto next=std::make_shared<RuntimeEntities>(); next->level=level;
                next->meshes.swap(scanner.meshes); next->triggers=scanner.triggers; next->walls=scanner.active;
                next->unsupported=scanner.unsupported; next->milliseconds=scanner.milliseconds;
                if(readLevelIdentity(memory,image,*profile)!=level) continue;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if(generation.load()!=version) continue;
                    entities=std::move(next);
                }
                if(!built&&GetTickCount64()>=retryAt) {
                    RuntimeSceneBuilder builder; builder.memory=memory;
                    builder.cancelled=[&] { return stopping.load()||generation.load()!=version; };
                    auto nextWorld=builder.build(level);
                    if(nextWorld&&readLevelIdentity(memory,image,*profile)==level) {
                        std::lock_guard<std::mutex> lock(mutex);
                        if(generation.load()==version) { world=std::move(nextWorld); built=true; }
                    }
                    retryAt=GetTickCount64()+2000;
                }
            } catch(...) { retryAt=GetTickCount64()+2000; }
        }
    }
public:
    RuntimeSceneManager()=default;
    ~RuntimeSceneManager() { stop(); }
    void configure(WallMemory reader) { memory=reader; } // Before the first request; used by read-only probes/tests.
    void request(const LevelIdentity& level,uintptr_t image,const GameProfile& profile) {
        std::lock_guard<std::mutex> lock(mutex);
        if(requested==level&&requestedProfile==&profile&&worker.joinable()) return;
        requested=level; base=image; requestedProfile=&profile; world.reset(); entities.reset(); ++generation;
        if(!worker.joinable()) { stopping=false; worker=std::thread([this] { run(); }); }
        wake.notify_one();
    }
    void retire(std::shared_ptr<const RuntimeScene> old) {
        if(!old) return;
        std::lock_guard<std::mutex> lock(mutex); retired.push_back(std::move(old)); wake.notify_one();
    }
    void snapshot(const LevelIdentity& level,std::shared_ptr<const RuntimeScene>& outWorld,std::shared_ptr<const RuntimeEntities>& outEntities) {
        std::lock_guard<std::mutex> lock(mutex);
        outWorld=world&&world->level==level?world:nullptr;
        outEntities=entities&&entities->level==level?entities:nullptr;
    }
    void stop() {
        stopping=true; ++generation; wake.notify_one();
        if(worker.joinable()) worker.join();
        world.reset(); entities.reset(); retired.clear(); requested={};
    }
};
static RuntimeSceneManager runtimeManager;
static LevelIdentity runtimeLevel;
static std::shared_ptr<const RuntimeScene> runtimeWorld;
static std::shared_ptr<const RuntimeEntities> runtimeEntities;
static bool runtimeRendering=false;
static bool updateRuntimeScene() {
    uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const GameProfile* profile=currentGameProfile();
    if(!profile) return false;
    LevelIdentity level=readLevelIdentity(WallMemory{},base,*profile);
    runtimeRendering=true;
    if(level!=runtimeLevel) {
        runtimeManager.retire(std::move(runtimeWorld)); runtimeEntities.reset(); runtimeLevel=level;
        runtimeManager.request(level,base,*profile); liveWalls.meshes.clear();
        char message[640]; sprintf_s(message,"Level changed: %s; old geometry removed, preparing current physics scene.",level?level.name.c_str():"loading/unavailable"); note(message);
    }
    else if(level&&!runtimeWorld) runtimeManager.request(level,base,*profile);
    bool hadWorld=bool(runtimeWorld);
    runtimeManager.snapshot(level,runtimeWorld,runtimeEntities);
    if(!hadWorld&&runtimeWorld) {
        char message[800]; sprintf_s(message,"Runtime scene ready: %s, %zu player-blocking collision instances, %u unique geometries, %llu triangles, filtered non-blocking=%u, unsupported=%u, background build=%.1fms.",level.name.c_str(),runtimeWorld->meshes.size(),runtimeWorld->unique,runtimeWorld->triangles,runtimeWorld->nonBlocking,runtimeWorld->unsupported,runtimeWorld->milliseconds); note(message);
    }
    static ULONGLONG lastEntities=0;
    if(runtimeEntities&&GetTickCount64()-lastEntities>5000) {
        char message[240]; sprintf_s(message,"Runtime entities: %u triggers, %u invisible walls, unsupported=%u, background scan=%.2fms.",runtimeEntities->triggers,runtimeEntities->walls,runtimeEntities->unsupported,runtimeEntities->milliseconds); note(message); lastEntities=GetTickCount64();
    }
    return bool(level)&&(runtimeWorld||runtimeEntities);
}
