# Player collider and attempt comparison — proposal item 5

Launch through the updated companion once with Collision Viewer enabled to load the new native DLL. Open **Movement lab…** in the main window. Reuse the already installed compatible meathook. The modeless lab can stay open while playing.

## First comparison

1. Enable **Show player collider**. Cyan is the decoded convex core swept by its actual Havok radius, drawn as a tessellated rounded outline. Yellow is a bounds-only fallback when the shape cannot be verified; the status explicitly says so. The player physics body supplies position, orientation and dimensions, independently of the render camera.
2. Enter a name and press **Start attempt**, then return to the game. Recording begins with the next gameplay sample, excluding buffered points from before Start. Press **Stop attempt** afterwards. Starting from a paused game is supported: the recording waits for gameplay.
3. Mark the selected attempt **Success**, **Failure** or **Unmarked**. These are your annotations, never an inferred trigger/encounter outcome. Repeat the route to record another attempt.
4. Check up to six attempts. **Show compared paths in game** overlays their world-space paths; the companion also plots XY, XZ or YZ. Success is green, failure red, and unmarked attempts use other colors. Separate attempts never connect even if they share a color.
5. Highlight one attempt as the reference and move the time slider. The table shows the nearest recorded sample within 75 ms: speed, planar speed, world Z, differences in Z/position/velocity, support trace, slope, target ID, distances, dimensions and shape index. No sample is invented across a gap or beyond a recorded attempt. The displayed sample time makes the offset visible. **Latest sample** follows an active recording.

## Collider rendering rate and cost

The collider pose now refreshes **on every presented game frame**. The renderer reads the actual player's current position, orientation, bounds, shape selection and support fields. It reuses the background decoder's rounded wire; it does not decode/tessellate Havok geometry, scan entities, measure target distances or write recording files per frame. This removes the previous 20 Hz stepping of the collider pose. It does not create additional physics ticks or interpolate from the camera.

Current map/player/physics identity, local/world bounds and shape selection are checked around this small read. A changed shape uses current bounds until the background decoder verifies its new rounded outline; old geometry is not stretched into another posture. Measurements stay at the sampled rate and keep their original origin instead of being attached to a newer pose.

The local fixture benchmark measured **0.364 microseconds per pose refresh** over 10,000 calls. This measures only the reader on fixture memory, not total rendering time or an in-game FPS comparison. The wire's line count is unchanged; a large new rendering cost is therefore not expected. `VulkanViewer.log` reports actual frame-reader average/maximum CPU microseconds and rejected reads every five seconds while the collider is shown. Full-game FPS still depends on the rest of the overlay and game workload.

## Support plane projections and angles

Open **Surface projections…** inside Movement lab. The separate modeless window shows **XY, XZ and YZ** simultaneously, independent of camera perspective:

- Cyan is the projected unit ground-trace normal.
- Orange is the intersection of the contact tangent plane with that world-axis plane through the contact point. Its signed angle is measured from the first positive axis (X for XY/XZ, Y for YZ); the line is directed toward that axis, or upward for an exactly vertical line.
- Overall surface slope is measured against gravity, independently of the world-axis cuts. The projected normal's bearing is labelled separately from the cut tangent's angle.
- A floor with normal `(0,0,1)` has 0° XZ/YZ cuts. Its tangent plane is coincident with XY, so there is no unique XY intersection line; no arbitrary 0° is reported there. Normal `(sqrt(0.5),0,sqrt(0.5))` gives a −45° XZ cut and 45° overall slope with ordinary downward gravity.

Choose live/last gameplay data or the highlighted attempt's selected-time sample. Paused/stale samples are identified and show age; missing contacts clear the angles. **Hold this sample** freezes a plane explicitly, including after stepping off an edge. **Show angles in game HUD** displays the numerical normal, gravity-relative slope and three cut angles in the existing meathook HUD. Its values use the 20 Hz observation stream, independently of the collider's presentation-rate pose.

