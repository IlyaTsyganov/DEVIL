# Active encounter list and native HUD — experimental features

Enable Collision Viewer before launching the game through DEViL so the layer is loaded, then open **Active encounters…**. The window can stay open while using the main companion window or switching back to the game. Geometry groups can be hidden independently; the live list and HUD need the loaded layer even when its collision drawing is subsequently disabled.

- The default list shows managers whose reflected `idBloatedEntity::thinkFlags` is nonzero, matching meathook's newer active-manager criterion. Unreadable manager states remain explicitly unavailable rather than being silently counted as inactive.
- **Show inactive managers** reveals all observed `idEncounterManager` entities for inspection. Search matches either the reference title or technical manager name.
- Columns retain the human/reference title, technical name, state, script index, active script-command count and entity ID/generation. Reference names reuse the existing 188 exact map-scoped mappings from the user's encounter document. Unknown names remain technical; campaign labels are not reused for master/horde maps.
- Snapshots update approximately every 250 ms. The selected game's PID identifies its snapshot file. Loading, missing metadata, incompatible meathook, unreadable data and snapshots older than two seconds have separate statuses.

This measures manager activity, not proof that the player is in a combat encounter or that an encounter completed. Paused managers, spawn-limit pauses and waiting-for-exit states are read from separate reflected fields. `isEnabled` is not substituted for activity. Optional fields that are absent/unreadable stay unknown.

## In-game native debug HUD

Enable **Show in-game HUD**, or set the real engine cvar:

```
lab_encounters_show 1
lab_encounters_show 0
```

With no value, `lab_encounters_show` uses the engine's normal cvar query behavior. `lab_encounters_list` prints the latest active list to the console; `lab_commands_list` lists the cvars and action commands.

The HUD uses meathook's persistent native debug text at virtual coordinates x=24, y=32..240, above DEViL's existing inspector at y=280..460. It shows up to five active managers with reference/technical names, script index and entity ID. Overflow points to the full list in DEViL. Menu/pause/console and loading transitions hide old text; stale names are removed. Only the encounter HUD's own text rows are removed. `com_debugHUD 1` is requested on the game thread for each visible interval, preserving the shared engine HUD when DEViL's text is hidden.

The reference list and HUD use the same copied snapshots. Metadata lookup occurs on a meathook game-frame callback; the background sampler only reads scalar fields and bounded strings. Name-string offsets are themselves resolved from reflection (`idEntity::name`, `idStr::data`); no encounter offsets from another executable version are assumed. Map-slot identity, generation, deletion, vtable and map/world identity are checked around reads. Atomic versioned files carry copied values to the companion; the GUI never dereferences entity pointers.

## Cvars versus commands

Persistent scalar settings are registered as actual engine cvars through the audited native constructor and cvar-system registration method. Current creation support is **6.66 Rev 3**; unknown versions or changed method signatures withhold cvar creation with one diagnostic for the game process. Action commands and GUI features remain available independently.

Initial values come from DEViL's saved settings. Console changes are delivered to the running companion and saved through the same settings path as UI changes; UI changes are reflected back into cvars through the native setter. The engine applies the registered boolean/integer type and range; values outside DEViL's accepted range and unacknowledged changes are restored to the saved value. The normal engine cvar query and completion behavior remains available.

The earlier implementation incorrectly tried to create variables using the textual command `set`. That command is absent in the user's 6.66 Rev 3, so failed creation was retried every two seconds. The current implementation never sends `set`: it constructs process-lifetime DEViL-owned `idCVar` objects using the engine constructor, then registers them through the cvar system. Constructor, register and setter code are guarded by the executable identity, expected method addresses and code signatures, in addition to the cvar-system RTTI and value-object ABI. Name collisions are rejected before any variables are created. Failed registration, lost ownership or rejected updates stop synchronization with one diagnostic; they do not restart a registration loop.

| State cvar | Values |
| --- | --- |
| `lab_viewer_show` | 0/1 |
| `lab_triggers_show` | 0/1 |
| `lab_collision_show` | 0/1 |
| `lab_walls_show` | 0/1 |
| `lab_inspector_show` | 0/1 |
| `lab_disabled_volumes_show` | 0/1 |
| `lab_volume_history_show` | 0/1 |
| `lab_path_show` | 0/1 |
| `lab_player_show` | 0/1 — player collider |
| `lab_surface_show` | 0/1 — support-plane angle HUD |
| `lab_attempts_show` | 0/1 — compared attempt paths |
| `lab_encounters_show` | 0/1 |
| `lab_viewer_fill` | 0 outlines / 1 filled |
| `lab_viewer_distance` | 20..200 |

| Action command | Arguments |
| --- | --- |
| `lab_commands_list` | none |
| `lab_encounters_list` | none |
| `lab_recording_start` | none |
| `lab_recording_stop` | none |
| `lab_recording_clear` | none |
| `lab_markers_add` | text |
| `lab_trigger_set` | exact class, 0/1 |

The previous short command names remain compatibility aliases. There are no separate show/hide commands for the new visibility settings. `lab_trigger_set` is an action on one item in the class-visibility map, rather than a scalar cvar.

Controls only affect DEViL while its companion is running and the matching game executable is selected. Engine registrations live for the game process lifetime: names can remain queryable/completable after the companion closes, but action handlers refuse execution and cvar changes are not applied to DEViL's settings. Closing the app clears the encounter HUD. Meathook and the layer are pinned because the public callback API has no unregister operation.

## Local checks

Native fixtures cover active/inactive/paused/removed/unreadable managers, reflection-dependent offsets, scoped aliases, unknown reference names, slot reuse, map changes, file publication, native HUD visibility/clearing and cvar value-object identity. Player/game layouts are checked across all 22 profiles. Managed tests cover snapshot freshness and PID scoping, search/filtering, partial input, UI/HUD preferences and both window layouts.

A read-only production probe on the user's 6.66 Rev 3 / UAC Atlantica found 36 managers. An initial sample identified `starting_platform_encounter_manager_1` as active and matched **UAC Atlantica / Combat 1 Landing Pad 1**; later samples correctly reported no active managers after the game state changed. The cvar system's RTTI and existing `com_debugHUD`/`com_skipIntroVideo` value layouts were inspected without modifying the game.

The original encounter feature is included in DEViL 0.7; the native cvar registration correction is in the development build. The updated DLL must be loaded by a complete game restart through DEViL. `LabCvarTests.exe` covers creation/synchronization, quiet idle, UI/console changes, companion close/reopen, acknowledgment/timeout, foreign ownership and permanent failure without retries. The production resolver passed a read-only probe against the user's running 6.66 Rev 3. Executing the corrected constructor/register/setter still requires in-game acceptance after loading the new layer.
