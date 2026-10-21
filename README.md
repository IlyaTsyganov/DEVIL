# DEViL

**DOOM Eternal Visualization & Inspection Layer** is a Windows companion application for viewing the game's collision geometry. It configures a Vulkan layer for the selected game executable and provides a single window for choosing what geometry to show and how to render it.

DEViL is an unofficial community tool. It does not modify the game installation or access game-process memory. The viewer is loaded by the Vulkan loader when the supported game executable starts.

## Features

- Enable or disable the Collision Viewer for the next game launch.
- Display world collision, triggers, and invisible walls independently.
- Choose filled surfaces or outlines and set the render distance from 20 to 200 metres.
- Select the game executable and launch it from DEViL.
- Save viewer and rendering preferences automatically between runs.
- Show whether the game and companion utility processes are running.
- Use a per-user Vulkan layer registration scoped to the selected executable; unrelated applications are not opted in.

## Download and run

Download [DEViL for Windows x64](releases/DEViL-standalone-2026-10-21.zip), extract the archive, and run `DEViL.exe`. Keep the included files and folders together. The package requires the **.NET 10 Desktop Runtime**.

In the application, select `DOOMEternalx64vk.exe`, enable Collision Viewer, choose the geometry and rendering options, then launch the game. Changes to the viewer setting take effect on the next game launch. DEViL supports the verified DOOM Eternal 1.1 executable profile only.

## Build from source

Requirements: Windows x64, Visual Studio Build Tools with the C++ workload, and the .NET 10 SDK.

From the repository root, build the native viewer and then publish the Windows application:

```powershell
cmd /c .\StandaloneApp\Native\CollisionViewer\build-viewer.cmd
dotnet publish .\StandaloneApp\DEViL.csproj -c Release -o .\publish
```

The native source, tests, shaders, and third-party notices are under `StandaloneApp/Native/CollisionViewer/`. Vulkan-Headers and their license files are included under `third_party/Vulkan-Headers/`.

## Safety and limitations

When enabled, DEViL writes its own manifests under the current user's local application data and registers them in that user's Vulkan layer settings. Registration is scoped to the selected game executable. Disabling the viewer removes only DEViL's own registration. The application does not stop or modify a running game process; a running game must be restarted for changed viewer settings to apply.

The bundled layer is tied to a specific DOOM Eternal 1.1 executable profile. Other game versions and elevated launches have not been verified. Large render distances can reduce game performance. Native behavior has automated tests, but rendering should be validated in-game on the target system.

## Licensing and assets

The source is distributed under [GNU GPL v3](LICENSE). Vulkan-Headers retain their upstream Apache-2.0 and MIT terms and notices. The included DOOM Eternal typefaces and logo assets remain the property of their respective owners; their inclusion does not transfer ownership or grant rights beyond the original terms. DOOM Eternal is a trademark of its respective owners. DEViL is not affiliated with or endorsed by id Software or Bethesda.

