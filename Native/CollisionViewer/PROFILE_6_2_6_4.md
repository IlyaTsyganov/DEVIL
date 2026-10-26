# 6.2, 6.3 and 6.4 profile validation

Validation date: 2026-10-26. These profiles are included in DEViL 0.5.

## Executable identities

Each revision has its own PE metadata, code fingerprint and profile. A directory name alone does not select a layout.

| Revision | PE timestamp | SizeOfImage | SHA-256 |
| --- | --- | --- | --- |
| 6.2 | `0x60f26b2f` | `0x1bb06000` | `3f86b9d3a0a3be5f2aedc8e22576c7787d6b93459d1a0bcaab5991fff34ea4f9` |
| 6.3 | `0x6116d6b5` | `0x1cd60000` | `be22cb6003c12b059ad02fd865208008f2f0489070098312b72f1ebe6b510930` |
| 6.4 | `0x61202c76` | `0x1d77e000` | `b5b27d2b023af52c12cbe0b5dbac47b9eea1072406932a0cff83396782993747` |

## Audited offsets

Renderer globals were traced through the current-context setter; the render-view list offset was checked in the list getter. Player and wall vtables were resolved through each executable's type registration and getter. Constructor/destructor references distinguish the primary vtable from any additional unreferenced method table. Camera and HUD offsets were read from reflected member descriptions. The console profiles use the global console pointer and validate its vtable before reading the shared fraction/forced fields.

| Revision | Render context global | Game system global | Player vtable | Wall vtable | HUD | Console global | Console vtable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 6.2 | `0x6289ea0` | `0x383d068` | `0x2d1f978` | `0x2b6ce68` | `0x46da8` | `0x437aa98` | `0x2969548` |
| 6.3 | `0x6289ea0` | `0x383d068` | `0x2d1f978` | `0x2b6ce58` | `0x46da8` | `0x437aa98` | `0x2969548` |
| 6.4 | `0x6289ea0` | `0x383d068` | `0x2d1f968` | `0x2b6ce48` | `0x46da8` | `0x437aa98` | `0x2969548` |

Offsets below are relative to their owning render context, render view, map or HUD object. The entity/Havok decoder is shared across these profiles.

| Revision | Views | View position | View matrix | Map player | Map entities | HUD menus | HUD paused |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 6.2 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x388` | `+0x3dc` |
| 6.3 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x388` | `+0x3dc` |
| 6.4 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x388` | `+0x3dc` |

## Checks

- Viewer behavior tests passed for all 22 profiles, including 1.0, 1.1, 3.1 and 5.1.
- Fixtures cover translated cameras, profile-specific view allocation sizes, direct/indirect consoles, null/wrong console objects, and trigger getters near the end of each profile's code range.
- Depth tracker tests passed.
- All 19 isolated GPU readback cases passed, including depth rejection, console clipping and separate graphics/presentation queues. The tested DLL matches the local application build.

## Live validation

| Revision | Production readers | Layer draw log | Visual alignment |
| --- | --- | --- | --- |
| 6.2 | Passed | Passed | Confirmed |
| 6.3 | Passed | Passed | Confirmed |
| 6.4 | Passed | Passed | Confirmed |

Live checks open the game with query/read access only. The user starts and stops the game. A newly built Vulkan layer is loaded on a subsequent game start through the selected-executable registration. Other executable variants with the same version label remain unverified.

## Observed live scenes

Counts describe the decoded level at the time of the production probe; active entities vary during gameplay.

| Revision | Level | World instances | Triangles | Triggers | Walls | Unsupported |
| --- | --- | --- | --- | --- | --- | --- |
| 6.2 | `game/dlc2/e5m1_spear/e5m1_spear` | 11995 | 626249 | 782 | 28 | 0 |
| 6.3 | `game/dlc/e4m1_rig/e4m1_rig` | 28225 | 1328498 | 991 | 25 | 0 |
| 6.4 | `game/dlc2/e5m1_spear/e5m1_spear` | 11995 | 626249 | 782 | 28 | 0 |
