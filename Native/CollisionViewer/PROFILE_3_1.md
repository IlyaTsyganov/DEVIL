# DOOM Eternal 3.1 validation

Validated on 2026-10-26.

## Executable identity

- PE timestamp: `0x5f90c129`.
- PE SizeOfImage: `0x1e0c1000`.
- SHA-256: `0a3c47138c785f8d6cecfaa2b0268b4233bb31a1624dee3d4ba2b317f4060afd`.
- Code fingerprint RVA: `0x316270`, using the existing profile identification logic.

## Audited layout

| Field | 3.1 |
| --- | --- |
| Render manager global RVA | `0x61f1690` |
| Game system global RVA | `0x35e32b8` |
| Player vtable RVA | `0x2b516f0` |
| Dynamic wall vtable RVA | `0x29b0308` |
| Player HUD offset | `0x374e8` |
| Console object / vtable RVA | `0x4651850` / `0x27b7ef8` |
| Render views list / count | `0x5a7198` / `0x5a71a0` |
| View position / matrix | `0x20a44` / `0x21410` |
| Map player / entity list | `0x1af8` / `0x61cd8` |
| HUD menus / pause flag | `0x370` / `0x3c4` |

HUD and camera members were checked against the game's reflected fields. Global service addresses were traced through initialization code. The render manager global was checked against live views, the camera matrix/position and game instructions referencing that global. The player and dynamic wall vtables were checked through the game's type getters. Entity and Havok layouts continue to use the shared decoder; there are no map-specific offsets or geometry files.

## Read-only integration result

The game was running on UAC Atlantica Facility (`game/dlc/e4m1_rig/e4m1_rig`). The production `RetailProfileProbe.exe` reported:

```text
Profile: DOOM Eternal 3.1
Camera valid: 1
Gameplay visible: 1
Console fraction: 0.000
Level: game/dlc/e4m1_rig/e4m1_rig
Entities: examined=11045 triggers=986 walls=25 unsupported=0 meshes=1011
World: instances=28103 unique=15126 triangles=1393840 nonBlocking=322041 unsupported=0
```

These counts describe decoded objects throughout the level. Counts for active entities vary with gameplay. The probe only opens the game with query/read permissions; it neither injects a layer nor modifies game memory.

## Validation status

- `ViewerBehaviorTests.exe`: passed for 1.0, 1.1, 3.1 and 5.1. The camera fixtures also check a nonzero camera position to detect using the wrong position member after a layout change.
- `DepthTrackerTests.exe`: passed.
- All 19 isolated GPU readback tests passed, including depth rejection, console clipping and separate graphics/presentation queues.
- The Windows application and native layer built successfully. The application supports all four profiles.

## In-game run

After restarting 3.1 with the registered layer, the runtime log confirmed selection of the 3.1 profile, initialization of the Vulkan renderer and construction of the current UAC Atlantica Facility scene. It recorded ongoing collision and trigger draws as the camera moved (more than 6,600 rendered frames), zero unsupported forms, and hiding the overlay when entering the menu.

Live scene decoding and Vulkan draw submission are verified. Visual alignment has not been separately confirmed for this profile; draw submission alone does not establish the appearance of the final image.

Other executables carrying the 3.1 version label and other levels have not been individually verified. The same current-map discovery supports DLC paths without maintaining a filename list.
