# Research timeline, player path and lab_ controls — experimental features

Open **Research timeline & path…** in DEViL. Start the game through the updated local companion once so the new native DLL is loaded.

## Manual recording

- **Start recording / Stop recording:** recording is explicitly controlled by the user. It is scoped to the selected running game PID, so a new game process cannot inherit an active recording accidentally.
- **Show path in game:** controls drawing independently of recording. Stop retains the recorded line. The viewer's main enable switch, distance limit, depth/console handling and menu hiding still apply.
- **New session:** clears the displayed path and live journal view. Prior session files remain in the per-user research directory.
- **Add marker:** records a user note at the sampled position. It can be used to annotate a checkpoint, fight or route attempt.
- **Export CSV…:** exports the full retained session journal, including position samples. Searching the table or hiding sample rows does not restrict the export.

The route follows the player's physics-body origin, rather than the render camera. It samples at about 10 Hz while gameplay is visible and retains a new point after at least 0.05 game units of movement. The line changes from blue to pink from earlier to later points. Pauses, unreadable positions, gaps longer than one second, and position changes greater than 50 game units break the line. This identifies sampling discontinuities; it does not claim every break is a teleporter. A map/world change clears the displayed line, while the journal retains the level-change event and earlier samples. Closing DEViL normally stops recording.

The in-game line keeps at most 12,000 points from the current map; the UI keeps the most recent 16,000 journal rows. A separate append-only `.full.tsv` journal preserves earlier rows for CSV export. The short status and live snapshot are published atomically about once per second; the full journal is flushed on that interval and on stop. An abnormal game exit can lose the final buffered second. Files stay local under `%LOCALAPPDATA%\DEViL\research`.

## What the timeline actually observes

The journal contains recording boundaries, gameplay sampling pauses/resumes, map changes, player positions, manual markers, newly observed volumes and changes in volume collision state. It records the sampled position, class, map ID and generation. Initial map discovery creates a baseline instead of claiming all volumes just activated. Hidden trigger classes are still observed by the background scanner.

`Enabled -> Disabled` is a collision-state observation, not proof that an encounter script executed. This version does not automatically report encounter completion, user-flag delivery or checkpoint creation. Checkpoints and other gameplay outcomes can be annotated with manual markers until independently validated event sources are added. The timestamp measures elapsed wall-clock time within the recording session, including pauses and stopped intervals; it is not the game's internal timer. The native observations do not currently resolve entity/encounter display names in the journal.

## Console controls

All commands use the `lab_` prefix and require the running DEViL companion with the same game executable selected. The companion receives validated requests over a same-user named pipe and applies them through the same settings/UI as mouse input. The native callback never evaluates arbitrary shell commands or forwards user text as engine commands.

Persistent state now uses real engine cvars; one-shot operations use action commands. The full current reference is in [active encounters and console controls](ACTIVE_ENCOUNTERS_LOCAL.md#cvars-versus-commands). Previous short command names remain compatibility aliases.

| Control | Action |
| --- | --- |
| `lab_commands_list` | Print state cvars and action commands. |
| `lab_path_show 0/1` | Recorded-path visibility cvar; no argument queries the value. |
| `lab_recording_start` | Start/resume manual recording. |
| `lab_recording_stop` | Stop recording, retaining the displayed path. |
| `lab_recording_clear` | Start a new research session. |
| `lab_markers_add <text>` | Add a manual marker. |
| `lab_trigger_set <class> 0/1` | Change one exact trigger class's visibility. |

Examples: `lab_recording_start`, `lab_markers_add before arena`, `lab_trigger_set idEncounterTrigger_AmbientAIAudio 0`, `lab_recording_stop`.

Meathook is required for console registration and the existing native HUD, but not for route recording/drawing or the timeline UI. The public v7.2 API does not expose command registration directly. DEViL recognizes its audited ExecuteCommandText thunk, validates the engine command-system object's MSVC RTTI and executable method addresses, and registers commands on a meathook game-frame callback. Unknown implementations withhold command registration; GUI functions remain available. Command callbacks and their modules are pinned for the game lifetime. After DEViL closes, command names may remain in console completion, but handlers refuse execution and no settings requests are accepted.

## Validation

- Manual/PID-scoped recording, stopped-path retention, pause/large-jump separation, map changes, markers, bounded snapshots and archive flushing are covered by native research tests.
- Player-origin readers are checked against all 22 executable-profile fixtures. Vertex tests verify route segments and reject cross-map or hidden paths.
- `lab_` argument validation, incorrect meathook thunk/RTTI rejection, companion-presence lifetime and actual named-pipe transport are tested without calling engine registration in fixtures.
- Managed checks cover settings, parsing, partial rows, full CSV export, directory boundaries, record/stop/show/new-session buttons and both UI layouts.
- All 19 isolated Vulkan GPU cases passed on the final native build, including depth occlusion, reversed depth, console clipping and split-queue presentation.
- A read-only production probe can validate player position and command-system ABI against the running 6.66 Rev 3. It does not register commands or inject the new DLL.
- On 2026-10-27 that production probe passed on the user's 6.66 Rev 3 / UAC Atlantica: the player-body origin was readable and the installed meathook's engine command-system ABI passed validation. The existing game process still needs a new launch to load this viewer build.

Included in DEViL 0.7. Visual route acceptance and executing the newly registered commands require a complete game launch through the updated DEViL; these have not yet been accepted in-game with this new layer.
