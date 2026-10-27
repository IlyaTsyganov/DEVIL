# Entity links — development build

Open **Entity links…** from DEViL's main window. This modeless window can stay open while playing. The selected game's updated viewer layer and an already installed compatible meathook are required. Completely restart the game through this build of DEViL with Collision Viewer enabled to load the new layer.

Enable **Volume inspector (meathook)** and **Follow crosshair**, then aim at a displayed trigger or invisible wall. Its map-scoped ID and generation select the graph root. Switching to the companion retains the last selected root. Selecting an entity in the list, clicking a graph node or double-clicking an edge row turns off following and lets you explore the graph. Select **Follow crosshair** again to follow the game selection.

The centre is the selected entity. Left nodes contain references to it; right nodes are referenced by it. Both sides show **every distinct observed neighbor**, automatically reducing the entire diagram to fit the available field. Multiple reference fields to the same neighbor share a node; the table retains every observed incoming/outgoing edge and its exact field path.

- **Mouse wheel:** zoom in/out around the pointer, including block labels and arrows.
- **Hold the right mouse button and drag:** move the graph inside its field. Right-clicking does not select a vertex.
- **Left-click a vertex:** select it as the new centre and update the entity list, details and reference table. This turns off Follow crosshair.
- **Fit graph:** restore the automatic overall view. Selecting another root or changing maps also fits the new neighborhood. Resizing fits the graph while automatic mode is active.
- Manual zoom and position survive periodic snapshot updates for the same selected entity. Hover a node to read its full name, class and ID when its scaled label is too small or truncated. The footer displays the scale and distinct neighbor counts.

Search matches technical names, classes, IDs and existing map-scoped encounter reference labels. **Linked entities only** hides isolated nodes from the list. **Copy name** copies the exact sampled technical name. **Export JSON…** saves the complete captured graph, map identity, timestamp, evidence and scan counters, independently of the search or selected root. Pan/zoom does not change the sampled graph or export.

## What a link proves

This version reads runtime references, including encounter trigger `owners` lists. For example, `trigger → manager` with field `owners[0]` means that this list contained a validated reference to that manager. It does **not** prove that the trigger fired, that the manager opened a wall, or that a particular script command ran.

Only observed references are drawn. Guessed dependencies are not generated. Empty results mean **no references found within the scanned fields**, not that the entity has no relationships. Static map/definition links, string-based targets, script execution, event order and causal dependencies are outside this reader. It does not invoke `mh_dumpmap`, `mh_ScriptCmdEnt`, script events or entity property getters. This is the first runtime-reference implementation of proposal item 4; it is not a complete decompilation of level logic.

Supported reference shapes:

- Reflected direct entity or managed-class pointers, whose target is also a validated current map entity.
- Reflected `idManagedClassPtr` values: the resolved pointer must identify a validated current map entity; pointer, reference token and target object number must stay stable during the copied observation. The reference's `spawnId` and the object's `objectNumber` use different numbering domains on the inspected retail level and are not compared for equality.
- Reflected `idList` containers of these values, plus inline nested reflected structures and inherited fields.

Offsets, type sizes and list element strides come from the current game's meathook metadata. Root/value types may have a nonnull pointer to an empty superclass name; that is valid metadata. Reflected field arrays end on a null field-name pointer, following meathook's public iterator. No offsets for encounter links are borrowed from another executable profile. Entity slot ID, generation, address, deletion flag and vtable are checked before/after reading. Endpoints that disappear during capture are discarded. A changed map, world or entity table discards the graph. Map identity and generation also prevent the GUI from retaining a selection after a map change or slot reuse. Internal self pointers are omitted to keep the graph focused on links between different entities.

## Sampling and limits

While the graph window is open it renews a PID-scoped request once per second. The native worker captures approximately once per second; an expired request stops scanning within four seconds. Closing DEViL stops work through the companion presence check. Initial type resolution may take several updates. Meathook class lookup runs only on its game-frame callback, at most four types per frame. The worker receives immutable copied metadata and calls no engine methods.

The status distinguishes pending metadata, unavailable/unsupported reads, stale snapshots and scan limits. Snapshots older than four seconds are labelled stale; loading or missing data clears the displayed graph. Readable stale snapshots remain available as historical captures. Failed metadata resolution is retried periodically while the window is open.

The reader caps the entity table at 32,768 nodes, edges at 16,384, each list at 256 sampled elements (counts above 4,096 are rejected), nested structures at five levels, inheritance at 24 steps and reference-shaped traversal work at 250,000 visits per capture. Triggers, encounter entities and the crosshair root are scanned first. Fixed arrays, unknown container layouts, arbitrary object-pointer traversal and unsupported pointer qualifiers are omitted. These limits mean the graph can be partial even with no read errors. Sampling is not an atomic or frame-accurate event journal.

Snapshots use versioned, atomically replaced `entity-graph-<PID>.tsv` files under `%LOCALAPPDATA%\DEViL`. Companion reads are bounded, reject partial rows, duplicate or dangling identities and mismatched PIDs, and never dereference entity pointers. JSON exports preserve technical identities and evidence for offline inspection.

## Validation

`EntityGraphTests.exe` uses the production reader against all 22 supported executable-profile fixtures. It covers managed owner links with different reference-token/object-number values, direct entity links, nested inline structures, raw pointer lists, exact field paths, integer values that resemble pointers, foreign managed pointers, removed endpoints (including removal during capture), reused generations, map/world changes, null and empty superclass sentinels, unreadable parent pointers, field-name terminators, list limits, atomic publication and request expiry. Managed checks cover parsing, PID/freshness boundaries, searching, isolated nodes and JSON export. UI checks cover following the crosshair root, navigation, both link directions, map changes and minimum-size layout. Navigation checks use 11 incoming and 17 outgoing neighbors, including duplicate reference fields: all 29 vertices fit, the last node remains selectable, zoom anchors to the pointer, RMB drag captures/releases correctly, transformed LMB selection updates the form, refreshes preserve manual views, and Fit graph/map/root/resize changes restore the expected fit. Dense and enlarged diagrams are rendered for visual inspection.

The companion navigation update also passed rendering and fit checks on a copied native capture from the running UAC Atlantica session: 9,570 entities / 1,788 sampled references, with all 43 outgoing neighbors of a heavily linked root visible. This UI change leaves the native layer unchanged; replacing the companion alone is sufficient when the latest trigger-catalogue layer is already loaded. Its earlier native/GPU results remain applicable.

The eight native suites, managed suites and 19 isolated Vulkan GPU readback cases pass. A read-only production probe on 2026-10-27 captured **10,107 entities and 1,694 links, including 97 owner references**, on the user's running **6.66 Rev 3 / UAC Atlantica**. The companion parser accepted that actual native snapshot. Example: `underwater_facility_arena_encounter_trigger_commit_1 → underwater_facility_arena_encounter_manager_dlc1_1`, field `owners[0]`. The probe invoked no engine methods and did not change or restart the game. Its output is a partial sampled graph; missing/unsupported types and scan limits remain explicit. Live game-callback metadata resolution and crosshair following with the updated DLL still need acceptance after loading it. The earlier UI preview uses synthetic relationships.

The retail probe accepts `RetailProfileProbe <PID> --graph-only <type-address-file> <output-directory>`. The versioned type-address file must belong to that PID and supply copied reflection addresses; it is private diagnostic input, not a release asset. `dotnet run --project Tests/Launcher/LauncherTests.csproj -c Release -- --graph-snapshot <snapshot.tsv>` checks the native-to-companion protocol against an actual captured graph without touching the running companion's files.
