# Lighting fix validation

Included in DEViL 0.6, checked on 2026-10-26.

The default Vulkan 1.x indirect-barrier correction is adapted from [billymooreq's DoomEternal-v1.1--RTXFix](https://github.com/billymooreq/DoomEternal-v1.1--RTXFix/tree/0bf1f44b5f4945eb7e6b76a204b4f6f2a074e23e), under the [MIT license](licenses/doomfix-MIT.txt). The DEViL launch button opts in audited 1.0, 1.1 and 2.0 executables. The native layer verifies the profile and NVIDIA vendor ID again. The child also receives `SteamAppId=782330`, as in the upstream launcher, to prevent a Steam restart from discarding its environment.

## In-game checks

All three versions were launched through DEViL and tested on Hell on Earth (`game/sp/e1m1_intro/e1m1_intro`). The user confirmed restored lighting, correct barrel rendering and correct collision-viewer volumes in each version. Runtime logs independently confirmed `Lighting fix: on`, increasing patched-barrier counters, loaded world geometry and overlay draw calls.

| Version | World collision instances | Unique geometries | Triangles | Unsupported world shapes | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| 1.0 | 31,338 | 13,412 | 1,800,516 | 0 | Passed |
| 1.1 | 31,338 | 13,412 | 1,800,516 | 0 | Passed |
| 2.0 | 31,353 | 13,428 | 1,800,678 | 0 | Passed |

Entity readers reported zero unsupported objects in each run. Collision Viewer retains its existing depth visibility, geometry filters, menu hiding and console clipping. Lighting synchronization is corrected while recording the game's commands; overlay rendering remains at presentation after the game frame.

## Automated checks

- Viewer behavior tests cover all 22 native profiles; depth-tracking tests passed.
- `LightingFixTests.exe` exercises actual barrier/event hooks, unchanged caller arrays, idempotence, missing global dependencies, version/vendor/launch gating and operation with the viewer disabled.
- Launcher tests cross-check all 22 displayed versions against the native profile identities and code fingerprints, and check child-only environment handling and the Steam app ID.
- All 19 GPU readback cases passed with lighting correction active, including depth occlusion, console clipping and split graphics/presentation queues. Each case checks that a real pipeline barrier was corrected before presenting the overlay.

Validated native DLL SHA256: `1d7f3ec57cbdeffcec7c7b7a17bc107b3afd4aaf901bbf21bb1fd270533d6d9d`.
