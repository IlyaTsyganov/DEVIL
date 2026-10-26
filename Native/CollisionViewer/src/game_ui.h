#pragma once
// Audited idGameSystemLocal/idPlayer/idHUD/idMenu layouts, selected by profile.
// Missing/unloading game state is deliberately treated as a menu/loading screen.
static bool gameplayVisible(const WallMemory& memory,uintptr_t base,const GameProfile& profile) {
    uintptr_t system=0,map=0,player=0,vtable=0;
    uint32_t state=0;
    if(!memory.get(base+profile.gameSystemRva,system)||!memory.get(system+0x44,state)||state!=2||
       !memory.get(system+0x50,map)||!memory.get(map+profile.mapPlayerOffset,player)||
       !memory.get(player,vtable)||vtable!=base+profile.playerVtableRva) return false;
    unsigned char paused=1,deleted=1;
    if(!memory.get(player+0xa4,deleted)||deleted||!memory.get(player+profile.playerHudOffset+profile.hudPausedOffset,paused)||paused) return false;
    uintptr_t menus=0; uint32_t count=0;
    if(!memory.get(player+profile.playerHudOffset+profile.hudMenusOffset,menus)||!memory.get(player+profile.playerHudOffset+profile.hudMenusOffset+8,count)||count>64) return false;
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