The HUD block now uses `(360,300)` through y=385, beside the inspector. The earlier y=500 anchor could fall outside the debug-HUD coordinate area and make all its rows invisible. It automatically requests `com_debugHUD 1` once per visible interval. Keep DEViL open, close the console/menu and stand on a surface; airborne or missing support shows an explicit status instead of old angles. A changed layer requires a full game restart. `VulkanViewer.log` now reports `Support plane HUD` callback/submission counters, source state and anchor every five seconds while the switch is enabled, so missing callbacks can be distinguished from waiting data or text placement. The anchor fix is verified with a recording HUD backend; live visibility still requires game acceptance.

At an edge, a single observed normal determines a tangent plane, not a unique 3D tangent direction. These cuts do not identify the physical edge direction or the dihedral angle between neighboring mesh faces. The source remains the game's support trace, which can differ from a chosen visible mesh face.

## Difference from Research timeline & path

Both use the player's physics-body origin; neither substitutes the camera position. The tools record independently and their path visibility switches are independent. Showing both can overlay two traces of the same route.

| | Research timeline & path | Movement lab |
| --- | --- | --- |
| Purpose | One investigation session: route, markers and observed volume-state changes | Named attempts and numerical/visual comparisons |
| Recording rate | About 10 Hz; a new route point requires at least 0.05 units of motion | About 20 Hz; stationary samples retain velocity, dimensions and support observations |
| Data | Position, journal boundaries, markers, maps, volume discovery/state | Position, physical velocity, shape/bounds, support point/normal/gravity, selected-boundary measurements and user outcome |
| Displayed paths | One retained current-map session line, blue → pink by sample order | Up to six saved attempts, outcome colors, XY/XZ/YZ and time-selected comparison |
| Storage | Research session journal and full CSV | Individual attempt JSON files and complete attempt CSV |

The collider's new frame-rate pose does **not** change either recorder's sampling rate or turn either path into a frame-accurate trace.

**Align starts (plot)** translates only the companion plot. Recorded data, numerical differences and in-game overlays remain in absolute world coordinates. Only matching map names and executable profiles are compared/overlaid; a map reload can be compared with an earlier attempt on the same map. Process changes and map/world reloads end an active recording.

Attempts save automatically every four seconds, on stop and on normal close. The lab loads the newest 64 files, up to 200,000 retained samples in total; older files stay on disk. Each attempt holds at most 24,000 samples (about 20 minutes at the target rate), with a two-hour wall-clock limit including pauses. An unexpected process exit can lose the last buffered interval. **Export CSV…** exports every sample of the highlighted attempt, independent of the slider, plot projection or checkboxes.

## Ground contact and selected-volume measurements

The orange cross/line at the player displays the game's current support-trace point and normal. A support observation requires the current player ground-plane flag, a valid ground-trace fraction and a finite unit normal. Old ground traces are not reported while airborne. Slope is measured against the current gravity direction. These are sampled support observations, not a full side/ceiling contact manifold or a frame-exact landing log.

To measure a trigger or invisible wall, enable the existing **Volume inspector**, aim at its displayed geometry, and use **Follow aimed volume**. Uncheck it to hold the last aimed volume while moving away. Holding preserves its map-scoped entity/generation; current geometry is refreshed from the scanner. Removed, unreadable or historical-only targets supply no current distance.

- **Origin → boundary** is the shortest distance from the physics-body origin to the sampled target triangles. It is not the distance from the camera or the collider center.
- **Skin gap** is the distance between the solid convex player core and the target boundary, minus the player's convex radius. Positive values indicate a gap; zero is a sampled touch. Negative values mean the rounded body reaches/crosses the sampled boundary. This is not a penetration depth, inside/outside classification, trigger firing signal or proof of an engine collision response. Distance tests include target triangles lying entirely inside the convex core.
- Skin distance is withheld when the shape is unverified or a target would require over 50,000 triangle pairs; the origin distance can still be available. The full target ID/generation accompanies measurements so different targets are not silently treated as the same one.

## Reading and sampling

Meathook's existing game-frame callback resolves fields for `idEntity`, `idPhysics`, `idHavokPhysics_Base`, `idHavokPhysics_Player`, `playerPState_t`, `trace_t`, `contactInfo_t` and the Havok shape wrappers. An immutable layout goes to a separate background sampler. No game physics setter, event, collision query method, entity getter or memory write is invoked.

