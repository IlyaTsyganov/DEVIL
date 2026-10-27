# Level state: before / after — local development build

Proposal item 6 compares observed state in one loaded map instance. Open **Level state: before / after…** in DEViL. Keep the companion open and Collision Viewer enabled; load this build through a full game restart.

1. Load a level and press **Capture BEFORE**. Wait for the captured-volume count.
2. Return to the game and perform the action being investigated: pick up a prop, interact, finish an encounter or cross a volume.
3. Press **Capture AFTER**. The table reports observed changes and unavailable data separately. Search by object name, class, ID, change or sampled value.
4. Select a volume row to show its captured outline in the game, or enable **Highlight all volume changes** (up to 256 identities). Return to gameplay to see it. The native viewer's menu/console hiding, frustum and depth occlusion still apply. **Ignore view distance for highlights** bypasses the ordinary distance limit, not depth occlusion.

**Before outlines** are dashed cyan; identities missing from the After table are red. **After outlines** are green, disabled pink, removed red or unavailable gray. The switches allow each capture to be inspected independently when outlines overlap. These are the captured locations, including for moved/deleted volumes; they do not follow the current object. If exact geometry is unavailable or exceeds the draw budget, a world-axis bounding box is used and reported in the status line. Selecting manager/checkpoint rows shows fields without inventing a drawable volume. No camera movement, teleportation or restoration of game state occurs.

**Capture BEFORE** starts a new comparison and clears the previous After. **Save pair… / Open pair…** preserve the sampled fields and experiment note as local JSON. **Export changes…** saves all comparison rows as CSV, independently of the search. Full outlines stay in the native game's two-capture cache, not in JSON. A saved pair can be studied later, but highlighting requires those original cached captures and the same loaded map instance; recapture after a game restart or cache replacement.

## What is compared

| Source | Observations and limits |
| --- | --- |
| Recognized trigger/playerclip identities | Added/removed identity, enabled/disabled collision, raw contents flags, physics transform, bounds and decoded local-triangle fingerprint. Fingerprints can change because of coordinate, topology or triangle-order changes; they are not a canonical shape comparison. |
| Complete entity membership | ID plus generation distinguishes a missing identity from an existing object whose geometry/type/state could not be read. Slot reuse is a separate removal/addition. A newly recognizable volume that already existed is **Observation recovered**, not **Added**. |
| Encounter managers | Raw `thinkFlags`, enable/pause/waiting flags, current script index and command count. Updating is not proof that combat began or ended. Unknown values (`?` / `-1` for unavailable flags/counts) remain explicit. |
| Loaded checkpoint | Reflected `idGameSpawnInfo.checkpointName`, anchored to its matching map name and validated inline string storage. This is the name in the current spawn info, **not confirmation that a new checkpoint/save was written**. Missing metadata or an invalid string is unavailable, not an empty checkpoint. |

Capture ignores the ordinary trigger class/path visibility filters and includes disabled triggers when recognizable. A generic disabled dynamic prop is only treated as a wall if playerclip contents or retained observations identify it as one; the tool cannot recover an unobserved disabled wall's unknown past. Static world collision is not enumerated as mutable entities. Individual unavailable reads never establish script events, collision activation, deletion or a checkpoint change.

Both captures must be ready, chronological and from the same process, executable profile, map name and map/world identity. A level reload invalidates the pair even if entity IDs are reused. This is background polling, not an atomic simulation-frame snapshot or a full save game; changes between reads or transient events may be missed. Entity membership is reread at the end to reject changed tables. Moving/animated geometry can also create comparison rows unrelated to the experimental action.

## Implementation and bounds

- A PID-scoped, unique-token request triggers an on-demand background read. No continuous full-level scan is added to each rendered frame. The worker polls requests every 200 ms; requests expire after 10 seconds. Engine methods are not called to obtain snapshots, and no game memory is modified.
- Existing audited entity/physics readers decode volume geometry. Reflection supplied by the already-loaded compatible meathook resolves manager/name/checkpoint fields on its game-frame callback. Snapshot reading remains on the background worker. Missing meathook metadata is visible as unavailable sources.
- Limits: 32,768 membership rows, 8,192 recognized volumes, 2,048 managers; retained exact geometry up to 1.5 million vertices per capture, then bounding outlines. Highlight requests expire after three seconds and are renewed while the comparison window stays open. Native requests require both retained capture tokens; the viewer does not accept arbitrary geometry from JSON.
- Highlight preparation limits exact wire geometry to 300,000 source vertices, then uses bounds for the remaining selected identities. Captured geometry is immutable/shared and survives ordinary short volume-history expiry. Unavailable geometry cannot be reconstructed from the saved fields alone.
- Local files under `%LOCALAPPDATA%\DEViL`: `level-state-<PID>.request`, `level-state-<PID>-before.tsv`, `level-state-<PID>-after.tsv`, `state-highlight-<PID>.request` and `.status`. Closing the comparison window clears its highlight request. Closing the companion stops native work through the existing presence check.

## Verification

The native level-state suite covers all 22 supported profile fixtures: complete membership, enabled/disabled/deleted/unreadable state, generation reuse, anchored checkpoint-string validation, retained deleted outlines, distance bypass, expiry, map/world rejection and TSV publication. Managed tests cover the actual protocol, comparison semantics, strict scope, malformed input, JSON save/load and token requests. WinForms tests exercise Before/After, summary, search, single/all selection, native-cache absence, process/token/map changes, reset and minimum-size layout; the rendered change examples are synthetic.

A read-only production-reader probe on **6.66 Rev 3 / UAC Atlantica** captured **9,659 entity identities, 1,039 recognized volumes, 36 encounter managers and loaded checkpoint `cp_01a`**, with zero reported unavailable reads, in about **203 ms**. The managed parser accepted that native output. This validates the reading/serialization path; game HUD visibility and integrated highlight appearance still need acceptance after loading the new DLL.

All ten native suites, managed checks, WinForms interaction/layout checks and 19 isolated GPU scenarios passed for the combined build. These GPU cases cover the existing overlay/presentation path; the new captured outlines also have CPU vertex-preparation tests. They do not substitute for live visual acceptance of the new outlines in the game.
