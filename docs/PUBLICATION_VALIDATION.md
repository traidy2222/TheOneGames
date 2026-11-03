# Public baseline validation — 2026-11-03

Engine revision: `da5ce286c271cb92e37c78bf6601de70faa39daf`.
The same value is recorded in `engine-revision.txt`. Native source changes were
validated through game commit `d56b1f7`; subsequent publication documentation does
not change native behavior.

Environment: Windows x64, Visual Studio 2022, C++20, Vulkan SDK 1.4.304.0,
NVIDIA GeForce RTX 3080, Release. The engine and game collection are independent
Git repositories. Games consume public engine targets from a sibling checkout.

## Builds and checks performed

| Scope | Result |
| --- | --- |
| Current engine module/header assembly, core content, STEP animation endpoints, promoted edit history and scene reuse | All five focused CTests passed. Public headers compile independently, including the promoted APIs. |
| Earlier current-state renderer/cache checks | Eight selected checks passed before the publication extractions, covering core/module boundaries, physics editing, streaming memory, caches, presentation pacing, shadows and prepared assets. No renderer/shader algorithms changed in the extractions. |
| Fresh game build under `build/publication` | Sandbox, Robots, Planets, Tornado, Transformers inspection and Transformers Play all built, together with their selected test/profile executables. Dependency sources were reused from the engine's pinned download cache; libraries and executables were built in the new build directory. |
| Game integration, Robots synthetic inventory/motion, repository separation, planet material/save model, Sandbox/Robots Vulkan validation, scene handoff and tornado model | All nine selected checks passed in the initial game run. Robots rendering used local ignored converted content. |
| Planets validation, async rebuild and distance modes | All three passed after updating its obsolete single-buffer smoke assumption. Each frame slot warms its own terrain buffer; smoke progression waits for staged uploads. Checks retain cache reuse and stale-result rejection assertions. |
| Transformers asset inspection | Passed against the local ignored Optimus export: 19 pieces, 273 catalog clip entries, 1,365 pose samples, 381,024 vertices and transformation endpoint checks. |
| Transformers controller check | Passed spawn, walking, jumping, transformation, driving/steering, respawn, wall collision, target impulses, checkpoint lap, shooting/melee and palette assembly. |
| Tornado Vulkan smoke | Completed 14 frames at quality 48 with synchronization validation and a nonempty readback (303,150 scene pixels). This is an integration smoke, not visual acceptance or a throughput benchmark. |

The first game run was 9/10 because Planets rejected the legitimate first upload
to the second frame slot. Only the affected three Planets modes were repeated
after its smoke correction; unchanged checks were not rerun as a complete suite.
Vulkan emitted the existing unused vertex-attribute performance warning. No
validation errors were reported by the passing GPU checks.

The default Sandbox executable was initially locked by an existing process.
That process was not terminated for this work; a separate verification build
avoided replacing its executable or interacting with unsaved session state.

## Reproduce selected checks

From the engine checkout:

```powershell
cmake --build build --config Release --target theone_animation_tests theone_editor_history theone_scene_reuse theone_module_smoke theone_core_tests --parallel 8
ctest --test-dir build -C Release -R '^engine_(animation|editor_history|scene_reuse|module_boundaries|core)$' --output-on-failure
```

Configure the game collection first (`cmake -S . -B build -A x64`, with your Vulkan
SDK available), then build the selected test/application targets before CTest.
Use `-B build/publication` and `--test-dir build/publication` to isolate verification
from an existing build. Example native target list:

```powershell
cmake --build build --config Release --target sandbox robots planets tornado transformers transformers_play game_integration_tests scene_handoff_profile planet_tests tornado_model_check transformers_check transformers_play_check --parallel 8
ctest --test-dir build -C Release -R '^(game_integration|robots_animation_inventory|robots_motion_sampler|game_engine_separation|planet_world|scene_handoff_focused|tornado_model|sandbox_validation|planets_validation|planet_async|planet_distance)$' --output-on-failure
```

Robots rendering and Transformers runtime/asset checks require local source
exports as described in their project documentation. Those assets are not
provided by cloning the public repository.

## Evidence and limits

Local ignored logs include engine `build/publication-focused-build.log` and
`build/publication-focused-checks.log`; game `build/publication-clean-build.log`,
`build/publication-game-checks.log`, `build/publication-planets-checks.log`,
`build/publication-transformers-check.log`,
`build/publication-transformers-play-check.log` and
`build/publication-tornado-check.log`. They are not public source dependencies.

No complete engine/game suite, new Unity Editor validation, interactive gameplay
acceptance, original-game fidelity audit or controlled performance comparison was
performed. Unity source/settings/provenance and its historical development
captures are included, with their original dates and limitations.
