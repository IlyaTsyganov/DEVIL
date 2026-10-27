#pragma once

// A graph edge is a copied entity reference, never an inferred script event.
struct GraphField { std::string name,type,ops; uint32_t offset=0,size=0; };
struct GraphType { uint32_t size=0; std::string parent; std::vector<GraphField> fields; bool complete=false; };
using GraphLookup=std::function<const GraphType&(const std::string&)>;
static std::string graphTrim(std::string value) {
    auto first=value.find_first_not_of(" \t"),last=value.find_last_not_of(" \t");
    return first==std::string::npos?std::string{}:value.substr(first,last-first+1);
}
static GraphType readGraphType(const WallMemory& memory,uintptr_t info,const std::string& expected) {
    GraphType result; uintptr_t name=0,parent=0,rows=0;
    if(!info||!memory.get(info,name)||readEntityText(memory,name,256)!=expected||!memory.get(info+16,result.size)||
       !result.size||result.size>0x200000||!memory.get(info+8,parent)||!memory.get(info+32,rows)) return {};
    if(parent) {
        // Root/value types (idStr, idManagedClassPtr, idList...) use a
        // nonnull pointer to "" for superType in retail meathook metadata.
        // Distinguish that valid sentinel from an unreadable parent string.
        unsigned char first=0; if(!memory.get(parent,first)) return {};
        if(first) { result.parent=readEntityText(memory,parent,256); if(result.parent.empty()) return {}; }
    }
    if(!rows) { result.complete=true; return result; }
    for(unsigned i=0;i<2000;++i) {
        uintptr_t row=rows+size_t(i)*72,type=0,ops=0,fieldName=0; int32_t offset=0,size=0;
        if(!memory.get(row+16,fieldName)) return {};
        // Public m_classvar_iterator_is_done checks the field NAME, not type.
        if(!fieldName) { result.complete=true; return result; }
        if(!memory.get(row,type)||!type||!memory.get(row+8,ops)||!memory.get(row+24,offset)||!memory.get(row+28,size)) return {};
        if(offset<0||size<=0||uint64_t(offset)+size>result.size) continue;
        GraphField field{readEntityText(memory,fieldName,256),graphTrim(readEntityText(memory,type,256)),ops?graphTrim(readEntityText(memory,ops,64)):"",
            static_cast<uint32_t>(offset),static_cast<uint32_t>(size)};
        if(field.name.empty()||field.type.empty()) return {};
        result.fields.push_back(std::move(field));
    }
    return {}; // A missing terminator is not a trusted layout.
}
struct GraphNode { EntityReference ref; std::string name,label; uintptr_t vtable=0; };
struct GraphEdge { size_t from=0,to=0; std::string field,evidence; };
struct EntityGraphSnapshot {
    DWORD process=GetCurrentProcessId(); uint64_t captured=0;
    LevelIdentity level; std::string phase="waiting_meathook";
    uint32_t unavailable=0,pending=0,limited=0;
    uint32_t selectedId=0,selectedGeneration=0; bool selected=false;
    std::vector<GraphNode> nodes; std::vector<GraphEdge> edges;
};
static bool graphRequestFresh(const std::filesystem::path& directory,uint64_t now) {
    std::ifstream input(directory/(L"entity-graph-"+std::to_wstring(GetCurrentProcessId())+L".request"));
    uint64_t requested=0; return bool(input>>requested)&&requested<=now+1000&&now<=requested+4000;
}
class EntityGraphReader {
    struct Entry { uint32_t id,generation; uintptr_t entity,previous,next; };
    std::unordered_map<uintptr_t,std::string> classes;
public:
    EntityGraphSnapshot capture(const WallMemory& memory,uintptr_t base,const GameProfile& profile,const LevelIdentity& level,
                                const GraphLookup& lookup,const InspectorPacket& selected={}) {
        EntityGraphSnapshot result; result.captured=encounterEpochMillis(); result.level=level;
        uintptr_t system=0,map=0,list=0; uint32_t count=0;
        if(!level||!memory.get(base+profile.gameSystemRva,system)||!memory.get(system+0x50,map)||map!=level.map) { result.phase="loading"; return result; }
        if(!memory.get(map+profile.mapEntitiesOffset,list)||!memory.get(map+profile.mapEntitiesOffset+8,count)||count>32768) { result.phase="read_error"; return result; }
        std::vector<Entry> entries(count);
        if(count&&!memory.read(list,entries.data(),entries.size()*sizeof(Entry))) { result.phase="read_error"; return result; }
        auto valid=[&](const GraphNode& node) {
            Entry current{}; uintptr_t vtable=0; unsigned char removed=1;
            return memory.get(node.ref.slot,current)&&current.id==node.ref.id&&current.generation==node.ref.generation&&current.entity==node.ref.address&&
                memory.get(node.ref.address+0xa4,removed)&&!removed&&memory.get(node.ref.address,vtable)&&vtable==node.vtable;
        };
        const auto& nameType=lookup("idEntity"); const auto& stringType=lookup("idStr");
        auto find=[](const GraphType& type,const char* name)->const GraphField* {
            for(const auto& field:type.fields) if(field.name==name) return &field; return nullptr;
        };
        const auto* nameField=find(nameType,"name"); const auto* dataField=find(stringType,"data");
        if(!nameType.complete||!stringType.complete) { result.phase="metadata_pending"; return result; }
        if(!nameField||nameField->type.rfind("idStr",0)!=0||!dataField||dataField->size!=8||
           dataField->type!="char"||dataField->ops!="*"||dataField->offset+8>nameField->size) { result.phase="metadata_unavailable"; return result; }
        std::unordered_map<uintptr_t,size_t> byAddress;
        for(size_t i=0;i<entries.size();++i) {
            const auto& entry=entries[i]; if(!entry.entity) continue;
            uintptr_t vtable=0; unsigned char removed=1;
            if(!memory.get(entry.entity,vtable)||!memory.get(entry.entity+0xa4,removed)) { ++result.unavailable; continue; }
            if(removed) continue;
            auto found=classes.find(vtable);
            if(found==classes.end()) {
                auto type=readEntityClass(memory,vtable,base,profile);
                if(type.empty()) { ++result.unavailable; continue; }
                found=classes.emplace(vtable,std::move(type)).first;
            }
            GraphNode node; node.ref={entry.entity,map,list+i*sizeof(Entry),entry.id,entry.generation,found->second}; node.vtable=vtable;
            uintptr_t name=0;
            if(memory.get(entry.entity+nameField->offset+dataField->offset,name)) node.name=readEntityText(memory,name,256);
            if(node.name.empty()) ++result.unavailable;
            node.label=encounterLabel(level.name,node.name);
            if(!valid(node)) { ++result.unavailable; continue; }
            byAddress.emplace(entry.entity,result.nodes.size()); result.nodes.push_back(std::move(node));
        }
        const auto& managedType=lookup("idManagedClass"); const auto* objectNumber=find(managedType,"objectNumber");
        unsigned work=0;
        std::vector<size_t> order; for(size_t i=0;i<result.nodes.size();++i) order.push_back(i);
        auto priority=[&](size_t index) {
            const auto& ref=result.nodes[index].ref;
            if(selected.entity.address==ref.address&&selected.entity.generation==ref.generation) return 0;
            if(ref.className.rfind("idTrigger",0)==0||ref.className.rfind("idEncounter",0)==0) return 1;
            return 2;
        };
        std::stable_sort(order.begin(),order.end(),[&](size_t left,size_t right) { return priority(left)<priority(right); });
        for(size_t source:order) {
            auto add=[&](uintptr_t target,const std::string& path,const char* evidence) {
                auto found=byAddress.find(target);
                if(found==byAddress.end()) { if(target) ++result.unavailable; return; }
                if(found->second==source) return; // Internal self callbacks do not connect different entities.
                if(!valid(result.nodes[source])||!valid(result.nodes[found->second])) { ++result.unavailable; return; }
                if(result.edges.size()>=16384) { ++result.limited; return; }
                result.edges.push_back({source,found->second,path,evidence});
            };
            // Inline structs and reflected idLists only. Never follow arbitrary
            // pointers to objects, evaluate getters, or scan integer bit patterns.
            auto walk=[&](auto&& self,uintptr_t address,const std::string& type,const std::string& ops,uint32_t available,
                          const std::string& path,unsigned depth)->void {
                if(type.rfind("id",0)!=0||type.rfind("idStr",0)==0||type.rfind("idAtomic",0)==0) return;
                if(++work>250000||depth>5||path.size()>480) { ++result.limited; return; }
                if(!ops.empty()) {
                    if(ops!="*"||available!=8||type.rfind("id",0)!=0) return;
                    auto declared=type;
                    bool entity=false;
                    for(unsigned ancestry=0;ancestry<24&&!declared.empty();++ancestry) {
                        if(declared=="idEntity"||declared=="idManagedClass") { entity=true; break; }
                        const auto& schema=lookup(declared); if(!schema.complete) { ++result.unavailable; return; } declared=schema.parent;
                    }
                    if(entity) { uintptr_t target=0; if(memory.get(address,target)) add(target,path,"Entity pointer"); else ++result.unavailable; }
                    return;
                }
                const auto* schema=&lookup(type); if(!schema->complete||!schema->size||schema->size>available) { ++result.unavailable; return; }
                if(type.rfind("idManagedClassPtr",0)==0) {
                    const auto* ptr=find(*schema,"ptr"); const auto* spawn=find(*schema,"spawnId");
                    if(!ptr||ptr->size!=8||ptr->ops!="*"||!spawn||spawn->size!=4||!objectNumber||objectNumber->size!=4) return;
                    uintptr_t target=0; uint32_t token=0,object=0;
                    if(!memory.get(address+ptr->offset,target)||!memory.get(address+spawn->offset,token)) { ++result.unavailable; return; }
                    if(!target) return;
                    // The retail managed pointer's spawnId is NOT objectNumber:
                    // actual owners use distinct numbering domains. Its resolved
                    // ptr is authoritative (as in meathook's look-target reader).
                    // Validate the live map slot and require a stable reference
                    // token/object number across this copied observation.
                    uintptr_t finalTarget=0; uint32_t finalToken=0,finalObject=0;
                    if(byAddress.find(target)==byAddress.end()||!memory.get(target+objectNumber->offset,object)||
                       !memory.get(address+ptr->offset,finalTarget)||finalTarget!=target||!memory.get(address+spawn->offset,finalToken)||finalToken!=token||
                       !memory.get(target+objectNumber->offset,finalObject)||finalObject!=object) { ++result.unavailable; return; }
                    add(target,path,"Managed pointer / map slot checked"); return;
                }
                if(type.rfind("idList <",0)==0||type.rfind("idList<",0)==0) {
                    const auto* data=find(*schema,"list"); const auto* num=find(*schema,"num");
                    if(!data||data->size!=8||(data->ops!="*"&&data->ops!="**")||!num||num->size!=4||num->type!="int") return;
                    uintptr_t elements=0; int32_t length=0;
                    if(!memory.get(address+data->offset,elements)||!memory.get(address+num->offset,length)) { ++result.unavailable; return; }
                    if(length<0||length>4096||(!elements&&length)) { ++result.unavailable; return; }
                    uint32_t stride=8; std::string elementType=graphTrim(data->type),elementOps;
                    if(data->ops=="**") elementOps="*";
                    else if(!elementType.empty()&&elementType.back()=='*') { elementType=graphTrim(elementType.substr(0,elementType.size()-1)); elementOps="*"; }
                    else { const auto& element=lookup(elementType); if(!element.complete) { ++result.unavailable; return; } stride=element.size; }
                    if(!stride||stride>0x200000) return;
                    int32_t bounded=std::min(length,256); if(bounded<length) ++result.limited;
                    size_t edgeStart=result.edges.size();
                    for(int32_t i=0;i<bounded;++i) {
                        if(work>=250000) { ++result.limited; break; }
                        self(self,elements+size_t(i)*stride,elementType,elementOps,stride,path+"["+std::to_string(i)+"]",depth+1);
                    }
                    uintptr_t finalElements=0; int32_t finalLength=0;
                    if(!memory.get(address+data->offset,finalElements)||elements!=finalElements||!memory.get(address+num->offset,finalLength)||length!=finalLength) {
                        result.edges.resize(edgeStart); ++result.unavailable;
                    }
                    return;
                }
                for(unsigned ancestry=0;ancestry<24;++ancestry) {
                    for(const auto& field:schema->fields) {
                        if(work>=250000) { ++result.limited; break; }
                        self(self,address+field.offset,field.type,field.ops,field.size,path.empty()?field.name:path+"."+field.name,depth+1);
                    }
                    if(schema->parent.empty()) break;
                    schema=&lookup(schema->parent);
                    if(!schema->complete||schema->size>available) { ++result.unavailable; break; }
                    if(ancestry==23) ++result.limited;
                }
            };
            const auto& type=lookup(result.nodes[source].ref.className);
            if(type.complete) walk(walk,result.nodes[source].ref.address,result.nodes[source].ref.className,"",type.size,"",0);
            else ++result.unavailable;
            if(work>=250000) { ++result.limited; break; }
        }
        // Discard edges whose source or target disappeared during this scan.
        std::vector<bool> alive; for(const auto& node:result.nodes) alive.push_back(valid(node));
        result.edges.erase(std::remove_if(result.edges.begin(),result.edges.end(),[&](const GraphEdge& edge) { return !alive[edge.from]||!alive[edge.to]; }),result.edges.end());
        if(selected.enabled&&selected.updated<=GetTickCount64()&&GetTickCount64()-selected.updated<1500&&selected.level==level) {
            auto found=byAddress.find(selected.entity.address);
            if(found!=byAddress.end()) {
                const auto& node=result.nodes[found->second];
                if(alive[found->second]&&node.ref.id==selected.entity.id&&node.ref.generation==selected.entity.generation) {
                    result.selected=true; result.selectedId=node.ref.id; result.selectedGeneration=node.ref.generation;
                }
            }
        }
        std::vector<size_t> remap(result.nodes.size()); std::vector<GraphNode> retained;
        for(size_t i=0;i<result.nodes.size();++i) if(alive[i]) { remap[i]=retained.size(); retained.push_back(std::move(result.nodes[i])); } else ++result.unavailable;
        for(auto& edge:result.edges) { edge.from=remap[edge.from]; edge.to=remap[edge.to]; }
        result.nodes=std::move(retained);
        uintptr_t finalMap=0,finalList=0; uint32_t finalCount=0;
        result.phase="live";
        if(!memory.get(system+0x50,finalMap)||finalMap!=map||!memory.get(map+profile.mapEntitiesOffset,finalList)||finalList!=list||
           !memory.get(map+profile.mapEntitiesOffset+8,finalCount)||finalCount!=count||readLevelIdentity(memory,base,profile)!=level) {
            result.nodes.clear(); result.edges.clear(); result.selected=false; result.phase="loading";
        }
        return result;
    }
};
static bool writeEntityGraph(const EntityGraphSnapshot& snapshot,const std::filesystem::path& directory) {
    if(directory.empty()) return false;
    auto clean=[](std::string text) { for(char& c:text) if(static_cast<unsigned char>(c)<32||c==127) c=' '; return text; };
    try {
        std::filesystem::create_directories(directory);
        auto path=directory/(L"entity-graph-"+std::to_wstring(snapshot.process)+L".tsv"),temporary=path; temporary+=L".tmp";
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc); out.imbue(std::locale::classic());
        out<<"#DEVIL_GRAPH_V1\t"<<snapshot.process<<'\t'<<snapshot.captured<<'\t'<<snapshot.phase<<'\t'<<clean(snapshot.level.name)<<'\t'
           <<snapshot.level.map<<':'<<snapshot.level.world<<'\t'<<snapshot.unavailable<<'\t'<<snapshot.pending<<'\t'<<snapshot.limited<<'\t'
           <<(snapshot.selected?std::to_string(snapshot.selectedId)+":"+std::to_string(snapshot.selectedGeneration):"")<<'\n';
        for(const auto& node:snapshot.nodes) out<<"N\t"<<node.ref.id<<':'<<node.ref.generation<<'\t'<<clean(node.name)<<'\t'<<clean(node.ref.className)<<'\t'<<clean(node.label)<<'\n';
        for(const auto& edge:snapshot.edges) out<<"E\t"<<snapshot.nodes[edge.from].ref.id<<':'<<snapshot.nodes[edge.from].ref.generation<<'\t'
           <<snapshot.nodes[edge.to].ref.id<<':'<<snapshot.nodes[edge.to].ref.generation<<'\t'<<clean(edge.field)<<'\t'<<clean(edge.evidence)<<'\n';
        out.close(); if(!out) return false;
        return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)!=0;
    } catch(...) { return false; }
}
class EntityGraphBridge {
    std::mutex exchange;
    std::unordered_map<std::string,std::shared_ptr<const GraphType>> schemas;
    std::vector<std::string> requested;
    const MeathookHudApi* api=nullptr; const GameProfile* profile=nullptr;
    bool started=false,registered=false; ULONGLONG retryAt=0;
    std::atomic<bool> callbackReady{false};
    static void callback(void* self) { static_cast<EntityGraphBridge*>(self)->onFrame(); }
    void onFrame() {
        if(!labAppAvailable()) return;
        callbackReady=true;
        // Only this callback invokes meathook. At most four copied layouts per frame.
        for(unsigned i=0;i<4;++i) {
            std::string type;
            { std::lock_guard<std::mutex> lock(exchange); if(requested.empty()) return; type=std::move(requested.back()); requested.pop_back(); }
            auto schema=std::make_shared<const GraphType>(readGraphType(WallMemory{},reinterpret_cast<uintptr_t>(api->findClass(type.c_str())),type));
            std::lock_guard<std::mutex> lock(exchange); schemas[type]=std::move(schema);
        }
    }
    void run() {
        EntityGraphReader reader; const auto directory=researchDirectory(); auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        unsigned attempts=0;
        for(;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if(!labAppAvailable()||!graphRequestFresh(directory,encounterEpochMillis())) continue;
            EntityGraphSnapshot snapshot; snapshot.captured=encounterEpochMillis();
            try {
                if(!profile) snapshot.phase="unsupported_profile";
                else if(!callbackReady.load()) snapshot.phase="waiting_meathook";
                else {
                    std::unordered_map<std::string,std::shared_ptr<const GraphType>> layouts;
                    { std::lock_guard<std::mutex> lock(exchange);
                        if(++attempts%10==0) for(auto it=schemas.begin();it!=schemas.end();) {
                            if(!it->second->complete) it=schemas.erase(it); else ++it;
                        }
                        layouts=schemas;
                    }
                    std::vector<std::string> missing;
                    auto lookup=[&](const std::string& type)->const GraphType& {
                        auto found=layouts.find(type); if(found!=layouts.end()) return *found->second;
                        if(std::find(missing.begin(),missing.end(),type)==missing.end()&&missing.size()<2048) missing.push_back(type);
                        static const GraphType empty; return empty;
                    };
                    auto level=readLevelIdentity(WallMemory{},base,*profile);
                    snapshot=reader.capture(WallMemory{},base,*profile,level,lookup,inspectorHud().selection());
                    snapshot.pending=static_cast<uint32_t>(missing.size());
                    if(snapshot.phase=="metadata_pending"&&!snapshot.pending) snapshot.phase="metadata_unavailable";
                    { std::lock_guard<std::mutex> lock(exchange);
                        for(const auto& type:missing) if(schemas.find(type)==schemas.end()&&std::find(requested.begin(),requested.end(),type)==requested.end()) requested.push_back(type);
                    }
                }
            } catch(...) { snapshot.phase="read_error"; snapshot.nodes.clear(); snapshot.edges.clear(); }
            writeEntityGraph(snapshot,directory);
        }
    }
public:
    void connect() {
        if(!labAppAvailable()) return;
        HMODULE pinned=nullptr;
        if(!started) {
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&callback),&pinned)) return;
            profile=currentGameProfile(); started=true; std::thread([this] { run(); }).detach();
        }
        if(registered||GetTickCount64()<retryAt) return; retryAt=GetTickCount64()+2000;
        HMODULE module=GetModuleHandleW(L"XINPUT1_3.dll"); if(!module) return;
        auto getter=reinterpret_cast<const MeathookHudApi* (*)()>(GetProcAddress(module,"mh_api_get")); if(!getter) return;
        const auto* candidate=getter(); MeathookHudApi copy{};
        if(!candidate||!WallMemory{}.get(reinterpret_cast<uintptr_t>(candidate),copy)||!copy.findClass||!copy.registerPreFrame) return;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(getter),&pinned)) return;
        api=candidate; registered=true; api->registerPreFrame(callback,this);
    }
};
static EntityGraphBridge& entityGraph() { static auto* graph=new EntityGraphBridge; return *graph; }
