# Research points and practice saves — proposal item 7

Open **Research points…** for observations or **Сохранения…** for checkpoint loading. Keep DEViL running; installing a changed native viewer requires a game restart, while the simplified save UI uses the same native DLL as the accepted live-restore build. A research point records an observation; a practice archive preserves files from a normal game checkpoint slot.

## Research points

Enter a name, category and route/hypothesis note, then press **Capture new point** in a loaded level. A point is saved locally with:

- Map name, executable profile, capture time, process and original map/world identity.
- Physical player origin, rendered camera eye and centre-screen aim direction, plus the full view-projection matrix. Body origin is not replaced by camera position.
- Loaded spawn-info checkpoint name or an explicit unavailable status. It does not prove that a new save was written.
- The recently aimed inspector volume, when available, with name, class, ID/generation and current collision state. **Attach recent aim** adds another volume while preserving the point's saved position. Enable the inspector, aim and attach within ten seconds; the recorded aim age is displayed. Attachments must belong to the original process/map instance, up to 32 identities.
- Your editable name/category/note and an optional link to a separately verified practice archive.

Use **Save edits**, search and JSON **Import point… / Export point…** to maintain the catalogue. Categories are free-form: the displayed examples are names, not prebuilt inventory/route configurations. Add as many category/level/trick points as needed; the current list loads up to 10,000 files and never deletes older points automatically.

**Copy position / aim** copies both physical and rendered values. **Copy return command** copies numerical `setviewpos x y z yaw pitch` using the captured camera eye/aim; it does not execute a command. Use it manually on the matching map/build with the necessary console access and test it in a practice slot. Camera placement, current velocity, crouch/posture and intervening script state can affect the result. **Copy spawn info** produces the same style of camera-eye/yaw/pitch-derived spawn data as meathook's `mh_spawninfo` workflow, with roll zero; it is not a full actor pose restoration. Cutscene/spectator cameras remain rendered camera observations.

A position return does not restore enemies, their AI, pickups, unlocks, cooldowns, encounters, scripts or checkpoint progress. Capturing is polling, not an atomic simulation-frame capture; movement over 0.1 units during the guarded read rejects the capture. A pause can retain the last rendered camera view. Saved entity IDs/generations are historical bindings to the original map instance, not live references that survive a reload.

## Practice archive library

The latest development experiment also provides live checkpoint archival, current-checkpoint reload and **Restore archive + reload…**, restricted to native live-copy archives for TAG1 slot 3 on 6.66 Rev 3. See [live experiment status and test instructions](LIVE_PRACTICE_LOCAL.md). Archival and repeated restoration between earlier/later checkpoints passed retail acceptance without restarting game/Steam. The offline workflow below remains available.

The main **Сохранения…** window provides simple live **Сохранить / Загрузить** controls ([instructions](LIVE_PRACTICE_LOCAL.md)). For the offline workflow below, open **Дополнительно… → Инструменты и диагностика…**. Select `Steam/userdata/<account>/782330/remote` and the campaign slot. **Find** discovers Steam locations; **Browse…** supports an explicit location. Campaign slots 0–2, TAG1 slots 3–5 and TAG2 slots 6–8 use their actual folder names.

1. Close the game and Steam, including Steam's background client processes.
2. Enter an archive name/category and optional map tag. These are annotations, not inferred checkpoint contents.
3. **Inspect slot** checks the four recognized containers. **Archive full slot** retains every regular top-level slot file, including **both `-BACKUP` files**, writes per-file SHA-256 metadata and verifies the new archive. It does not request a new save or capture the current game simulation.
4. Select a library row and **Verify archive**, **Export folder…**, or **Import folder…**. Import and restore require the same Steam account as the selected save location. Account changes/re-signing and campaign-family mixing are blocked.
5. **Link to research point** associates the selected verified archive with the point. The archive's checkpoint may precede the point's precise location/time. Linking does not load anything.

External archives are independent of the game's three slots per campaign. To use a library entry, restore it into an already occupied slot and load that slot normally in the game. The library loads metadata for up to 10,000 entries without automatic deletion; disk space and explicit per-file limits still apply. This is not an arbitrary mid-frame RAM snapshot or a replacement game serializer.

## Restoring a full archive

Close the game and Steam. Disable DOOM Eternal's Steam Cloud setting yourself, then check the explicit Cloud confirmation in this window. DEViL does not change Steam settings. Select a same-account archive and a matching campaign-family **occupied** target slot, then choose **Review restore…**. The review shows the exact target and archive before the final replacement.

The operation revalidates source hashes/authentication and the target fingerprint after review. It stages the complete replacement, preserves the raw current slot, copies `PROFILE` and the Steam app's flat metadata into recovery evidence, and retains the original directory outside `remote`. It does not replace/delete `PROFILE`, `remotecache.vdf` or the numbered Steam metadata files. Failure between directory renames attempts to return the original directory immediately. A detected game/Steam start during the operation aborts and rolls back the rename when possible.

After success, the window provides the original directory and evidence paths. Those copies are retained; there is no automatic cleanup. The archive still represents the game's chosen checkpoint state. Loading it can reconstruct what the normal checkpoint serializer stored, not everything that was live when a research point was captured.

