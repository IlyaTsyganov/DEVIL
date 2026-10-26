# 4.0 and 4.1 profile validation

Validation date: 2026-10-26. These profiles are included in DEViL 0.5.

## Executable identities

Each revision has its own PE metadata, code fingerprint and profile. A directory name alone does not select a layout.

| Revision | PE timestamp | SizeOfImage | SHA-256 |
| --- | --- | --- | --- |
| 4.0 | `0x5fb68f01` | `0x1c7e9000` | `7bea7ebd1e42dedf4a48c27c86c8a5d78a4e5d71957c6c42013094d6fb287038` |
| 4.1 | `0x5fc9da94` | `0x1c2eb000` | `911482009da6d0b78e8445402e85a6bdf4b19ac3a54319d5c67e4e1b2eaad874` |

## Audited offsets

Renderer globals were traced through the current-context setter; the render-view list offset was checked in the list getter. Player and wall vtables were resolved through each executable's type registration and getter. Constructor/destructor references distinguish the primary vtable from any additional unreferenced method table. Camera and HUD offsets were read from reflected member descriptions. The console profiles use the global console pointer and validate its vtable before reading the shared fraction/forced fields.

| Revision | Render context global | Game system global | Player vtable | Wall vtable | HUD | Console global | Console vtable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 4.0 | `0x61aefb0` | `0x3575148` | `0x2aecde8` | `0x2948700` | `0x373e0` | `0x45db398` | `0x274f3f8` |
| 4.1 | `0x61aefb0` | `0x3575148` | `0x2aecee0` | `0x29487f0` | `0x373e0` | `0x45db398` | `0x274f3f8` |

Offsets below are relative to their owning render context, render view, map or HUD object. The entity/Havok decoder is shared across these profiles.

| Revision | Views | View position | View matrix | Map player | Map entities | HUD menus | HUD paused |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 4.0 | `+0x5a7198` | `+0x20a44` | `+0x21410` | `+0x1af8` | `+0x61cd8` | `+0x370` | `+0x3c4` |
| 4.1 | `+0x5a7198` | `+0x20a44` | `+0x21410` | `+0x1af8` | `+0x61cd8` | `+0x370` | `+0x3c4` |

4.0/4.1 retain the earlier view-position/matrix and HUD member offsets, but require separately audited renderer, game-system, player and wall addresses. Their reflected surface-velocity map and subsequent idHavokPhysics fields differ from later 6.x layouts. Those later members are not read by the viewer; the world-collision compound pointer at `+0x210b0`, entity physics, body and shape members retain the audited offsets.

## Checks

- Viewer behavior tests passed for all 22 profiles, including 1.0, 1.1, 3.1 and 5.1.
- Fixtures cover translated cameras, profile-specific view allocation sizes, direct/indirect consoles, null/wrong console objects, and trigger getters near the end of each profile's code range.
- Depth tracker tests passed.
- All 19 isolated GPU readback cases passed, including depth rejection, console clipping and separate graphics/presentation queues. The tested DLL matches the local application build.

## Live validation

| Revision | Production readers | Layer draw log | Visual alignment |
| --- | --- | --- | --- |
| 4.0 | Passed | Passed | Confirmed |
| 4.1 | Passed | Passed | Confirmed |

Live checks open the game with query/read access only. The user starts and stops the game. A newly built Vulkan layer is loaded on a subsequent game start through the selected-executable registration. Other executable variants with the same version label remain unverified.

## Observed live scenes

Counts describe the decoded level at the time of the production probe; active entities vary during gameplay.

| Revision | Level | World instances | Triangles | Triggers | Walls | Unsupported |
| --- | --- | --- | --- | --- | --- | --- |
| 4.0 | `game/sp/e1m1_intro/e1m1_intro` | 31353 | 1800678 | 412 | 16 | 0 |
| 4.1 | `game/dlc/e4m1_rig/e4m1_rig` | 28102 | 1393828 | 988 | 25 | 0 |
