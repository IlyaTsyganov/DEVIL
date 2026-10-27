# Disabled volumes and recent changes

Included in DEViL 0.7 alongside the volume inspector.

## Controls

- **Show disabled volumes:** show inactive triggers and identified playerclip walls as pale outlines. They are never filled, even in Filled mode.
- **Show recent changes:** highlight an observed activation in green and a deactivation in pink for two seconds. Initial discovery is not treated as activation. Removed objects use red dashed outlines; temporarily unreadable ones use grey dashed outlines. These lost-object snapshots expire after at most 15 seconds.

The controls respect the Triggers / Invisible walls group toggles and view distance. Selection still uses the exact displayed triangle geometry; yellow selection takes priority over state colours. All options update live after the new viewer DLL is loaded.

The inspector displays `Geometry: current sample` or `Geometry: last snapshot` with age and sampled state. A snapshot is a previously decoded shape, not evidence that its old collision is still present. For removed objects, the inspector avoids calling the entity API with an obsolete pointer; the current name can therefore be unavailable while the old class/ID remain visible. Native text still requires meathook; volume rendering does not.

## Identification and limits

History keys include map, entity address, map ID and generation. A slot reused for a different generation produces a separate old snapshot and new object. Map changes clear history. A successful scan of the entity table establishes removal; a failed object/physics read is classified as unavailable instead of being guessed to be removal or deactivation.

Disabled trigger geometry is decoded when the object still owns a valid shape, even if it has no active physics body. If the engine released that shape, a previously observed shape of the same entity can be retained with a snapshot label. A positively identified disabled object can retain its last geometry while it remains in the entity table; the 15-second limit applies to removed/unavailable records.

The wall vtable is shared with ordinary dynamic entities. A wall needs explicit playerclip contents or prior identification of that exact entity/generation. Newly encountered NOCOLLISION-only dynamic props are not assumed to be invisible walls. A volume that was never observed with usable geometry cannot be reconstructed from nothing. This does not expose every possible inactive object on a cold level load.

## Checks

Checked locally on 2026-10-27:

- All 22 executable-profile fixtures: inactive shapes, released shapes, reactivation, deletion vs read errors, generation reuse, timed expiry, history reset, visibility toggles, outline-only inactive rendering and dashed snapshots.
- Inspector fixtures: stale references never reach the entity API, and historical geometry is labelled in the native HUD.
- All 19 isolated Vulkan GPU cases passed, including depth occlusion, reversed depth, console clipping and split-queue presentation; no console leaks were detected.
- Companion launch/version tests and window layout check, including no overlapping controls or clipped legend.
- Read-only production probe against the user's running 6.66 Rev 3 on `game/dlc/e4m1_rig/e4m1_rig`: 991 active triggers, 25 active walls, 23 additional disabled volumes; no missing geometry or unsupported active shapes in that sample. This probe did not inject the new viewer or change game state.

Visual in-game acceptance of the new colours and history still needs loading the updated DLL on the next complete game launch. The existing process cannot switch an already-loaded viewer DLL in place. DEViL's **Update pending** status detects this mismatch. Meathook does not need to be reinstalled or reloaded through DESRU.
