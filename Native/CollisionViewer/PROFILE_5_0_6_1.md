# 5.0, 6.0 and 6.1 profile validation

Validation date: 2026-10-26. These profiles are included in DEViL 0.5.

## Executable identities

Each revision has its own PE metadata, code fingerprint and profile. A directory name alone does not select a layout.

| Revision | PE timestamp | SizeOfImage | SHA-256 |
| --- | --- | --- | --- |
| 5.0 | `0x603896f3` | `0x1c5bf000` | `9848ea9bcba71687fff215e3bfe86457eccd91c5965da1fad51e1ba4c1f85b39` |
| 6.0 | `0x60c98dd6` | `0x1cb22000` | `7abfc3fda951723636863d5722f266d31bb3236488c1e97df1f4301129d39d16` |
| 6.1 | `0x60dff532` | `0x1bc55000` | `1f972ea4cc13a3bfcba01577ca8fd4f3c35c6aec01cfc9468135dd8d19fe2031` |

## Audited offsets

Renderer globals were traced through the current-context setter; the render-view list offset was checked in the list getter. Player and wall vtables were resolved through each executable's type registration and getter. Constructor/destructor references distinguish the primary vtable from any additional unreferenced method table. Camera and HUD offsets were read from reflected member descriptions. The console profiles use the global console pointer and validate its vtable before reading the shared fraction/forced fields.

| Revision | Render context global | Game system global | Player vtable | Wall vtable | HUD | Console global | Console vtable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 5.0 | `0x5ea7cd0` | `0x367c8c0` | `0x2bc5628` | `0x2a14ec8` | `0x46cd8` | `0x4275c18` | `0x2811058` |
| 6.0 | `0x6287920` | `0x383b068` | `0x2d1ee78` | `0x2b6c998` | `0x46da8` | `0x4378518` | `0x2969548` |
| 6.1 | `0x6287920` | `0x383b068` | `0x2d1ee78` | `0x2b6c998` | `0x46da8` | `0x4378518` | `0x2969548` |

Offsets below are relative to their owning render context, render view, map or HUD object. The entity/Havok decoder is shared across these profiles.

| Revision | Views | View position | View matrix | Map player | Map entities | HUD menus | HUD paused |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 5.0 | `+0x5a7198` | `+0x20a54` | `+0x21430` | `+0x1af8` | `+0x61cd8` | `+0x388` | `+0x3dc` |
| 6.0 | `+0x5a71a8` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x388` | `+0x3dc` |
| 6.1 | `+0x5a71a8` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x388` | `+0x3dc` |

5.0 has a smaller reflected surface-velocity map and different subsequent idHavokPhysics fields than the 6.x revisions. The viewer does not read those fields. Its world-collision compound pointer at `+0x210b0`, entity physics, body and shape members retain the audited offsets. 6.0/6.1 use a different render-view list offset from 6.2 despite sharing the newer view-position/matrix offsets.

## Checks

- Viewer behavior tests passed for all 22 profiles, including 1.0, 1.1, 3.1 and 5.1.
- Fixtures cover translated cameras, profile-specific view allocation sizes, direct/indirect consoles, null/wrong console objects, and trigger getters near the end of each profile's code range.
- Depth tracker tests passed.
- All 19 isolated GPU readback cases passed, including depth rejection, console clipping and separate graphics/presentation queues. The tested DLL matches the local application build.

## Live validation

| Revision | Production readers | Layer draw log | Visual alignment |
| --- | --- | --- | --- |
| 5.0 | Passed | Passed | Confirmed |
| 6.0 | Passed | Passed | Confirmed |
| 6.1 | Passed | Passed | Confirmed |

Live checks open the game with query/read access only. The user starts and stops the game. A newly built Vulkan layer is loaded on a subsequent game start through the selected-executable registration. Other executable variants with the same version label remain unverified.

## Observed live scenes

Counts describe the decoded level at the time of the production probe; active entities vary during gameplay.

| Revision | Level | World instances | Triangles | Triggers | Walls | Unsupported |
| --- | --- | --- | --- | --- | --- | --- |
| 5.0 | `game/dlc2/e5m1_spear/e5m1_spear` | 12005 | 622281 | 783 | 28 | 0 |
| 6.0 | `game/dlc2/e5m1_spear/e5m1_spear` | 11995 | 626249 | 782 | 28 | 0 |
| 6.1 | `game/dlc/e4m1_rig/e4m1_rig` | 28225 | 1328498 | 990 | 25 | 0 |
