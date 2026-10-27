# DEViL 0.8 — research tools and practice checkpoints

Released 2026-10-27. Requires Windows x64 and the .NET 10 Desktop Runtime (x64).
Download **DEViL-0.8.zip**, extract it and run **DEViL.exe**. Matching source and licenses are included; **SHA256SUMS.txt** verifies the ZIP.

This release includes the accumulated entity graph, trigger-folder filters, Movement lab, support projections, before/after snapshots, research points, simple practice-save library and firewall launch gate described below. A separately installed compatible meathook is required for native HUD and reflection features; it is not bundled.

Live checkpoint archiving and repeated early/late restoration passed in-game acceptance without game/Steam restarts on **6.66 Rev 3 / TAG1 / slot 3 / UAC Atlantica**. The user also confirmed the simplified companion starts without errors. Cloud must be off globally or for the game; mixing Steam accounts remains blocked. Archives preserve completed checkpoint files and are not arbitrary RAM savestates. Other builds/slots use the advanced offline workflow, whose closed-process guards remain intact.

Launch from DEViL requires DESRU's enabled outbound block for the exact selected game EXE. Existing Windows rules are only read. When upgrading from public 0.7, restart the game once through the new build to load its native layer. When upgrading from the already tested live-restore/simple-saves development build, only the companion needs restarting: the native DLL is unchanged.

Validation: eleven native fixture suites, managed parser/settings/save/firewall checks, WinForms scenario/minimum-layout checks and 19 isolated Vulkan GPU cases passed. Fixture coverage across 22 collision-viewer profiles does not broaden the narrow live-restore support or prove HUD visibility and every research feature on every build.

## Changes since 0.7

- Simplify **Сохранения…** to an automatically detected Steam folder, optional name, searchable library and **Сохранить / Загрузить** buttons. New captures are selected automatically; live loading needs no intermediate dialog. Technical offline/recovery tools remain under **Дополнительно…**. Existing archives and all native account/Cloud/pause/writer checks are preserved. Live support remains 6.66 Rev 3 / TAG1 slot 3.
- Require an effective Windows Firewall outbound block for the exact selected executable before launch from DEViL. Disabled/inbound/allow/partial rules, missing active-profile coverage and disabled firewall profiles reject launch with a reason. The check does not change Windows settings or restrict manual launches outside DEViL.

- Add live archival and **Restore archive + reload…** for same-account native live-copy archives on 6.66 Rev 3 / TAG1 slot 3. Pin reviewed source/target hashes, authenticate independently through Windows CNG, preserve all four originals, write through Steam storage and verify disk/cache bytes before engine loading. Roll partial writes back; retain recovery paths on failure. Extend the companion wait and explain pressing Space at 100% loading. Native archival and repeated restoration between earlier/later checkpoints passed retail acceptance without restarting game/Steam; other slots remained unchanged.

- Fix the live experiment falsely reporting Cloud enabled when Steam Cloud is globally disabled but the per-game preference remains enabled. Query both account and app flags, report their values, and permit operations when either setting disables synchronization; unknown states still block. Add regression coverage for all combinations.


- Add **Research points & practice saves…** (proposal item 7): persistent points with map/profile, physical position, rendered eye/aim/matrix, loaded checkpoint, selected volumes, category/route notes and practice-archive links. Copy return/spawn data, edit/search and import/export points. Capture never executes a teleport or save command.
- Add full Steam checkpoint-slot archives with per-file SHA-256 and AES-GCM checks, including both internal backups. Prepare a separate same-account archive from a valid backup pair when primary containers fail/miss. Guard imports/restores against account mismatch, guard campaign-family/occupied-slot/Cloud/process compatibility, stage replacement and preserve current-slot/profile/Steam evidence with failure rollback. No owner changes, automatic profile/index deletion or arbitrary live-state restoration. See [instructions and validation](Native/CollisionViewer/RESEARCH_POINTS_LOCAL.md).
- Add **Level state: before / after…** (proposal item 6): on-demand recognized-volume and encounter-manager snapshots, loaded spawn-info checkpoint name, identity/state/transform/bounds/fingerprint comparisons, selected/all captured outlines, JSON pairs/notes and complete CSV export. Preserve the distinction between removed identities and unavailable data; reject different processes/profiles/map instances. Saved fields remain readable without the native outline cache. See [usage and validation](Native/CollisionViewer/LEVEL_STATE_LOCAL.md).
- Move the optional support-angle HUD from y=500 to a compact block at `(360,300)` and add callback/submission/source diagnostics for missing text. Keep the companion running and reload the layer with a full game restart; live visibility remains subject to in-game acceptance.
- Refresh the player collider pose every presented frame while reusing verified background geometry; keep recording/target measurements at 20 Hz. Add **Surface projections…** with XY/XZ/YZ plane cuts, signed tangent angles, live/held/recorded sources and a `lab_surface_show` numerical HUD. Distinguish coincident cuts and edge/face geometry from the single observed support plane. Report frame-reader CPU cost in the native log.

- Add **Movement lab…** (proposal item 5): validated convex player core with Havok rounding, body bounds/velocity/support trace, 20 Hz target sampling, named persistent attempts, manual success/failure annotations, up to six world-space path overlays, XY/XZ/YZ plots and elapsed-time comparison. Show origin-to-boundary distance and rounded-hull skin gap for an aimed/held volume; export complete attempts as CSV. Invalid geometry falls back to explicitly labelled bounds; sampling gaps remain gaps. New native visibility cvars: `lab_player_show` / `lab_attempts_show`.

