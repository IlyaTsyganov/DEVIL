#include "../src/vulkan_layer.cpp"
#include <tlhelp32.h>

// Read-only integration probe: runs the production readers/decoder against an
// existing game without injecting a DLL, writing memory or changing game state.
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) { fprintf(stderr,"Usage: RetailProfileProbe <game-pid>\n"); return 2; }
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
        if(!cameraValid||!level) throw std::runtime_error("Enter a loaded game level before decoding");
        LiveWalls scanner; scanner.memory=memory; scanner.refresh(base,*profile,true);
        printf("Entities: examined=%u triggers=%u walls=%u unsupported=%u meshes=%zu\n",scanner.entitiesExamined,scanner.triggers,scanner.active,scanner.unsupported,scanner.meshes.size());
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
