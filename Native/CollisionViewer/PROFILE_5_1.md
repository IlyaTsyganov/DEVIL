# DOOM Eternal 5.1 validation

Validated on 2026-10-25.

## Executable identity

- PE timestamp: `0x607e69b0`.
- PE SizeOfImage: `0x1b5dd000`.
- SHA-256: `93da12b964438b8bcabeb8fb12ff4050cbc6042f723b00baa89544482292594c`.
- Profile selection uses the same PE metadata and code fingerprint checks as 1.0/1.1.

## Audited differences

| Field | 1.1 | 5.1 |
| --- | --- | --- |
| Render manager global RVA | `0x5bd6540` | `0x5ea7cd0` |
| Game system global RVA | `0x33fc468` | `0x367c8c0` |
| Player vtable RVA | `0x2984578` | `0x2bc5628` |
| Dynamic wall vtable RVA | `0x27f2b58` | `0x2a14ec8` |
| Player HUD offset | `0x36020` | `0x46cd8` |
| Console object / vtable RVA | `0x410bc50` / `0x26080b8` | `0x427bb08` / `0x2811058` |
| Render views list / count | `0x5a7188` / `0x5a7190` | `0x5a7198` / `0x5a71a0` |
| View position / matrix | `0x20a28` / `0x213f0` | `0x20a54` / `0x21430` |
| Map player / entity list | `0x1ad0` / `0x61cb0` | `0x1af8` / `0x61cd8` |
| HUD menus / pause flag | `0x358` / `0x3ac` | `0x388` / `0x3dc` |

The HUD and view members were checked against the game's reflected member descriptions. Map pointers and render-manager views were checked in live memory. The shared entity physics pointer, contents flags, transforms, Havok world shape chain and shape decoder were exercised by the production probe. No write access to the game process is used.

## Read-only integration result

The production `RetailProfileProbe.exe`, running against The World Spear, reported:

```text
Profile: DOOM Eternal 5.1
Camera valid: 1
Gameplay visible: 1
Console fraction: 0.000
Level: game/dlc2/e5m1_spear/e5m1_spear
Entities: examined=10335 triggers=782 walls=28 unsupported=0 meshes=810
World: instances=12005 unique=9088 triangles=622281 nonBlocking=252731 unsupported=0
```

The entity count depends on the current game state. These are decoded active objects across the level, not the number visible from one camera position.

## Automated checks

- `ViewerBehaviorTests.exe`: passed for 1.0, 1.1 and 5.1, covering profile rejection, camera, menus, pause, console, exact convex topology, wall/trigger activation, world filtering and level transitions. Map discovery fixtures include both `game/dlc2/...` and `game/dlc/...` paths.
- `DepthTrackerTests.exe`: passed.
- `tests/run-gpu-tests.ps1`: all 19 isolated GPU readback cases passed, including depth rejection, console clipping and separate graphics/presentation queues. Results: `build/gpu-verification.json`.
- Windows application published successfully with .NET 10 SDK and the rebuilt native layer.

## In-game verification

After restarting with the registered layer and loading The World Spear, the live log confirmed profile 5.1 selection, initialization of the Vulkan renderer, successful world and entity snapshots, and ongoing geometry draws as the camera moved. It also recorded hiding the overlay in menus and restoring it on return to gameplay.

The tester confirmed that the colored volumes were visible and matched the environment when turning the camera. This supplies visual confirmation in addition to the production-reader and GPU tests.

## Scope

Other 5.1 executables with different PE metadata, other game patches and all other levels have not been individually validated. DLC maps use the existing runtime map discovery and shared physics decoding; no separate geometry format or map-specific geometry files were introduced.
