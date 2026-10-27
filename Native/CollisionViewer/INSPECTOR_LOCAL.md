# Volume inspector — experimental research feature

Included in DEViL 0.7. This filename is retained from the original local prototype.

## Use

1. Extract the release archive and start `DEViL.exe` (or use your separately published development build).
2. Select the game executable and enable Collision Viewer and the desired volume groups.
3. Enable **Volume inspector (meathook)** in Render Settings.
4. Launch the game with a meathook build compatible with that executable. The already-loaded `XINPUT1_3.dll` must export `mh_api_get`; the upstream v7.2 binary has this export. DEViL does not install or load meathook itself. API availability does not guarantee that a meathook binary supports every game version.
5. Aim at a displayed trigger or invisible wall. The closest exact triangle intersection along the centre-screen ray is highlighted yellow. Selection follows aim automatically; it respects group toggles and view distance. World collision meshes have no entity identity and are not selectable. World geometry is not used as a picking occluder, so this is a volume-inspection ray, not a weapon/focus trace.

The card uses the game's `idDebugHUD` via meathook persistent text, rather than a separate Windows window or Vulkan text renderer. It shows name, runtime class, map entity ID and generation, physical collision state, role, distance, and a documented encounter name where a confirmed mapping exists. `Enabled` describes collision availability; it does not mean the encounter has started or a trigger has fired. The ID is the map-table identifier, not an unverified claim about console spawn-ID encoding.

The card clears on pause, menus, console, map changes, stale data, or disabling the inspector. No per-frame engine methods run on the Vulkan presentation thread. Meathook callbacks consume a copied selection on the game frame thread. Because the public API has no callback removal, its module and the viewer module stay pinned until process exit after binding.

The inspector automatically executes `com_debugHUD 1` through meathook on the game frame thread when its card becomes active. Merely enqueuing persistent text is insufficient with the released meathook 7.2: with this cvar at 0, the game's native renderer skips the queue. The command is not repeated every frame. Hiding/disabling the inspector removes its own rows and leaves the shared HUD enabled for other tools. The log distinguishes API registration from callback execution, visibility requests and submitted rows.

Without the required API, geometric highlighting still works and `viewer-runtime/VulkanViewer.log` explains why text is unavailable. The viewer's settings and registration use the usual DEViL per-user directory, so close another DEViL window before changing settings in this local build. Restart the game after updating its loaded viewer DLL.

The companion now compares the packaged DLL's content-derived filename with the viewer module actually loaded in the game. `Update pending` means that the running process has an old/conflicting viewer or no viewer; its tooltip names the expected and loaded files. Changing the manifest does not replace a module already loaded in a running game. A matching module is reported as running even if the companion window was opened later. If module metadata cannot be read, the previous launch-time heuristic remains the fallback.

## Encounter labels

Source: [user-supplied encounter reference](https://docs.google.com/document/d/15BZ6j8a0nQukOvY93aHMsUO2MStfqPIX33Rl-8CGRR8/edit), accessed 2026-10-26. The document credits DrakePHOSE, bowsr, DrLa and testing with siavash's HP Tool.

`src/encounter_labels.inc` contains 188 explicit, certain `Name in File` mappings scoped to campaign/DLC maps. Missing, question-mark, ambiguous and master-level entries are excluded. Names remain in the document's original English; the technical class and entity name remain visible. Encounter-trigger owners are resolved through reflected `owners` fields and validated managed pointers, not by proximity or substring guesses. If the layout or mapping is unavailable, only confirmed technical data and a general role are shown. The document describes mostly earlier patches; labels are reference annotations, not proof of current script behaviour.

Regenerate with `python tools/import_encounter_labels.py <local HTML export>` from this directory. The import script has no network access and does not copy encounter wave/kill-condition prose.

## Verification

`InspectorTests.exe` covers exact ray intersections, range, reverse/inside views, bounds-only misses, map-scoped labels, reflected owner links, unsafe text escapes, generation reuse, collision-state changes and HUD cleanup on all 22 profile fixtures. HUD calls use a recording backend; fixture success does not verify native font rendering or binary compatibility inside a running game. Run `ViewerBehaviorTests.exe`, `DepthTrackerTests.exe`, `LightingFixTests.exe` and the isolated GPU checks as well.

The HUD visibility regression starts with the native HUD disabled, requires activation before any row submission, checks that the visibility command is not issued every frame, and verifies reactivation after menus without disabling another tool's shared HUD. On the user's running 6.66 Rev 3, read-only inspection confirmed one registered callback, populated native text rows (including name, class and ID), and `com_debugHUD=0`; this was the cause of the invisible card. The local native fix is built with `build-inspector-native.cmd` beside the checkout and can replace the packaged viewer DLL without closing the companion; the running game still needs a restart to load a new DLL.

Live acceptance still needs a compatible game/meathook pair: confirm the native card appears, names/class/ID match the aimed volume, encounter labels match the location, performance metrics remain usable, and the card disappears in menus/console and when disabled.