- Show all distinct incoming/outgoing neighbors in **Entity links…**, replacing the four-per-side display limit with automatic fit. Add pointer-centred wheel zoom, RMB drag, LMB node selection, full-name hover hints and **Fit graph**. Periodic updates retain manual zoom/position; new roots and maps fit automatically.

- Add hierarchical path filters beneath each trigger class in **Trigger types…**. The bundled catalogue contains 24 class/folder groups from 107 trigger definitions in the installed 6.66 Rev 3 resources, including lootdrop, weapons, ai, stat_event and encounter paths. Live discoveries extend it. Match both instance names and entity definitions so map-renamed objects keep their resource subtype; flat names have no artificial directory entry. Choices are class-scoped, persistent and apply live to surfaces, outlines, history snapshots and picking. Parent switches retain child preferences.

- Fix repeated `Unknown command 'set'` messages: create owned `lab_*` variables with the audited native engine constructor/register API, synchronize through the native setter and stop permanently on failed registration/ownership checks. Native creation is guarded for 6.66 Rev 3; unsupported versions produce one diagnostic and retain GUI/action-command features. The resolver passed read-only validation on the running retail game; executing the new native calls still needs acceptance after restarting with this build.
- Fix **Entity metadata unavailable**: accept empty superclass names for root/value types and follow the public field-name terminator. Validate resolved managed pointers against map slots instead of equating different ID domains; omit internal self pointers. A read-only capture of UAC Atlantica produced 10,107 entities / 1,694 links / 97 owner links and passed companion parsing.

- Add **Entity links…**: a navigable graph of observed runtime entity references, crosshair following, technical/reference-name search, incoming/outgoing field evidence, name copying and full JSON export.
- Resolve current-game layouts through meathook reflection; validate map slots, generations and stable resolved managed pointers. Missing metadata, read errors, stale data and scan limits remain visible. Capture runs while the window requests it.
- Links describe sampled references, not script execution or confirmed causal dependencies. Script-only and static map links are not decoded. See [scope, controls and validation](Native/CollisionViewer/ENTITY_GRAPH_LOCAL.md).
- Automated coverage includes the new graph suite on all 22 profile fixtures, existing native suites, managed parser/control checks and 19 isolated GPU cases. The graph still needs in-game acceptance with this layer.

# DEViL 0.7

Adds tools for studying DOOM Eternal volumes, routes and encounter managers.

- **Volume inspector:** aim at a trigger or invisible wall to highlight its exact geometry and show its name, class, map ID/generation, collision state and role in the game's native debug HUD. Confirmed encounter names are taken from 188 map-scoped mappings in the community reference.
- **Volume changes:** optionally show disabled volumes, flash observed state changes and retain removed/unreadable geometry as dashed snapshots for up to 15 seconds.
- **Trigger filters:** a searchable checklist controls visibility of each exact runtime trigger class and remembers choices.
- **Research timeline and player path:** manually start/stop recording, display the recorded route, add markers and export the full journal to CSV. Path visibility and recording are independent.
- **Active encounters:** a live window updates about every 250 ms with reference/technical names, state, script counters and entity IDs. An optional native HUD shows up to five active managers.
- **Console controls:** real `lab_*` cvars represent settings, such as `lab_encounters_show 0/1` and `lab_path_show 0/1`. Commands perform actions: `lab_recording_start`, `lab_recording_stop`, `lab_encounters_list`. Use `lab_commands_list` for the complete reference. Controls apply only while the matching DEViL companion is running.
- Launching the game from DEViL always adds `+com_skipIntroVideo 1`. The companion also reports **Update pending** when the running game has an older viewer DLL.

Download **DEViL-0.7.zip**, extract it and run **DEViL.exe**. Requires Windows x64 and the **.NET 10 Desktop Runtime (x64)**. The archive includes matching source code and licenses; **SHA256SUMS.txt** contains its checksum. A separately installed, compatible **meathook** is required for native HUD text, encounter metadata and console controls; it is not bundled.

Completely exit the game and launch it through the updated DEViL with Collision Viewer enabled to load the new layer. Enable **Volume inspector (meathook)** for the inspector, or open **Active encounters…** and select **Show in-game HUD**. `lab_encounters_show 1` enables that HUD; `0` hides it; entering the cvar without a value queries it.

Validation: all six native suites, 22 profile fixtures, managed launcher/settings/parser checks, UI layout/control checks and 19 isolated Vulkan GPU readback cases passed. Read-only production inspection on **6.66 Rev 3 / UAC Atlantica** validated player position, 36 encounter managers, reference-name matching and the existing engine command/cvar-system ABI. The inspector HUD visibility fix was confirmed by the user on that game build.

Research features are experimental. The newly added encounter HUD, route recording and native cvar creation still require in-game acceptance after loading this layer; fixture coverage is not a claim that meathook supports every collision-viewer profile. Manager activity means a nonzero reflected `thinkFlags`, not proof that combat started or completed. Unknown fields and unmatched reference names remain explicit rather than being inferred.
