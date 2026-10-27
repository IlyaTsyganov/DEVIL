#pragma once
// Shared read-only layouts: idEntity, idPhysicsProperties, idHavokPhysics_Base;
// Havok member offsets also match the map's embedded TAG0 type descriptions.
#include "native_profile.h"
#include "volume_history.h"
// DOOM stores gameplay contents beside each Havok shape in an engine-owned
// idHavokShape table. Validate the owner chain before using those flags.
struct ShapeContentsFilter {
    static constexpr uintptr_t shapeUserData=0x30, tableList=0x08, tableCount=0x10;
    static constexpr size_t entrySize=0x58;
    static constexpr uint32_t solid=0x1, playerClip=0x8, noCollision=0x40000000;
    struct Table { uintptr_t data=0; std::vector<uint32_t> contents; };
    WallMemory memory;
    std::unordered_map<uintptr_t,Table> tables;
    void clear() { tables.clear(); }
    static bool blocksPlayer(uint32_t flags) {
        return (flags&(solid|playerClip))!=0&&!(flags&noCollision);
    }
    const Table* lookup(uintptr_t shape,size_t& index) {
        uintptr_t entry=0,wrapper=0;
        if(!memory.get(shape+shapeUserData,entry)||!memory.get(entry,wrapper)||!wrapper) return nullptr;
        auto found=tables.find(wrapper);
        if(found==tables.end()) {
            Table table; uint32_t count=0;
            if(memory.get(wrapper+tableList,table.data)&&memory.get(wrapper+tableCount,count)&&count&&count<=100000&&
               table.data&&entry>=table.data&&(entry-table.data)%entrySize==0&&(entry-table.data)/entrySize<count) {
                std::vector<unsigned char> rows(size_t(count)*entrySize);
                if(memory.read(table.data,rows.data(),rows.size())) {
                    table.contents.reserve(count);
                    for(size_t i=0;i<count;++i) {
                        uintptr_t owner=0; uint32_t flags=0;
                        memcpy(&owner,rows.data()+i*entrySize,8);
                        if(owner!=wrapper) { table.contents.clear(); break; }
                        memcpy(&flags,rows.data()+i*entrySize+8,4); table.contents.push_back(flags);
                    }
                }
            }
            found=tables.emplace(wrapper,std::move(table)).first;
        }
        const Table& table=found->second;
        if(entry<table.data||(entry-table.data)%entrySize) return nullptr;
        index=(entry-table.data)/entrySize;
        return index<table.contents.size()?&table:nullptr;
    }
    bool allowsShape(uintptr_t shape) {
        size_t index=0; const Table* table=lookup(shape,index);
        return !table||blocksPlayer(table->contents[index]);
    }
    bool allowsTag(uintptr_t shape,uint16_t tag) {
        size_t index=0; const Table* table=lookup(shape,index);
        return !table||tag==0xffff||tag>=table->contents.size()||blocksPlayer(table->contents[tag]);
    }
};
struct WallTransform {
    float m[12]{1,0,0,0,0,1,0,0,0,0,1,0};
    Vec3 apply(Vec3 p) const { return {m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3],m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7],m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11]}; }
    WallTransform compose(const WallTransform& other) const {
        WallTransform result;
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
            result.m[r*4+c]=c==3?m[r*4+3]:0;
            for(int k=0;k<3;++k) result.m[r*4+c]+=m[r*4+k]*other.m[k*4+c];
        }
        return result;
    }
};
struct LiveWalls {
    WallMemory memory;
    std::vector<Mesh> meshes;
    uint32_t entitiesExamined=0,active=0,unsupported=0,triggers=0;
    uint32_t disabled=0,removed=0,unavailable=0,missingGeometry=0;
    VolumeHistory history;
    void resetHistory() { history.clear(); meshes.clear(); }
    std::unordered_map<uintptr_t,bool> triggerTypes;
    std::unordered_map<uintptr_t,std::string> classNames;
    std::unordered_set<std::string> observedTriggerClasses;
    std::unordered_set<std::string> observedTriggerPaths;
    double milliseconds=0;
    std::string shapeError;
    bool shapeFailure(const char* message) { shapeError=message; return false; }
    static bool blocksPlayer(uint32_t contents,bool disabled) {
        // Playerclip volumes have neither SOLID nor OPAQUE. Hidden/disabled
        // dynamic entities switch their contents to NOCOLLISION (0x40000000).
        return !disabled&&(contents&8)&&!(contents&3)&&!(contents&0x40000000);
    }
    template<class T> bool array(uintptr_t address,std::vector<T>& output,uint32_t limit=100000) const {
        uintptr_t data=0; uint32_t count=0;
        if(!memory.get(address,data)||!memory.get(address+8,count)||count>limit) return false;
        output.resize(count); return !count||memory.read(data,output.data(),size_t(count)*sizeof(T));
    }
    bool compressed(uintptr_t address,const WallTransform& transform,Geometry& geometry,ShapeContentsFilter* contents=nullptr) {
        uintptr_t data=0;
        if(!memory.get(address+0x50,data)) return shapeFailure("compressed data pointer");
        uintptr_t tree=data+0x20;
        float domain[8]{}; bool haveTags=false;
        std::vector<std::array<unsigned char,96>> sections;
        std::vector<std::array<unsigned char,4>> primitives;
        std::vector<uint16_t> sharedIndices;
        std::vector<uint32_t> packed;
        std::vector<uint64_t> shared;
        std::vector<std::array<unsigned char,4>> dataRuns;
        if(!memory.read(tree+16,domain,32)||!array(tree+64,sections,4096)||!array(tree+80,primitives)||
           !array(tree+96,sharedIndices)||!array(tree+112,packed)||!array(tree+128,shared)) return shapeFailure("compressed arrays");
        haveTags=contents&&array(tree+144,dataRuns);
        for(float v:domain) if(!std::isfinite(v)) return shapeFailure("compressed domain nonfinite");
        std::vector<Vec3> sharedPoints;
        for(uint64_t v:shared) {
            uint32_t q[3]{uint32_t(v&0x1fffff),uint32_t((v>>21)&0x1fffff),uint32_t((v>>42)&0x3fffff)};
            float p[3]; for(int k=0;k<3;++k) p[k]=domain[k]+q[k]*(domain[k+4]-domain[k])/float(k==2?0x3fffff:0x1fffff);
            sharedPoints.push_back({p[0],p[1],p[2]});
        }
        for(const auto& section:sections) {
            float codec[6]; uint32_t first=0,sharedDesc=0,primitiveDesc=0,runDesc=0;
            memcpy(codec,section.data()+48,24); memcpy(&first,section.data()+72,4);
            memcpy(&sharedDesc,section.data()+76,4); memcpy(&primitiveDesc,section.data()+80,4); memcpy(&runDesc,section.data()+84,4);
            unsigned packedCount=section[88],sharedCount=section[89];
            if(size_t(first)+packedCount>packed.size()||size_t(sharedDesc>>8)+sharedCount>sharedIndices.size()||
               size_t(primitiveDesc>>8)+(primitiveDesc&255)>primitives.size()) return shapeFailure("compressed section range");
            if(packedCount) for(float v:codec) if(!std::isfinite(v)) return shapeFailure("compressed codec nonfinite");
            std::vector<Vec3> points;
            for(unsigned i=0;i<packedCount;++i) {
                uint32_t v=packed[first+i];
                points.push_back({codec[0]+(v&2047)*codec[3],codec[1]+((v>>11)&2047)*codec[4],codec[2]+((v>>22)&1023)*codec[5]});
            }
            for(unsigned i=0;i<sharedCount;++i) {
                unsigned index=sharedIndices[(sharedDesc>>8)+i];
                if(index>=sharedPoints.size()) return shapeFailure("compressed shared vertex index");
                points.push_back(sharedPoints[index]);
            }
            struct Edge { unsigned a,b,count=0; Vec3 normal{}; bool crease=false; };
            std::unordered_map<uint32_t,Edge> edges;
            std::array<uint16_t,256> tags; tags.fill(0xffff);
            if(haveTags&&size_t(runDesc>>8)+(runDesc&255)<=dataRuns.size()) {
                for(unsigned i=0;i<(runDesc&255);++i) {
                    const auto& run=dataRuns[(runDesc>>8)+i];
                    if(unsigned(run[2])+run[3]>(primitiveDesc&255)) continue;
                    uint16_t tag=uint16_t(run[0])|(uint16_t(run[1])<<8);
                    for(unsigned j=run[2];j<unsigned(run[2])+run[3];++j) tags[j]=tag;
                }
            }
            auto triangle=[&](unsigned a,unsigned b,unsigned c) {
                if(a==b||b==c||a==c) return;
                for(unsigned i:{a,b,c}) geometry.triangles.push_back(transform.apply(points[i]));
                Vec3 u{points[b].x-points[a].x,points[b].y-points[a].y,points[b].z-points[a].z};
                Vec3 v{points[c].x-points[a].x,points[c].y-points[a].y,points[c].z-points[a].z};
                Vec3 normal{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
                for(auto pair:{std::pair<unsigned,unsigned>{a,b},{b,c},{c,a}}) {
                    uint32_t key=(std::min(pair.first,pair.second)<<16)|std::max(pair.first,pair.second);
                    auto [it,inserted]=edges.emplace(key,Edge{pair.first,pair.second}); Edge& edge=it->second;
                    if(inserted) edge.normal=normal;
                    else {
                        double aa=double(edge.normal.x)*edge.normal.x+double(edge.normal.y)*edge.normal.y+double(edge.normal.z)*edge.normal.z;
                        double bb=double(normal.x)*normal.x+double(normal.y)*normal.y+double(normal.z)*normal.z;
                        double dot=double(edge.normal.x)*normal.x+double(edge.normal.y)*normal.y+double(edge.normal.z)*normal.z;
                        edge.crease=edge.crease||aa<1e-18||bb<1e-18||dot*dot<.99999*.99999*aa*bb;
                    }
                    ++edge.count;
                }
            };
            for(unsigned i=0;i<(primitiveDesc&255);++i) {
                const auto& p=primitives[(primitiveDesc>>8)+i];
                if(p==std::array<unsigned char,4>{0xde,0xad,0xde,0xad}) continue;
                if(haveTags&&!contents->allowsTag(address,tags[i])) continue;
                for(unsigned v:p) if(v>=points.size()) return shapeFailure("compressed primitive vertex index");
                triangle(p[0],p[1],p[2]); triangle(p[0],p[2],p[3]);
            }
            for(const auto& item:edges) if(item.second.count!=2||item.second.crease) {
                geometry.lines.push_back(transform.apply(points[item.second.a])); geometry.lines.push_back(transform.apply(points[item.second.b]));
            }
            if(geometry.triangles.size()>60000) return shapeFailure("compressed vertex budget");
        }
        return true;
    }
    bool shape(uintptr_t address,const WallTransform& transform,Geometry& geometry,unsigned depth=0,ShapeContentsFilter* contents=nullptr) {
        if(depth>12||geometry.triangles.size()>60000) return false;
        unsigned char data[112]{};
        if(!memory.read(address,data,sizeof(data))) return false;
        auto u16=[&](size_t at) { uint16_t n; memcpy(&n,data+at,2); return n; };
        if(data[0x22]==13) { // hknpCompoundShape
            uintptr_t rowsAddress=0; uint32_t count=0;
            memcpy(&rowsAddress,data+0x50,8); memcpy(&count,data+0x58,4);
            if(count>4096) return false;
            std::vector<unsigned char> rows(size_t(count)*112);
            if(count&&!memory.read(rowsAddress,rows.data(),rows.size())) return false;
            for(uint32_t i=0;i<count;++i) {
                const auto* row=rows.data()+size_t(i)*112;
                if(row[0x5c]) continue;
                float columns[16],scale[3]; uintptr_t child=0;
                memcpy(columns,row,64); memcpy(scale,row+0x40,12); memcpy(&child,row+0x50,8);
                WallTransform local;
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
                    float v=c<3?columns[c*4+r]*scale[c]:columns[12+r];
                    if(!std::isfinite(v)||fabsf(v)>1000000) return false;
                    local.m[r*4+c]=v;
                }
                if(!shape(child,transform.compose(local),geometry,depth+1,contents)) return false;
            }
            return true;
        }
        if(contents&&!contents->allowsShape(address)) return true;
        if(data[0x22]==9) return compressed(address,transform,geometry,contents);
        // Box shapes (type 6) are also convex polytopes, with the same reflected arrays.
        if(!(u16(0x20)&2)) { shapeError="shape type "+std::to_string(data[0x22]); return false; }
        uint16_t vertices=u16(0x42),faces=u16(0x54),indices=u16(0x58);
        if(vertices<4||vertices>256||faces<4||faces>512||indices>8192) return shapeFailure("convex counts");
        std::vector<std::array<float,4>> points(vertices);
        std::vector<std::array<unsigned char,4>> polygons(faces);
        std::vector<unsigned char> corners(indices);
        if(!memory.read(address+0x42+u16(0x44),points.data(),points.size()*16)||
           !memory.read(address+0x54+u16(0x56),polygons.data(),polygons.size()*4)||
           !memory.read(address+0x58+u16(0x5a),corners.data(),corners.size())) return false;
        for(auto& point:points) for(int k=0;k<3;++k) if(!std::isfinite(point[k])||fabsf(point[k])>100000) return false;
        auto point=[&](unsigned index) { return transform.apply({points[index][0],points[index][1],points[index][2]}); };
        std::unordered_set<uint32_t> edges;
        for(const auto& face:polygons) {
            unsigned first=face[0]|(unsigned(face[1])<<8),count=face[2];
            if(count<3||first+count>corners.size()) return false;
            for(unsigned i=0;i<count;++i) if(corners[first+i]>=vertices) return false;
            for(unsigned i=1;i+1<count;++i) {
                geometry.triangles.push_back(point(corners[first]));
                geometry.triangles.push_back(point(corners[first+i]));
                geometry.triangles.push_back(point(corners[first+i+1]));
            }
            for(unsigned i=0;i<count;++i) {
                unsigned a=corners[first+i],b=corners[first+(i+1)%count];
                uint32_t edge=(std::min(a,b)<<16)|std::max(a,b);
                if(edges.insert(edge).second) { geometry.lines.push_back(point(a)); geometry.lines.push_back(point(b)); }
            }
        }
        return true;
    }
    bool triggerType(uintptr_t vtable,uintptr_t base,const GameProfile& profile) {
        auto found=triggerTypes.find(vtable); if(found!=triggerTypes.end()) return found->second;
        uintptr_t getter=0,info=0,name=0; unsigned char code[8]{}; char text[96]{};
        bool result=false;
        if(vtable>=base&&vtable<base+0x7000000&&memory.get(vtable+16,getter)&&
           getter>=base&&getter<base+profile.typeGetterLimitRva&&memory.read(getter,code,8)&&
           code[0]==0x48&&code[1]==0x8d&&code[2]==0x05&&code[7]==0xc3) {
            int32_t displacement=0; memcpy(&displacement,code+3,4); info=getter+7+displacement;
            if(memory.get(info,name)&&memory.read(name,text,sizeof(text)-1))
                result=strncmp(text,"idTrigger",9)==0||strncmp(text,"idEncounterTrigger",18)==0;
        }
        triggerTypes.emplace(vtable,result); return result;
    }
    void refresh(uintptr_t base,const GameProfile& profile,bool includeTriggers=false,bool includeDisabled=false,bool includeHistory=false,ULONGLONG now=GetTickCount64(),TriggerPathLayout paths=triggerPathLayout()) {
        auto start=std::chrono::steady_clock::now();
        meshes.clear(); entitiesExamined=active=unsupported=triggers=0;
        disabled=removed=unavailable=missingGeometry=0;
        uintptr_t service=0,map=0,list=0; uint32_t count=0;
        if(!memory.get(base+profile.gameSystemRva,service)||!memory.get(service+0x50,map)||
           !memory.get(map+profile.mapEntitiesOffset,list)||!memory.get(map+profile.mapEntitiesOffset+8,count)||count>32768) return;
        struct Entry { uint32_t id,generation; uintptr_t entity,previous,next; };
        std::vector<Entry> entries(count);
        if(count&&!memory.read(list,entries.data(),entries.size()*sizeof(Entry))) return;
        history.begin(map);
        for(size_t entryIndex=0;entryIndex<entries.size();++entryIndex) {
            const auto& entry=entries[entryIndex];
            EntityReference ref{entry.entity,map,list+entryIndex*sizeof(Entry),entry.id,entry.generation,{}};
            history.present(ref);
            ++entitiesExamined;
            uintptr_t vtable=0;
            if(!memory.get(entry.entity,vtable)) { history.missing(ref,EntityStatus::Unavailable,now); continue; }
            bool trigger=includeTriggers&&triggerType(vtable,base,profile);
            if(!trigger&&vtable!=base+profile.wallVtableRva) { history.missing(ref,EntityStatus::Unavailable,now); continue; }
            auto foundClass=classNames.find(vtable);
            if(foundClass==classNames.end()) foundClass=classNames.emplace(vtable,readEntityClass(memory,vtable,base,profile)).first;
            ref.className=foundClass->second;
            if(trigger&&validTriggerClassName(ref.className)) observedTriggerClasses.insert(ref.className);
            std::string namePath,definitionPath;
            if(trigger&&paths.namePointer<0x200000) {
                uintptr_t text=0;
                if(memory.get(entry.entity+paths.namePointer,text)) namePath=triggerNamePath(readEntityText(memory,text,512));
            }
            if(trigger&&paths.definition<0x200000&&paths.definitionNamePointer<0x200000) {
                uintptr_t definition=0,text=0,check=0;
                if(memory.get(entry.entity+paths.definition,definition)&&definition&&memory.get(definition+paths.definitionNamePointer,text)) {
                    definitionPath=triggerNamePath(readEntityText(memory,text,512));
                    if(!memory.get(entry.entity+paths.definition,check)||check!=definition) definitionPath.clear();
                }
            }
            unsigned char entity[0x240]{},physics[0x208]{}; uintptr_t physicsAddress=0;
            if(!memory.read(entry.entity,entity,sizeof(entity))) { history.missing(ref,EntityStatus::Unavailable,now); continue; }
            if(entity[0xa4]) { history.missing(ref,EntityStatus::Removed,now); continue; }
            if(trigger&&(!namePath.empty()||!definitionPath.empty())&&observedTriggerPaths.size()<2048&&validTriggerClassName(ref.className)) {
                Entry named{};
                if(memory.get(ref.slot,named)&&named.id==entry.id&&named.generation==entry.generation&&named.entity==entry.entity) {
                    if(!namePath.empty()) observedTriggerPaths.insert(ref.className+'|'+namePath);
                    if(!definitionPath.empty()&&observedTriggerPaths.size()<2048) observedTriggerPaths.insert(ref.className+'|'+definitionPath);
                }
            }
            memcpy(&physicsAddress,entity+0x1d0,8);
            if(!memory.read(physicsAddress,physics,sizeof(physics))) { history.missing(ref,EntityStatus::Unavailable,now); continue; }
            uint32_t type=0,contents=0,body=0;
            memcpy(&type,physics+0x14,4); memcpy(&contents,physics+0x44,4); memcpy(&body,physics+0x88,4);
            // idDynamicEntity is not unique to invisible walls. An inactive
            // object with only NOCOLLISION flags is identified as a wall only
            // if this exact entity/generation was previously a playerclip.
            bool playerClip=(contents&8)&&!(contents&3);
            if(!trigger&&!playerClip&&!history.knownWall(ref)) continue;
            bool inWorld=true;
            if(type==7) {
                unsigned char present=0;
                if(!memory.get(physicsAddress+0x210,present)) { history.missing(ref,EntityStatus::Unavailable,now); continue; }
                inWorld=present!=0;
            }
            EntityStatus status=volumeCollisionStatus(type,contents,physics[0x4f]!=0,body,inWorld,trigger);
            if(status==EntityStatus::Unavailable) { history.missing(ref,status,now); continue; }
            if(status==EntityStatus::Enabled) { if(trigger) ++triggers; else ++active; }
            auto lostGeometry=[&] {
                ++missingGeometry;
                if(status==EntityStatus::Enabled) ++unsupported;
                history.missing(ref,status==EntityStatus::Disabled?EntityStatus::Disabled:EntityStatus::Unavailable,now);
            };
            uintptr_t wrapper=0,root=0; memcpy(&wrapper,physics+0x1c0,8);
            if(!memory.get(wrapper,root)||!root) { lostGeometry(); continue; }
            auto geometry=std::make_shared<Geometry>(); geometry->kind=trigger?0u:2u;
            if(!shape(root,WallTransform{},*geometry)||geometry->triangles.empty()) { lostGeometry(); continue; }
            // Reject a torn snapshot if an entity was removed or its slot reused.
            uintptr_t stillPhysics=0; uint32_t stillContents=0; unsigned char removedFlag=1;
            if(!memory.get(entry.entity+0x1d0,stillPhysics)||stillPhysics!=physicsAddress||
               !memory.get(physicsAddress+0x44,stillContents)||stillContents!=contents||
               !memory.get(entry.entity+0xa4,removedFlag)||removedFlag) { history.missing(ref,EntityStatus::Unavailable,now); continue; }
            Entry current{};
            if(!memory.get(ref.slot,current)) { history.missing(ref,EntityStatus::Unavailable,now); continue; }
            if(current.id!=entry.id||current.generation!=entry.generation||current.entity!=entry.entity) { history.missing(ref,EntityStatus::Removed,now); continue; }
            Mesh mesh{}; mesh.kind=geometry->kind; mesh.geometry=geometry;
            mesh.entity=ref;
            mesh.triggerNameKnown=!namePath.empty(); mesh.triggerPath=std::move(namePath);
            mesh.triggerDefinitionPath=std::move(definitionPath);
            float axis[9],origin[3]; memcpy(axis,physics+0x8c,36); memcpy(origin,physics+0xb0,12);
            bool finite=true;
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
                float v=c<3?axis[c*3+r]:origin[r]; finite=finite&&std::isfinite(v); mesh.matrix[r*4+c]=v;
            }
            if(!finite) { lostGeometry(); continue; }
            for(int k=0;k<3;++k) { mesh.bounds[k]=FLT_MAX; mesh.bounds[k+3]=-FLT_MAX; }
            for(Vec3 point:geometry->triangles) {
                Vec3 world=mesh.world(point); float xyz[3]{world.x,world.y,world.z};
                for(int k=0;k<3;++k) { mesh.bounds[k]=std::min(mesh.bounds[k],xyz[k]); mesh.bounds[k+3]=std::max(mesh.bounds[k+3],xyz[k]); }
            }
            history.sample(std::move(mesh),status,now);
        }
        history.collect(meshes,includeTriggers,includeDisabled,includeHistory,now);
        for(const auto& mesh:meshes) {
            if(mesh.volumeState==EntityStatus::Disabled) ++disabled;
            else if(mesh.volumeState==EntityStatus::Removed) ++removed;
            else if(mesh.volumeState==EntityStatus::Unavailable) ++unavailable;
        }
        milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
};
static LiveWalls liveWalls;