The sampler targets **20 Hz**, separately from the existing research timeline's roughly 10 Hz scan. It reads the player's physics-body origin, orientation, local/world bounds, physical linear velocity, active rigid-body shape index, gravity and support trace. The existing audited Havok convex decoder reads the shape core; the shared convex-radius member is accepted only if the expanded core matches the reflected local bounds. Shape/index/bounds changes and a one-second cache refresh rebuild the rounded wire. Unknown layouts produce unavailable data rather than guessed player geometry. Bounds must match the transformed local bounds; player/map/physics pointers, deletion state, origin and shape selection are checked around reads.

This is polling, not an atomic simulation-frame capture. It can miss short contacts or fast changes between samples. Velocity is read from the physics body, not derived from sparse positions. Timestamps are elapsed wall time from the first recorded sample, including pauses. Missing samples, gaps over 250 ms, large jumps over 50 game units and new recording intervals break the line. The companion preserves samples from a rolling 256-row native buffer; a longer UI interruption creates a new segment instead of joining missing data.

Native snapshots are atomically published about every 200 ms as `motion-<PID>.tsv` under `%LOCALAPPDATA%\DEViL`. Requests expire after four seconds. A checked player/overlay display can keep sampling active outside the lab. Closing the companion stops its native work through the existing presence check. Attempts are local JSON files under `attempts/`; the bounded `motion-overlays.tsv` contains checked compatible paths. Long in-game/plot paths are decimated for display; JSON and CSV keep the complete retained samples. Viewer enable/distance/depth, console clipping and menu hiding apply to in-game lines.

Visibility uses native cvars `lab_player_show 0/1`, `lab_attempts_show 0/1` and `lab_surface_show 0/1` with the existing audited cvar bridge. Their compatibility action aliases are `lab_player`, `lab_attempts` and `lab_surface`. Recording/result annotations remain manual lab controls and are independent of the old research session.

## Verification

Native movement tests exercise reflected layouts, true core-plus-radius extents, bounds fallback, physical velocity/support, invalid/torn values, airborne traces, target distances/intersections, native telemetry, PID-scoped overlays, separate route identities and actual line-vertex preparation on all 22 profile fixtures. Managed checks cover strict parsing, freshness, physical metrics, nearest-sample gaps, persistence, outcome labels, overlay map/identity scope, CSV and cvar transport. UI checks record buffered data, reject pre-start/duplicate samples, preserve pauses, save outcomes, stop on PID changes, exclude foreign maps and validate minimum-size controls. Rendered previews use synthetic test attempts.

On the user's running **6.66 Rev 3 / UAC Atlantica**, a read-only production probe decoded the current player as a 12-triangle convex core with radius **0.391050**, matching a **0.790 × 0.790 × 1.790** body. It read physical velocity and a valid ground-trace normal `(0, 0, 1)`. The probe changes no game memory or state. Other postures/game builds still require in-game acceptance after loading the updated DLL; fixture coverage is not retail validation of every executable or shape.

All nine native suites, managed checks, WinForms interaction/layout checks and 19 isolated GPU cases passed on the final native build. The production reader also copied 20 actual physics samples into the native telemetry format, which the companion parsed successfully. This validates the reading/data path; live in-game rendering, pose changes, recording and cvar use still need acceptance after the new layer is loaded.

The presentation-rate update adds 22-profile pose tests for movement between buffered samples, wire reuse, current-bounds fallback after a shape change and invalid-state rejection. Native/managed plane tests cover flat, tilted, vertical and coincident cuts, normal-sign invariance and rotated gravity. The HUD recording backend verifies show/hide, one-time visibility requests and removal of old angles when airborne. UI tests cover live/recorded sources, held samples, no-contact states, HUD settings and minimum-size projection captions; the rendered incline preview is synthetic. A new retail read-only probe on 6.66 Rev 3 confirmed that the frame reader returns the current body pose while reusing the decoded wire.
