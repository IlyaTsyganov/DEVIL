# DEViL

**DOOM Eternal Visualization & Inspection Layer** is a Windows companion application for viewing the game's collision geometry. It configures a Vulkan layer for the selected game executable and provides a single window for choosing what geometry to show and how to render it.

DEViL is an unofficial community tool. The companion configures the Vulkan loader without modifying the game installation or injecting code. The viewer is loaded when the supported game executable starts and reads camera and physics data inside that process; it does not write game memory.

## Features

- Enable or disable the Collision Viewer for the next game launch.
- Display world collision, triggers, and invisible walls independently.
- Choose filled surfaces or outlines and set the render distance from 20 to 200 metres.
- Select the game executable and launch it from DEViL.
- Save viewer and rendering preferences automatically between runs.
- Show whether the game and companion utility processes are running.
- Use a per-user Vulkan layer registration scoped to the selected executable; unrelated applications are not opted in.

## Download and run

Download [DEViL 0.4 for Windows x64](https://github.com/IlyaTsyganov/DEVIL/releases/download/0.4/DEViL-0.4.zip), extract the archive, and run `DEViL.exe`. Keep the included files and folders together. The package requires the **.NET 10 Desktop Runtime**. The archive includes the corresponding source snapshot and licenses; `SHA256SUMS.txt` contains its checksum.

In the application, select `DOOMEternalx64vk.exe`, enable Collision Viewer, choose the geometry and rendering options, then launch the game. Changes to the viewer setting take effect on the next game launch. Executable selection and settings accept other game versions without a version hash check. The viewer automatically selects audited profiles for DOOM Eternal 1.0 (Release), 1.1, 3.1 and 5.1; other executable layouts withhold rendering. All profiles use the same player-blocking contents filter, frustum/distance culling, depth occlusion, menu/pause hiding and console clipping. Levels are discovered from the current map and physics state, including DLC levels available in 3.1 and 5.1; no map filename list is required. See the [3.1 validation record](Native/CollisionViewer/PROFILE_3_1.md) and [5.1 validation record](Native/CollisionViewer/PROFILE_5_1.md) for the verified scope of each profile.

## Build from source

Requirements: Windows x64, Visual Studio Build Tools with the C++ workload, and the .NET 10 SDK.

From the repository root, build the native viewer and then publish the Windows application:

```powershell
cmd /c .\Native\CollisionViewer\build-viewer.cmd
dotnet publish .\DEViL.csproj -c Release -o .\publish
```

The Windows application source is organized at the repository root. Native source, tests, shaders, and third-party notices are under `Native/CollisionViewer/`. Vulkan-Headers and their license files are included under `third_party/Vulkan-Headers/`.

The native build creates `ViewerBehaviorTests.exe`, `DepthTrackerTests.exe`, `LayerLoaderProbe.exe`, `RetailProfileProbe.exe`, and `VulkanSmoke.exe` under `Native/CollisionViewer/bin/`. Run the first two to check all four game profiles, viewer behavior and depth tracking. The loader probe accepts the path to a Vulkan layer runtime directory as its argument. A preconfigured MSVC environment (including a portable toolchain) can be used instead of a Visual Studio Installer installation.

Run `Native/CollisionViewer/tests/run-gpu-tests.ps1` with PowerShell 7 to execute 19 isolated GPU readback cases for depth occlusion, console clipping and separate graphics/presentation queues. Results are saved to `Native/CollisionViewer/build/gpu-verification.json`. Split-queue cases require a GPU with a non-graphics queue family that supports presentation. If the temporary path contains non-ASCII characters, provide an ASCII directory through `-RuntimeDirectory` for the Vulkan loader. Tests do not launch or modify the game.

## Safety and limitations

When enabled, DEViL writes its own manifests under the current user's local application data and registers them in that user's Vulkan layer settings. Registration is scoped to the selected game executable. Disabling the viewer removes only DEViL's own registration. The application does not stop or modify a running game process; a running game must be restarted for changed viewer settings to apply.

The bundled layer supports the audited 1.0 (Release), 1.1, 3.1 and 5.1 executables identified in `Native/CollisionViewer/src/native_profile.h`. Executable PE metadata and a code fingerprint select the layout; accepting a path in settings does not imply support for other layouts. Elevated launches have not been verified. Large render distances can reduce game performance. Native behavior has automated tests for all four profiles, but rendering should be validated in-game on the target system. `RetailProfileProbe.exe <game-pid>` runs the production camera, menu/console and geometry readers against a loaded level with read-only process access; it does not inject code or modify game state.

Presentation may use a compute/transfer queue that cannot draw graphics. The layer renders through a registered graphics queue and forwards the completed image to the original presentation queue. Concurrent sharing respects the game's allowed families; exclusive sharing uses paired image ownership barriers and image-indexed semaphores in both directions. A combined graphics/presentation queue keeps the original submission path. The isolated `VulkanSmoke.exe` test accepts `DCV_TEST_SPLIT_PRESENT=concurrent` or `exclusive` to exercise this on a supported GPU without launching or modifying the game.

Depth diagnostics: `viewer-runtime/Visibility.request` is an optional experiment file containing `imageHandleHex mode probeId bias` (for example, `0 2 0 0.05`). Mode 2 is the normal gameplay default; 0 and 1 are projection experiments. Requests are accepted only when written during the current game process lifetime. Older files are ignored because projection overrides and Vulkan image handles must not carry over between launches. Removing an active request, or replacing it with invalid input, restores defaults. `VulkanViewer.log` records the effective settings, request status and process ID. A requested depth probe also logs the finite sample range and warns about uniform depth; draw-call counts alone do not prove that fragments survived depth rejection.

## Licensing and assets

The source is distributed under [GNU GPL v3](LICENSE). Vulkan-Headers retain their upstream Apache-2.0 and MIT terms and notices. The included DOOM Eternal typefaces and logo assets remain the property of their respective owners; their inclusion does not transfer ownership or grant rights beyond the original terms. DOOM Eternal is a trademark of its respective owners. DEViL is not affiliated with or endorsed by id Software or Bethesda.

