# Optimus Prime asset integration

Source: the user's PC Revenge of the Fallen disc. Source archives and all converted content live under ignored `build/`. The installer and original game executable are never executed by the import tools.

The conversion uses the MIT-licensed [rotf-robots](https://github.com/HUNK2/rotf-robots) exporter at `dd97296c9a618d06cc2e01bf38c6221b057f6eba`. Its decoder is the source-data reference, not proof of parity with the running original game.

## Running

From this folder: `./run.ps1` (or `./run.ps1 -Build`). Choose standard, powered-up or gold in the viewer. `-Variant optimusflight` and `-Variant optimusgold` are also supported.

Reproduce the import with `./tools/extract_disc.ps1`, then `./tools/export_assets.ps1`. The supplementary exporter sources, license and focused validation command are in `tools/patches/`. No installer execution or manual Blender conversion is required.

The viewer provides every catalog clip, frame scrubbing, speed, root motion, both transformation directions, native mesh-set visibility, weapon attachment events and sound sample audition. Right-drag orbits, scroll zooms, Space pauses, T changes form. Powered-up Optimus flies using the robot body; its source has no truck model.

## Responsibilities

| Module | Responsibility |
|---|---|
| `tools/extract_disc.ps1`, `tools/stage_assets.py` | Read-only ISO/cab extraction, MSI path reconstruction, original asset hashes |
| `src/catalog.*` | Sidecar validation, immutable engine model loading, mesh-set visibility |
| `src/pose_track.*` | Native clip pose sampling, retained unkeyed poses |
| `src/player.*` | Event timeline, paired forms, weapon mounts, source tick speeds and root travel |
| `src/render_scene.cpp` | GPU draw/palette submission through TheOne |
| `src/audio.*` | Local WAV audition at clip event times |
| `src/stage.*`, `src/main.cpp` | Inspection stage and browser UI |
| `src/check.cpp` | Focused imported-pose, catalog and transformation checks |

## Validation and boundaries

The standard exporter comparison checked 267 animations at 73,700 source node samples, including fractional ticks. Maximum position difference was 0.000175 m. The imported catalogs are separately sampled in TheOne at five points per clip, checking finite transforms and transformation visibility endpoints. Render captures use the actual Vulkan path with validation enabled. Detailed logs and captures are in `build/inventory/`.

This is an asset/animation inspection demo, not a port of the original game's combat, locomotion, physics or missions. Exported gameplay, collision, particles, light, material and sound metadata are retained. The original particle/shader programs, full FEV parameter/spatial mixer, additive gameplay layers, wheel/suspension simulation and flight-surface simulation are not reproduced by the browser. Clips can be inspected individually; the browser's inspection floor follows the vehicle's lowest rendered bound rather than altering native poses or pretending to simulate suspension.

The viewer defaults to full texture atlases because small body submeshes share large atlases. The toggle restores the engine's normal texture residency behavior. Engine budgets, mesh caching, GPU skinning and the other games' residency settings are unchanged. Lighting is selectable between TheOne's legacy and PBR paths; neither is claimed to reproduce the original renderer exactly.

Completeness is audited against source animation objects and track keys, not only successful export counts. Native orphan motion/mesh data must remain explicitly unbound where the disc supplies no recoverable hierarchy. Do not mark original-game parity or complete binding as verified without corresponding evidence.

The supplementary import recovers the originally omitted player and camera intros for all three variants. Standard/gold also retain the two unbound FaceSet motion clips as independent named motion carriers, with no fabricated parent hierarchy or mesh assignment. The browser draws axis markers for camera/unbound carrier clips. Raw scalar/property and interaction-event streams remain in supplementary JSON. Every original texture payload is retained as DDS, including mip levels and cube faces; flat textures also have PNG previews.

Final inventory: **2,280/2,280 decoded native meshes**, **350/350 textures** (347 PNG previews and three DDS cubemaps), and **796 imported GLB animations** across the three variants. Standard/gold each expose 271 GLB animations plus two original event-only entries; powered-up has 254. Native source-object counts differ because animation aliases, per-weapon copies and additive aim layers can share source clips. The original 200/162/200 AnimFile objects decode without failures; recovered intros close the previously missing unique-clip coverage. All remaining LOD and ancillary meshes have explicit source-ID-to-GLB mappings in each supplemental file.

Focused supplemental readback passed for 359,419 source vertices/indices, 23 skinned LOD parts, 972 intro node poses, 1,800 unbound channel samples and all 350 DDS payloads. TheOne imports sample 4,000 catalog poses across the variants, including retained-pose and A/B tyre-position regression checks. Hidden source collections are not posed or uploaded while inspecting the main character.

The legacy monolithic `VehicleModel.Trans_R2V` keys duplicate tyre offsets in the original source data. It remains available for inspection exactly as authored, but normal truck initialization and paired transformations use clean rest → `Trans_R2V_A` → `Trans_R2V_B` → `V_Idle`. Seeding from the legacy clip contaminates the unkeyed tyre channels and is covered by a focused regression check.

One standalone legacy vehicle clip refers to `ANIM_OPTI_TRANS_VEH`, which does not exist in the extracted English Optimus FEV. Its actual A/B transformation cues exist and are used by paired transformations. This unresolved legacy name is retained rather than silently replaced with a different sound.