Forced process/PC termination between the two directory renames is not automatically repaired on startup. Recovery evidence contains `restore-plan.json` and **`RESTORE_PATHS.txt`** written before the rename. If the target folder is missing, leave the game/Steam closed and recover the retained original directory to the exact target path; do not merge files from different copies. Staging/original directories are direct children of the Steam app folder, outside `remote`, so they are not presented as extra game slots. Keep the evidence until the result has been checked in the game.

## Existing save problems

| Problem | What this build does |
| --- | --- |
| Reset Mission returns to the old slot's state | Archives/restores the entire set including both internal backups. It cannot retrospectively invent missing matching backup data. An internal backup can legitimately represent an earlier mission/checkpoint than the current primary. |
| Current primary container missing or fails authentication, both internal backups authenticate | **Recover backup pair…** prepares a separate archive from the two backups, keeping raw originals as evidence. No live slot is changed. Latest primary progress is lost; verify the recovered state in-game before relying on it. |
| File bytes changed after archive/review | Checksum or target-fingerprint mismatch rejects the operation before replacement. |
| A different Steam account's save | Import/restore are blocked. The owner is never changed and no account re-signing interface exists. |
| Empty save list, Steam index/profile problem, authentication failure on every file | Report the observed failure and retain originals. There is no automatic index/profile clearing or promise that slot files alone repair it. |
| Corrupt serialized gameplay or no usable backups | Container authentication does not validate game/script semantics or recreate missing data. Use a known-good same-account full archive; universal binary repair is not implemented. |

Recovery verifies AES-GCM using the original account, game code and exact filename. A backup's ciphertext cannot simply be renamed to a primary: the filename is part of the authenticated data. The recovery archive decrypts each validated backup and re-encrypts its unchanged payload for the corresponding primary name **under the same account**. Both original backups remain in the archive. These are supported Steam containers only; Bethesda/GOG/Xbox layouts and unknown filename/format variants are not guessed. Authentication failure can mean wrong account, corruption or an unsupported format; it does not by itself diagnose which.

Unknown nested slot/profile folders, reparse-point/junction paths, more than 256 slot files, files over 64 MB or sets over 128 MB fail explicitly. Offline archive/restore operations deny concurrent writers. Read-only inspection shares access with writers so it does not block autosaving; a detected file change is rejected, and an inspection while the game is running is only a container observation. Close the game/Steam and repeat before diagnosing a failed read. The program does not start/stop Steam, change firewall/Cloud settings, execute category notes, or modify the game's live memory.

## Storage, verification and further work

`%LOCALAPPDATA%\DEViL\research-points/<id>.json` stores points. `practice-saves/<id>/manifest.json` plus `slot/` stores archived checkpoint files. `practice-saves/recovery-evidence/` retains originals used to prepare recovered archives; `restore-evidence/` retains before-restore files, profile/Steam copies and exact paths. Point JSON and archive folders are exported/imported separately. Imported archives receive a new local ID, so re-link points after importing both. No real/user save files are bundled in a DEViL release.

Native tests cover guarded point reads, distinct body/camera/aim, selected-entity scope and invalid views on all 22 profile fixtures. Managed tests cover points and full archives, account/family/process/Cloud guards, all four authenticated files, corruption and path-tamper rejection, backup-pair reconstruction, target changes, failed-rename and process-start rollback, preservation of `PROFILE`/Steam metadata, and import/export. WinForms checks exercise capture, edits, attachments, search, archive/verify/link controls, blocked restores and minimum layouts; previews use synthetic data.

A read-only production probe on 6.66 Rev 3/UAC Atlantica captured the current body, camera eye, aim and checkpoint `01a_inside_building_a`; the managed parser accepted it. Read-only inspection of an existing Steam slot authenticated all four real primary/backup containers. No real save was replaced, repaired or loaded during development. Actual in-game acceptance of returned positions and restored/recovered archives still requires an expendable same-account practice slot. All ten native suites, managed/UI checks and 19 isolated GPU cases passed on the combined build.

True arbitrary savestates with live AI, cooldowns, pickups and all script state would require investigating and validating the engine's own serialization/restore path and its side effects. The present observed-entity snapshots cannot supply that completeness. A new hidden-stat HUD (enemy health, fast-dblade eligibility, temporary-state timers) is also separate work: each value and its meaning needs to be verified. Existing Movement lab velocity/support telemetry remains available.

Sources used to establish the current workflow/format:

- [Community save-management guide](https://docs.google.com/document/d/1V2LA0XIt7bjm1VX_p3aHk22Z0oUWTCk6pud1pUnxW7k/edit): occupied slots, full backups, Reset Mission behavior and practice categories.
- [Community troubleshooting FAQ](https://docs.google.com/document/d/13xX4mG9_gzWdSXrEIvh5pzzQAh3tl7__TUzne41OJag/edit): profile/index symptoms. Its deletion-based procedures are not performed automatically by DEViL.
- [Hg.SaveHistory changelog](https://github.com/HgAlexx/Hg.SaveHistory): version 0.7.5 removed the filter that omitted internal backup files.
- [DOOMSaveManager container framing](https://github.com/GoobyCorp/DOOMSaveManager/blob/master/Crypto.cs) and [filename/account context](https://github.com/GoobyCorp/DOOMSaveManager/blob/master/DoomEternalSavePath.cs). DEViL uses its own .NET AES-GCM implementation; no source or binary from that project is incorporated.
