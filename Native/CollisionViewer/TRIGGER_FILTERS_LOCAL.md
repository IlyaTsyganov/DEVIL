# Trigger class and path visibility

Open **Trigger types…** beside the main **Triggers** checkbox in DEViL.

- Root checkboxes control exact classes. `idTrigger` does not include `idTrigger_Teleporter` or other derived classes.
- Expand a class to see folders from trigger instance names **and entity-definition names**. For example, `idTrigger → trigger/ → props/` controls both `trigger/props/pickup_8469` and a map-renamed `pickup_123` whose definition is `trigger/props/pickup`. Either matching path can hide an object.
- Deeper folders are nested beneath their parent. Hiding `trigger/props/` also hides `trigger/props/weapons/`, but does not hide `trigger/props_extra/`. The same folder under another class has a separate preference.
- A class or ancestor-folder switch overrides descendants without clearing their saved choices. Checked children under a hidden ancestor are grey. Flat names have no synthetic directory category. An object with no readable directory in either source follows only its class switch. Old `@no_path` preferences are discarded.
- Search matches technical class names, descriptions and full folder paths. **Show listed / Hide listed** affect directly matching items; search-only ancestors are not changed. Search `trigger/props/` to change that branch without changing the entire class or its interact branch.
- Choices save immediately and apply to the loaded viewer within its 250 ms settings interval. Newly discovered classes/folders default to visible, subject to existing ancestor rules. The first use of the new native code requires one full game restart through the updated companion.

## Bundled catalogue

The class catalogue contains 37 concrete `idTrigger` / `idEncounterTrigger_*` classes from meathook reflection. The embedded `Assets/trigger-paths.tsv` adds **24 exact class/folder pairs across 17 classes**, independently of which map is open. Ancestor folders needed to display the tree are added automatically. Classes without a directory remain class-only until a live path is discovered.

The path catalogue was generated on 2026-10-27 from the user's installed **6.66 Rev 3**, including base, campaign, DLC, Horde, multiplayer, tutorial and patch resources:

- 109 `.resources` containers; 18,782 entity-definition entries / 5,802 unique definition names.
- Inheritance resolved for every definition; 107 definitions belong to the recognized trigger classes, with no ambiguous class assignments.
- 100 map `.entities` files checked, containing 26,353 trigger instances. Their instance names added no further directory groups.
- Examples beyond props/interact: `trigger/lootdrop/`, `trigger/props/weapons/`, `trigger/ai/`, `trigger/stat_event/`, `encounter/trigger/`, `encounter/combat_role/`, `interact/vault/` and `func/`.

This covers the recognized trigger definitions in that installation. Other game versions and mods can add paths; discovery merges them while the dialog is open. The catalogue describes name directories, not script behavior or class inheritance. Leaf definition names are not separate directory categories.

The read-only scanner used to generate it is `tools/scan_trigger_resources.py`. On Windows with Python, run:

```powershell
python Native/CollisionViewer/tools/scan_trigger_resources.py '<game directory>' '<audit output directory>' --catalogue --maps
```

It reads the installed resource containers and uses the game's installed Oodle decoder without modifying game files. Audit output contains resource metadata and class/folder lists, not full extracted game content. Copy the resulting `trigger-paths.tsv` to `Assets/trigger-paths.tsv` and rebuild the companion. The archive layout was checked against [EternalResourceExtractor](https://github.com/brunoanc/EternalResourceExtractor/blob/main/extract.cpp); no extractor executable or game codec is bundled.

## Reading and storage

Settings use `HiddenTriggerClasses` and `HiddenTriggerPaths` in the companion's per-user JSON. `viewer.ini` exports disabled classes as `ClassName=0` in `[TriggerClasses]` and folders as `ClassName|trigger/props/=0` in `[TriggerPaths]`. Omitted items remain visible. Separators/case normalize; invalid paths and settings separators are rejected.

The scanner atomically writes `trigger-classes.txt` and bounded, versioned `trigger-paths.tsv` in the per-user DEViL directory. The latter contains class/folder pairs, never addresses or full instance names. Previous discoveries and saved hidden paths remain available on other maps. Discovery and history tracking continue for hidden objects.

The existing meathook game-frame callback resolves `idEntity::name` / `idStr::data`, plus `idEntity::entityDef` / `idResource::name` / `idAtomicString::str`. It publishes one immutable validated layout to the scanner. The scanner copies bounded strings and does not call engine methods. Missing definition metadata does not prevent reading a valid instance-name path. A compatible loaded meathook is required for paths; the Volume inspector HUD checkbox need not be enabled. Class filtering works without path metadata.

Both sources' copied folders survive historical snapshots. The combined class/path rule controls filled surfaces, outlines and inspector picking. The main Triggers, distance and disabled/history switches still apply. Filters do not disable game entities or change encounter logic; world collision and invisible walls are unaffected.

This filters only classes already recognized by the viewer. Differently named volume classes such as `idEnvironmentalDamage_Hurt_Trigger` are outside this catalogue.

## Verification, 2026-10-27

All eight native suites passed, including normalization, retired no-directory rules, valid/invalid reflected layouts, either-source matching, folder boundaries, exact classes, fill/outlines/picking, history and scanner discovery on all 22 executable-profile fixtures. Managed checks cover embedded catalogue availability, legacy preference cleanup, persistence and validation. WinForms checks cover resource-only groups, removed synthetic entries, search, retained children, filtered bulk operations and live discovery.

On the user's running 6.66 Rev 3 / UAC Atlantica, the production read-only probe resolved instance pointer offset `0x48`, entityDef pointer offset `0xa8` and definition-name pointer offset `0x8` from reflection. It copied 552 instance folders and 624 definition folders, including 72 trigger meshes with flat instance names. It verified all 501 props meshes hidden, all 51 interact meshes retained, and filtering of all 72 flat-name objects by their definition folders. No game memory, commands or settings were changed.

All 19 standard isolated Vulkan cases passed, covering depth, console clipping and presentation. Integrated callback/rendering acceptance still requires launching with the new DLL. Reuse the already installed compatible meathook.
