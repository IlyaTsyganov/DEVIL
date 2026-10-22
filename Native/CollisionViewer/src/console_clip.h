#pragma once
static float readConsoleFraction(const WallMemory& memory,uintptr_t base) {
    // idConsoleLocal is embedded in the engine singleton. DrawSolidConsole
    // multiplies displayFrac (+6080fc) by the current framebuffer height.
    constexpr uintptr_t consoleRva=0x410bc50;
    uintptr_t vtable=0; float fraction=0; unsigned char forced=0;
    if(!memory.get(base+consoleRva,vtable)||vtable!=base+0x26080b8||
       !memory.get(base+consoleRva+0x6080fc,fraction)||!memory.get(base+consoleRva+0x608108,forced)) return 0;
    if(forced) return 1;
    return std::isfinite(fraction)?std::clamp(fraction,0.0f,1.0f):1.0f;
}
static uint32_t consoleClipTop(float fraction,uint32_t height) {
    if(!std::isfinite(fraction)||fraction>=1) return height;
    if(fraction<=0) return 0;
    // Include the console's lower border; leave the world below it visible.
    return std::min(height,static_cast<uint32_t>(ceilf(fraction*height))+2);
}
