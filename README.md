# TheOneGames

Public game and prototype collection consuming the separate [TheOne engine](https://github.com/traidy2222/TheOne).

```powershell
git clone https://github.com/traidy2222/TheOne.git
git clone https://github.com/traidy2222/TheOneGames.git
cd TheOne
git checkout (Get-Content ../TheOneGames/engine-revision.txt -Raw).Trim()
cd ../TheOneGames
```

`engine-revision.txt` records the engine commit used for the publication checks.
[Publication validation](docs/PUBLICATION_VALIDATION.md) records their scope;
[engine ownership review](https://github.com/traidy2222/TheOne/blob/main/docs/ENGINE_GAME_BOUNDARY_REVIEW.md)
explains what was promoted into the engine and what stays here.

[Reactive smoke/fire validation](docs/EFFECTS_VALIDATION.md) records the effects
lab's actual captures, numerical checks, measured costs and current fidelity limits.

Independent game projects consuming the external **TheOne** engine. This repository owns game executables, gameplay, game UI, assets, content conversion, saves and game tests. The engine checkout has no dependency on this repository.

```powershell
.\run.ps1 -Game Planets
.\run.ps1 -Game Sandbox
.\run.ps1 -Game Robots
.\games\robots\Play-Robots.ps1 -Level City
cmake --build build --config Release --target game_integration_tests
ctest --test-dir build -C Release -R '^game_integration$' --output-on-failure
```

Windows x64, Visual Studio 2022 C++, CMake and the Vulkan SDK are required. `THEONE_ROOT` defaults to the sibling `../TheOne`; override it with `cmake -S . -B build -DTHEONE_ROOT=...`. Games build engine libraries in their own build directory. Existing pinned dependency source downloads can be reused from the engine cache; its compiled binaries are not required. Game executables are normally under `build/bin/<game>/Release/` with shaders beside each executable. Planets can use a separate output directory through `PLANET_RUNTIME_DIRECTORY`; the launcher reads its generated executable path from `build/launch/planets-Release.txt`. Local build-directory overrides are not required for a fresh clone.

| Location | Ownership |
| --- | --- |
| `games/planets` | Kestrel planet exploration / excavation prototype |
| `games/sandbox` | Original import sandbox, editor UI and sample assets |
| `games/robots` | Existing Robots exploration port, tools, research, extracted local content |
| `games/transformers` | Asset inspection and playable character prototype; local original-game export required |
| `games/tornado` | Volumetric tornado and debris showcase |
| `games/effects-lab` | Engine reactive smoke/fire lab, six physical scenarios and repeatable captures |
| `UnityProject` | Separate Unity planetary flight, terrain, excavation and lighting experiments |
| `tools` | Authoring bridges and historical migration utilities |
| `shared/import-host` | Game-side host shared by Sandbox and Robots, including their gameplay and editor |
| `tests` | Cross-game integration and separation regression checks |
| `../TheOne` | Renderer, content, physics, input/effects primitives, generic import tools |

Each launcher sets its game's working directory so saves do not collide. Tests have separate working directories. The extracted Robots data stays ignored and local. Research documentation describes its fidelity limits; relocation does not establish a complete original-game port.

## Public source and local content

The repository includes native source, original generated sandbox fixtures, Unity
source/settings, attributed terrain textures and measured relief data. Build
outputs, Unity Library/Logs/UserSettings, saves, migration backups and extracted
Robots/Transformers assets remain ignored. See [content provenance](docs/CONTENT_PROVENANCE.md).
Robots and Transformers are source prototypes; cloning does not supply their
commercial game data or establish original-game fidelity. Sandbox, Planets and
Tornado build from repository source and generated/original fixtures.

The Unity project uses its own engine and is not part of the native CMake build.
Open `UnityProject` using the version in `ProjectSettings/ProjectVersion.txt`.
The Blender/Unreal/Fab tools are optional and require their documented external
applications/account setup; installing or logging in is not part of a game build.
Historical migration scripts under `tools` are records of the completed local
migration, not setup steps for new clones.

## Historical milestones and limitations

The following sections preserve earlier implementation and verification records.
Use the publication validation document for checks performed on the public baseline.

## Kestrel planet milestone

A native Vulkan prototype using TheOne's actual renderer, with a procedural **250 km radius** body. The global material field defines surface terrain, soil, sediment, basalt/granite, coherent ore pockets, cavities, hot solid mantle, molten outer core and solid inner core. Double-precision coordinates are translated to local floats before rendering. Excavation transfers whole cells into an inventory; deposits consume that inventory. Versioned saves preserve sparse terrain edits and inventory; invalid loads leave the current world intact.

- WASD flies the survey drone, Q/E descends/ascends, Shift boosts, held right mouse looks.
- Left click excavates the reticle target; X deposits the selected material.
- F5 saves and F9 loads `games/planets/saves/kestrel.json` when launched through `run.ps1`.
- O switches the surface view and global interior diagram. Latitude/longitude select a survey location. The bore button explicitly removes a chamber at the chosen depth and credits its material to inventory.

**This is a terrain foundation, not the completed simulation.** Local rendering uses a uniform 0.5 m voxel volume for nearby excavation, joined to a separate spherical height surface reaching approximately 8 km in each horizontal direction. The distant surface uses globally aligned levels of detail; tiny caves and individual edited cells never drive its coarse triangles. The near/far join uses clipping plus a narrow 4 cm overlap outside tool reach. Meshes rebuild in a background worker from immutable world snapshots; results from an older edit or view are rejected. The camera uses a 20 km far plane and reduced distance fog. This remains a finite surface view, not full planetary streaming or seamless orbital descent. Distant edits are represented in detail when their local volume is loaded; the remote height surface shows the procedural base terrain. Tool picks and drone collision still use cell occupancy, so they can differ from the smoothed visual surface by a fraction of a cell. Inventory accounts for full material cells; the reconstructed surface is not volume-exact. The cutaway is a geological diagram and does not show local excavation edits. The point-collision survey drone is not a walking character or vehicle. The molten-core label describes a material phase: magma flow, pressure, heat transfer, cooling, structural collapse, debris, machinery, multiplayer and production-scale streaming are not implemented. The planet proportions and temperature profile are gameplay approximations, not a geophysical model. No large-world frame-rate claim has been established.

The next engineering gates are bounded chunk streaming and continuous distant terrain, exact collision against the smooth surface, and a small mass/energy-conserving fluid and thermal test before connecting it to planetary interiors. Out of Ore informs excavation/material handling, while the global representation follows the procedural-world approach discussed in [Hello Games' GDC talk](https://gdcvault.com/play/1024265/Continuous-World-Generation-in-No). These are references, not code dependencies.

## Separation and verification

Before migration, existing tracked and untracked source files were copied into ignored `migration-backup/source`; the prior Git status and patch are alongside them. `migration-backup/moves.json` records the source-to-destination moves. Original assets and extracted data were moved on the same volume; scene references were updated to the new locations. The source backups retain the pre-migration references. Historical build artifacts can retain obsolete paths; active game launchers use the new executables and scenes.

Engine tests remain in the engine repository; their small original content fixtures live under `tests/fixtures/assets` and are not a playable game. Game behavior tests, animation tests and GPU smoke tests live here. GPU readback checks verify nonempty rendering, not visual fidelity. Smoke runs exercise synthetic inputs and audio submission; they are not a substitute for human gameplay/audio review.

Verified on 2026-11-01 in Release: two engine CTests and twelve game CTests passed, including Sandbox, Robots and Planets Vulkan validation plus the relocated external-model compatibility scene. Existing engine feature edits, Robots conversion tools, original sample assets and the full game regression source were compared with the backup byte-for-byte. The engine source SDK archive was inspected to exclude game source, executables and Python caches. Surface and interior GPU captures were visually inspected. This verification does not establish a complete planet simulation or a complete Robots port.

## Terrain refinement

The 2 m cube faces have been replaced with smooth shared-vertex terrain and 0.5 m excavation cells. The minimum tool radius is 0.5 m. Game-owned procedural surface shading adds world-anchored material grain, slope-dependent topsoil exposure and a simple sky/fog treatment. These are prototype materials, not photorealistic scanned assets or a physical atmosphere. Old 2 m saves refine to 0.5 m when loaded, preserving edited volumes and inventory quantities; the original file remains unchanged until the user saves. An upgrade exceeding the edit budget is rejected without replacing the active session.

Focused verification covers refinement accounting, non-axis-aligned normals, invariance under origin rebasing, synchronous and asynchronous excavation/save/view transitions, and Vulkan validation. The visual terrain captures are under `build/verification/planets/`. CPU mesh rebuild timings are reported by the application; they do not establish planet-scale performance.

## View distance and generation fix

Removed the expanding three-dimensional voxel grid that produced long sheets and wrongly exposed distant geological layers. Nearby terrain remains editable; distant terrain now comes directly from the radial height function, with independently aligned LOD bounds so moving does not drag the distant terrain samples. Filtered normals and material detail reduce coarse-scale striping. Camera near/far distances are configurable engine settings with the original defaults retained for other games.

Verification includes CPU checks for >7.5 km terrain coverage, distant surface height accuracy, stable far sample positions while walking, camera clipping, closed excavation meshes and material/save accounting; automated Vulkan captures cover the surface, excavation, a 512 m move and a different latitude. Background generation and stale-result rejection remain tested. The engine core regression also passed. No desktop UI automation was used after the user requested code-only work. The previous interactive process was left untouched to protect its unsaved edits.

## GPU geometry integration

Sandbox and Robots now submit imported meshes through `World::prepare` and TheOne's GPU skinning renderer. Their CPU triangle bake remains only for exact editor picking; particles remain dynamic. Planet rendering uses a mesh revision cache, incremented on accepted background builds and mesh replacement, so unchanged terrain stays in GPU memory. Animation pose sampling and procedural terrain generation still run on the CPU. Engine GPU parity tests and application validation smoke tests cover these paths; see `../TheOne/docs/GPU_PERFORMANCE.md` for measured results and limits.
