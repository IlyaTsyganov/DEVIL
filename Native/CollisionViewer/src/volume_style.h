#pragma once
static bool recentVolumeChange(const Mesh& mesh,ULONGLONG now) {
    return mesh.changedAt&&now>=mesh.changedAt&&now-mesh.changedAt<VolumeHistory::pulseMs;
}
static bool volumeShown(const Mesh& mesh,ULONGLONG now) {
    if(mesh.volumeState==EntityStatus::Enabled) return true;
    if(mesh.volumeState==EntityStatus::Disabled) return showDisabledVolumes||(showVolumeHistory&&recentVolumeChange(mesh,now));
    return showVolumeHistory;
}
struct VolumeStyle { std::array<float,4> fill,line; bool filled=true,dashed=false; };
static VolumeStyle volumeStyle(const Mesh& mesh,bool selected,ULONGLONG now) {
    VolumeStyle style;
    style.fill=mesh.kind==0?std::array<float,4>{.15f,.8f,1.f,.09f}:
        (mesh.kind==1?std::array<float,4>{.25f,.95f,.12f,.09f}:std::array<float,4>{1.f,.45f,.05f,.09f});
    style.line=mesh.kind==0?std::array<float,4>{.1f,.85f,1.f,.9f}:
        (mesh.kind==1?std::array<float,4>{.4f,1.f,.2f,.65f}:std::array<float,4>{1.f,.45f,.05f,.9f});
    style.filled=mesh.volumeState==EntityStatus::Enabled&&!mesh.historical;
    if(mesh.volumeState==EntityStatus::Disabled) style.line={.65f,.69f,.8f,.4f};
    else if(mesh.volumeState==EntityStatus::Removed) { style.line={1.f,.35f,.45f,.35f}; style.dashed=true; }
    else if(mesh.volumeState==EntityStatus::Unavailable) { style.line={.7f,.7f,.7f,.3f}; style.dashed=true; }
    if(showVolumeHistory&&recentVolumeChange(mesh,now)) {
        if(mesh.volumeState==EntityStatus::Enabled) style.line={.3f,1.f,.6f,1.f};
        else if(mesh.volumeState==EntityStatus::Disabled) style.line={1.f,.5f,.85f,.9f};
    }
    if(selected) { style.fill={1.f,.85f,.1f,.2f}; style.line={1.f,.85f,.1f,1.f}; }
    return style;
}
