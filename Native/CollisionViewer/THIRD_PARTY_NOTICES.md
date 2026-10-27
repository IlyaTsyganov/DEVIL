# Third-party dependencies

- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers), The Khronos Group. Build headers are included at `third_party/Vulkan-Headers`. License details and notices are retained there (Apache-2.0 / MIT, as specified per file).
- [DoomEternal-v1.1--RTXFix](https://github.com/billymooreq/DoomEternal-v1.1--RTXFix), copyright (c) 2026 billymooreq, MIT. `src/lighting_fix.h` adapts the default `on` mode's Vulkan 1.x indirect-barrier correction from commit `0bf1f44b5f4945eb7e6b76a204b4f6f2a074e23e`. The full license is retained in [licenses/doomfix-MIT.txt](licenses/doomfix-MIT.txt). DEViL handles launch selection and uses its existing layer; the upstream installer and launcher are not included.

Game executables, SDK executables, extracted game resources, Havok assets, scene snapshots, and process-memory dumps are not included.

The optional local inspector interoperates with [meathook's public native API](https://github.com/brongo/m3337ho0o0ok/blob/master/m34thook/mh_native_api.hpp). No meathook binary or implementation is bundled. Encounter-name annotations are extracted from the user-supplied community reference; attribution and limits are recorded in [INSPECTOR_LOCAL.md](INSPECTOR_LOCAL.md).

The local `lab_` command bridge also documents the command-system ABI from meathook's `cmdsystem.hpp` / `cmdsystem.cpp` and validates it against the installed v7.2 API and engine RTTI. It uses the engine's registration method; no meathook implementation or game executable is redistributed. See [research prototype instructions](RESEARCH_LOCAL.md).
