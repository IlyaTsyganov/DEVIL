#pragma once
// Retail 1.1 idGameSystemLocal/idPlayer/idHUD/idMenu reflection layouts.
// Missing/unloading game state is deliberately treated as a menu/loading screen.
static bool gameplayVisible(const WallMemory& memory,uintptr_t base) {
    uintptr_t system=0,map=0,player=0,vtable=0;
    uint32_t state=0;
    if(!memory.get(base+0x33fc468,system)||!memory.get(system+0x44,state)||state!=2||
       !memory.get(system+0x50,map)||!memory.get(map+0x1ad0,player)||
       !memory.get(player,vtable)||vtable!=base+0x2984578) return false;
    unsigned char paused=1,deleted=1;
    if(!memory.get(player+0xa4,deleted)||deleted||!memory.get(player+0x36020+0x3ac,paused)||paused) return false;
    uintptr_t menus=0; uint32_t count=0;
    if(!memory.get(player+0x36020+0x358,menus)||!memory.get(player+0x36020+0x360,count)||count>64) return false;
    for(uint32_t i=0;i<count;++i) {
        uintptr_t menu=0; unsigned char data[0xc8]{};
        if(!memory.get(menus+size_t(i)*8,menu)||!memory.read(menu,data,sizeof(data))) return false;
        if(data[0xb1]) continue; // explicitly hidden menu
        int screen=-1,next=-1,popups=0;
        memcpy(&screen,data+0x90,4); memcpy(&next,data+0x94,4); memcpy(&popups,data+0x80,4);
        if(screen>=0||next>=0||popups>0||data[0xb2]) return false;
    }
    return true;
}
