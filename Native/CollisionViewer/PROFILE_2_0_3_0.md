# 2.0, 2.1 and 3.0 profile validation

Validation date: 2026-10-26. These profiles are included in DEViL 0.5.

## Executable identities

Each revision has its own PE metadata, code fingerprint and profile. A directory name alone does not select a layout.

| Revision | PE timestamp | SizeOfImage | SHA-256 |
| --- | --- | --- | --- |
| 2.0 | `0x5eeb0267` | `0x1d396000` | `d5e55036101b41f1ce5f1ff6c201f70a21c7d84a6f151a4f4799f8eb8564cac4` |
| 2.1 | `0x5f203d59` | `0x1e1ef000` | `7e66b8a1527689c0e6c61d277d59716cc3273e6bf21f98e8bd6f5d0b245407dd` |
| 3.0 | `0x5f8065da` | `0x1c587000` | `bb5b6942b0cfe237486e802cce536de7f3d8a8219d7d3caf7f5f97ec590f0a6a` |

## Audited offsets

Renderer globals were traced through the current-context setter; the render-view list offset was checked in the list getter. Player and wall vtables were resolved through each executable's type registration and getter. Constructor/destructor references distinguish the primary vtable from any additional unreferenced method table. Camera and HUD offsets were read from reflected member descriptions. The console profiles use the global console pointer and validate its vtable before reading the shared fraction/forced fields.

| Revision | Render context global | Game system global | Player vtable | Wall vtable | HUD | Console global | Console vtable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2.0 | `0x5f55ae0` | `0x3465700` | `0x29dd7f0` | `0x2848358` | `0x36e18` | `0x44848a0` | `0x2658d48` |
| 2.1 | `0x5f589e0` | `0x3468708` | `0x29dfe78` | `0x2849948` | `0x36a30` | `0x4487920` | `0x2659e08` |
| 3.0 | `0x61f1610` | `0x35e32b8` | `0x2b516e0` | `0x29b02f8` | `0x374e8` | `0x464b998` | `0x27b7ef8` |

Offsets below are relative to their owning render context, render view, map or HUD object. The entity/Havok decoder is shared across these profiles.

| Revision | Views | View position | View matrix | Map player | Map entities | HUD menus | HUD paused |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2.0 | `+0x5a7188` | `+0x20a28` | `+0x213f0` | `+0x1ad8` | `+0x61cb8` | `+0x358` | `+0x3ac` |
| 2.1 | `+0x5a7188` | `+0x20a28` | `+0x213f0` | `+0x1ad8` | `+0x61cb8` | `+0x358` | `+0x3ac` |
| 3.0 | `+0x5a7198` | `+0x20a44` | `+0x21410` | `+0x1af8` | `+0x61cd8` | `+0x370` | `+0x3c4` |

2.0/2.1 use the map player pointer at `+0x1ad8` and entity-list data/count at `+0x61cb8`/`+0x61cc0`; 3.0 uses `+0x1af8` and `+0x61cd8`/`+0x61ce0`. Live 2.x memory places header fields eight bytes before the player/list pointers. These headers must not be treated as pointers; the production-reader checks below verify the actual data positions. The physics members read by the viewer retain their audited offsets; unrelated later Havok fields differ from 6.x.

## Checks

- Viewer behavior tests passed for all 22 profiles, including 1.0, 1.1, 3.1 and 5.1.
- Fixtures cover translated cameras, profile-specific view allocation sizes, direct/indirect consoles, null/wrong console objects, and trigger getters near the end of each profile's code range.
- Depth tracker tests passed.
- All 19 isolated GPU readback cases passed, including depth rejection, console clipping and separate graphics/presentation queues. The tested DLL matches the local application build.
- The 2.x map fixture reproduces the observed header fields and fixes player/list data positions independently of profile values, so the eight-byte offset regression cannot pass by moving the fixture with the profile.

## Live validation

| Revision | Production readers | Layer draw log | Visual alignment |
| --- | --- | --- | --- |
| 2.0 | Passed | Passed | Confirmed |
| 2.1 | Passed | Passed | Confirmed |
| 3.0 | Passed | Passed | Confirmed |

Live checks open the game with query/read access only. The user starts and stops the game. A newly built Vulkan layer is loaded on a subsequent game start through the selected-executable registration. Other executable variants with the same version label remain unverified.

## Observed live scenes

Counts describe the decoded level at the time of the production probe; active entities vary during gameplay.

| Revision | Level | World instances | Triangles | Triggers | Walls | Unsupported |
| --- | --- | --- | --- | --- | --- | --- |
| 2.0 | `game/sp/e1m1_intro/e1m1_intro` | 31353 | 1800678 | 408 | 16 | 0 |
| 2.1 | `game/sp/e1m1_intro/e1m1_intro` | 31353 | 1800678 | 412 | 16 | 0 |
| 3.0 | `game/dlc/e4m1_rig/e4m1_rig` | 28103 | 1393840 | 989 | 25 | 0 |
