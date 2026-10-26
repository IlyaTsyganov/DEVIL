# 6.66 profile validation

Validation date: 2026-10-26. These profiles are included in DEViL 0.5.

## Executable identities

Each revision has its own PE metadata, code fingerprint and profile. A directory name alone does not select a layout.

| Revision | PE timestamp | SizeOfImage | SHA-256 |
| --- | --- | --- | --- |
| 6.66 | `0x61692c83` | `0x1e4cd000` | `4a6c7962562462e83b99d490412c23a6054972df15ff3f09f05c2193cbe2874f` |
| 6.66 Rev 1 | `0x618dfc01` | `0x1c835000` | `cb1f3a25c34ddee1878fb206c0ddb7508f6d0ddad4db272bd58bb8cacf93075f` |
| 6.66 Rev 1.1 | `0x61d7704e` | `0x1c58a000` | `c7cf6702b5602a25f0969f748b72849e674b4f0c4cb898b102b6169d5fc4d26d` |
| 6.66 Rev 2 | `0x6256614c` | `0x1e69d000` | `84d6d3d01855fa5900f7ff8e452a14d9316ebc459bafdc4c4480831b537ff375` |
| 6.66 Rev 2.2 | `0x64cc8a9b` | `0x7421000` | `7352dc4da0b107939c7d4d1e2c42ea087c566c26e79f8e01270678a0c61f6b39` |
| 6.66 Rev 3 | `0x669f0dcf` | `0x744d000` | `005f887a04450ab3f34535be658a711a8fcd3fbb602d07bec19b54ecbf53efa8` |
| 6.66 Rev 3.2 | `0x6a7b9b8c` | `0x7431000` | `69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11` |

## Audited offsets

Renderer globals were traced through the current-context setter; the render-view list offset was checked in the list getter. Player and wall vtables were resolved through each executable's type registration and getter. Constructor/destructor references distinguish the primary vtable from any additional unreferenced method table. Camera and HUD offsets were read from reflected member descriptions. The console profiles use the global console pointer and validate its vtable before reading the shared fraction/forced fields.

| Revision | Render context global | Game system global | Player vtable | Wall vtable | HUD | Console global | Console vtable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 6.66 | `0x6400940` | `0x3969438` | `0x2e0abd0` | `0x2c48728` | `0x472d8` | `0x44fb498` | `0x2a426d0` |
| 6.66 Rev 1 | `0x6423fc0` | `0x398c438` | `0x2e2ae68` | `0x2c68a88` | `0x472d8` | `0x451eb18` | `0x2a5d6d0` |
| 6.66 Rev 1.1 | `0x64253c0` | `0x398d438` | `0x2e2bf68` | `0x2c69ad8` | `0x472d8` | `0x451ff18` | `0x2a5e710` |
| 6.66 Rev 2 | `0x6460140` | `0x39c3498` | `0x2e5b930` | `0x2c97ff8` | `0x472e8` | `0x455ab18` | `0x2a8b4f8` |
| 6.66 Rev 2.2 | `0x64340c0` | `0x399b498` | `0x2e38d10` | `0x2c75fc8` | `0x472d0` | `0x452ea98` | `0x2a694d8` |
| 6.66 Rev 3 | `0x66d08a0` | `0x3880398` | `0x2da6078` | `0x2be21f0` | `0x472d0` | `0x47cb218` | `0x2e3c928` |
| 6.66 Rev 3.2 | `0x66e2d60` | `0x38913a0` | `0x2db5698` | `0x2bf12f0` | `0x472d0` | `0x47dd918` | `0x2e4c840` |

Offsets below are relative to their owning render context, render view, map or HUD object. The entity/Havok decoder is shared across these profiles.

| Revision | Views | View position | View matrix | Map player | Map entities | HUD menus | HUD paused |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 6.66 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x390` | `+0x3e4` |
| 6.66 Rev 1 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x390` | `+0x3e4` |
| 6.66 Rev 1.1 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x390` | `+0x3e4` |
| 6.66 Rev 2 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x398` | `+0x3ec` |
| 6.66 Rev 2.2 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x398` | `+0x3ec` |
| 6.66 Rev 3 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x398` | `+0x3ec` |
| 6.66 Rev 3.2 | `+0x5e71c0` | `+0x28a64` | `+0x29440` | `+0x1af8` | `+0x61cd8` | `+0x398` | `+0x3ec` |

## Checks

- Viewer behavior tests passed for all 22 profiles, including 1.0, 1.1, 3.1 and 5.1.
- Fixtures cover translated cameras, profile-specific view allocation sizes, direct/indirect consoles, null/wrong console objects, and trigger getters near the end of each profile's code range.
- Depth tracker tests passed.
- All 19 isolated GPU readback cases passed, including depth rejection, console clipping and separate graphics/presentation queues. The tested DLL matches the local application build.

## Live validation

| Revision | Production readers | Layer draw log | Visual alignment |
| --- | --- | --- | --- |
| 6.66 | Passed | Passed | Confirmed |
| 6.66 Rev 1 | Passed | Passed | Confirmed |
| 6.66 Rev 1.1 | Passed | Passed | Confirmed |
| 6.66 Rev 2 | Passed | Passed | Confirmed |
| 6.66 Rev 2.2 | Passed | Passed | Confirmed |
| 6.66 Rev 3 | Passed | Passed | Confirmed |
| 6.66 Rev 3.2 | Passed | Passed | Confirmed |

Live checks open the game with query/read access only. The user starts and stops the game. A newly built Vulkan layer is loaded on a subsequent game start through the selected-executable registration. Other executable variants with the same version label remain unverified.

## Observed live scenes

Counts describe the decoded level at the time of the production probe; active entities vary during gameplay.

| Revision | Level | World instances | Triangles | Triggers | Walls | Unsupported |
| --- | --- | --- | --- | --- | --- | --- |
| 6.66 | `game/dlc2/e5m1_spear/e5m1_spear` | 12015 | 626617 | 796 | 28 | 0 |
| 6.66 Rev 1 | `game/dlc/e4m1_rig/e4m1_rig` | 28135 | 1320656 | 995 | 25 | 0 |
| 6.66 Rev 1.1 | `game/dlc/e4m1_rig/e4m1_rig` | 28135 | 1320656 | 994 | 25 | 0 |
| 6.66 Rev 2 | `game/dlc2/e5m1_spear/e5m1_spear` | 12015 | 626617 | 796 | 28 | 0 |
| 6.66 Rev 2.2 | `game/dlc/e4m1_rig/e4m1_rig` | 28135 | 1320656 | 990 | 25 | 0 |
| 6.66 Rev 3 | `game/sp/e1m1_intro/e1m1_intro` | 31366 | 1701533 | 404 | 17 | 0 |
| 6.66 Rev 3.2 | `game/dlc2/e5m1_spear/e5m1_spear` | 12015 | 626617 | 796 | 28 | 0 |
