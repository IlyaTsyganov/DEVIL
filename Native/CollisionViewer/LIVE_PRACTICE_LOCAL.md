# Live practice checkpoints

Open **Сохранения…** directly in the main window. Live operations are restricted to **DOOM Eternal 6.66 Rev 3, TAG1, slot 3 (`DLC1-AUTOSAVE5`)** and the active Steam account. Cross-account imports, restores and re-signing remain blocked.

## Initial installation and use

1. First installation of the live native layer requires one game restart. If already using **DEVIL-live-practice-restore**, update only the companion: its native DLL is unchanged. Steam and the game can stay open. For later launches, enable DESRU's outbound block for the selected EXE: the new companion verifies the firewall before launching.
2. Load TAG1 slot 3, wait for autosaving and pause. Open **Сохранения…**. The Steam folder is chosen automatically from the current runtime owner, active Steam account or a single discovered folder. An ambiguous folder can be selected in **Дополнительно…**. Cloud is checked by the native bridge on every operation; either a global or per-game disable qualifies.
3. Optionally enter a name and click **Сохранить**. A blank name gets a timestamp. It archives the last completed disk checkpoint, including both backups. The new archive is verified and selected automatically. Native SHA-256/AES-GCM and Steam/disk consistency checks precede the copy; the companion independently authenticates it.
4. Select an entry and click **Загрузить**. No intermediate confirmation is needed for this already selected training slot. The operation pins source/target hashes, retains all four originals, installs through Steam storage, verifies cache/disk and asks the engine to load the primary checkpoint. The main list shows compatible live archives from the selected account only.
5. At 100% on the game's loading screen, **press Space in the game**, wait for the level, then pause again. The companion waits up to five minutes for loading and reports the observed loaded map/checkpoint. A queued load alone is not completion.

The initial restart installs code. Archiving and subsequent restores run without closing the game or Steam. Technical tools, offline archives, import/export, backup recovery and **Test current checkpoint reload…** are available through **Дополнительно… → Инструменты и диагностика…**. Offline **Review restore…** and recovery retain their closed-process guards. Existing library folders are reused without migration. Detailed error/recovery paths remain visible in settings.

The firewall gate reads `HNetCfg.FwPolicy2` immediately before DEViL registers its layer or starts the selected EXE. It requires an enabled outbound Block rule for the exact path, all protocols/addresses/interfaces and every active profile, with firewall enabled and local policy effective. Separate matching profile rules may jointly cover active profiles. Unknown policy state denies launch. No firewall rule or setting is written; Steam/manual launches are outside this gate.

## Transaction and account checks

The native frame callback validates the master local user, current slot/index, TAG1 map/player, pause flag, pending saves, map-change request and asynchronous save/load tasks. It requires 500 ms of observed idle state and rechecks before mutation. Engine calls are restricted to the audited executable profile and five exact code signatures.

Steam identity comes from the existing `SteamUser020` interface. The `userdata` folder must belong to the same Steam64 ID. Both `IsCloudEnabledForAccount` and `IsCloudEnabledForApp` must be available. Synchronization is enabled only when **both** are true. Unknown values block the request; diagnostics show both raw flags. No Cloud settings or Steam initialization state are changed. See [the Steam two-setting contract](https://partner.steamgames.com/doc/api/ISteamRemoteStorage#IsCloudEnabledForAccount).

The managed review pins the archive manifest and all four target files. Staging copies the incoming files to a private PID/token-scoped bundle with all four source and target lengths/SHA-256 values. The native parser independently validates that plan and authenticates all eight containers against the current account and exact filenames through Windows CNG. There is no native re-signing/encryption operation.

Before a write, Steam's `FileRead`/`GetFileSize` view must equal the selected disk checkpoint. All four originals are copied and flushed to `782330/.devil-live-original-<token>/` outside `remote`; the bundle retains `RESTORE_PATHS.txt`. The bridge writes only the four fixed `DLC1-AUTOSAVE5/…` names using synchronous `ISteamRemoteStorage::FileWrite`, then verifies **both Steam reads and disk reads** against the reviewed incoming bytes. Steam maintains its own slot-file metadata. DEViL does not directly replace `PROFILE`, Steam indexes or other campaign slots. See [Steam file API semantics](https://partner.steamgames.com/doc/api/ISteamRemoteStorage#FileWrite).

If a write or verification fails, all four originals are written back through the same API and checked against disk/cache. If rollback cannot be verified, the operation reports the retained recovery location and does not request an engine load. After an engine load has been dispatched, files are not automatically rolled back beneath that asynchronous load.

The four-file update is not crash-atomic. After process/PC interruption during writing, keep game/Steam closed and restore **all four** files together from the retained original copy; do not mix primary/backup pairs or reset `PROFILE`. Original copies and private staging data are retained without automatic deletion.

## State and limitations

Only four standard files and native live-copy archives marked for this profile/slot are supported by this first restore experiment. Other builds/slots and offline-imported or recovered archives use the established offline path. Authentication verifies container integrity/ownership, not arbitrary script/progression consistency.

These archives represent the game's serialized checkpoint. They do not capture arbitrary current enemies, AI, cooldowns or movement between checkpoints. A loaded checkpoint name is an observation, not by itself proof of a new autosave. Category/map labels remain user annotations.

## Validation status

Actual retail validation already succeeded for native archival and primary checkpoint reload in the same running game/Steam processes. The runtime flags were account=0/app=1, correctly allowing globally disabled Cloud. Verified archives were captured at `cp_01a` and later `01a_inside_building_a`. The engine reported a real map transition and returned to `cp_01a`; the user confirmed the loaded level. The old 75-second GUI wait expired while awaiting loading-screen input; the new wait/prompt addresses that false failure.

Automated checks cover all Cloud flag combinations, wrong owner/slot/profile/map/user, pause and writer queues, independent .NET/CNG crypto fixtures, source/target hash pinning, Steam-cache mismatch, partial-write rollback, post-write verification failure, unchanged originals after rejected writes, retained recovery paths, and untouched profile/index sentinels. Managed and UI checks cover review/staging scope, stale/tampered archives, unchanged offline guards and minimum layout. A real staged checkpoint plan also passed the production native parser, SHA-256 and CNG authentication without changing the game slot.

**Live archive restoration passed retail acceptance:** Steam reads matched disk bytes before mutation; the later checkpoint was restored to `cp_01a`, then the later archive restored `01a_inside_building_a`, without restarting the game or Steam between those operations. The user confirmed both results. Physical player origins returned to the corresponding earlier/later values. The retained four-file original matched the verified later archive byte for byte; all 16 existing files in other slots still matched the pre-test backup.

The retail operations used the same production review/staging/protocol code as the buttons through the acceptance driver. The UI layout and guards were tested separately; native window capture was unavailable for physical button automation. Actual coverage remains the tested 6.66 Rev 3 / TAG1 slot 3 on UAC Atlantica. Other builds/slots and arbitrary RAM states are not covered by this live acceptance.

Audited engine chain, 6.66 Rev 3 RVAs: `RequestRestartMission 0x69e930 → dispatch 0x6682c0 → slot load 0x14877c0 → fresh holder 0x14847c0 / file preparation 0x148c100 → storage vtable +0x20`. Private load parameters are 192 bytes; the backup-selector byte at `0xb9` is false for the primary checkpoint. The mission-restart console command selects the older backup and is not used here.
