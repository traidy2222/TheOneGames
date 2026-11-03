# Continuous planetary flight

The active scene is `Assets/Scenes/PlanetaryFlight.unity`. It uses one observer camera and one scene throughout orbit, atmosphere, landing and ascent. Venus is the first configured world; the flight and terrain streamer read a `PlanetDefinition` rather than selecting a Venus-specific orbit/surface mode.

## Controls

- **1:** continuous descent to the ground below you. **2:** continuous ascent to space.
- Click to capture the mouse. Mouse looks around; **WASD** flies and cancels automatic travel.
- **Space / Left Ctrl:** rise / descend radially. **Shift:** boost. Wheel adjusts flight speed.
- **Esc:** releases the mouse and stops cruise. **L:** toggles the shadowed headlamp.
- Near the ground: **LMB** excavates, **RMB** deposits, **Z** undoes. **F5 / F9** save / load the current excavation cell.

Automatic flight changes physical position continuously. Its speed decreases toward the surface; it is a navigation aid, not a spacecraft or orbital-physics simulation. The landing camera turns toward the horizon over the final 300 metres.

## Adding another planet

Create a Planetary / Planet definition asset, configure it, and assign it to the scene's PlanetFlight component before entering Play mode. Radius and distances are in metres. Configure atmosphere height, cloud height (zero disables clouds), haze/daylight colours, fog density, atmosphere scale height, cloud-band width, materials, relief and seed. Surface gravity is retained as physical metadata; free flight does not simulate gravitational acceleration.

Turn off `useMagellan` for procedural terrain. `AirlessTestWorld.asset` is a second working definition and test fixture, not a measured reconstruction of the Moon. `Venus.asset` uses the existing measured Magellan adapter. A new measured world needs its elevation source connected in `PlanetSampler.SurfaceRadius`; all flight, tile and excavation code remains shared. There is no measured Earth terrain asset yet.

This implementation handles spherical solid-surface planets. Gas giants require a different atmosphere/interior model and do not have an excavatable solid landing surface. Multiple simultaneous planets, interplanetary travel and oblate bodies are not implemented.

## How the transition works

Physical positions remain double-precision planet-centred metres. Geometry is generated around double-precision tile anchors, then the observer is subtracted before conversion to Unity floats. One continuously varying render scale keeps orbital distances manageable and reaches one Unity unit per metre near the surface.

Six cube-sphere faces refine into an adaptive quadtree. At most three geometry tasks run concurrently. Mesh creation stays on the main thread with a two-mesh / approximately 3 ms commit budget; one commit can exceed that soft budget. A parent remains visible until all four child meshes exist. Skirts cover different-resolution boundaries. The cache is capped at 800 nodes and distant children are released. LOD changes remain discrete; there is no geometric morphing yet.

Atmospheric haze and cloud opacity change with altitude. These are visual approximations, not physically integrated scattering or volumetric weather. The DEM is read once during startup; there are no scene loads or file fetches during flight. Startup still performs initialization and the first coarse tiles appear over several frames.

## Excavation and persistence

The planet now has an implicit **solid three-dimensional interior**, with sparse editable samples on a global one-metre lattice. There is no fixed underground floor. A mine can continue through the centre and emerge on the opposite side. Unmodified rock is evaluated from the planet shape; only modified samples are stored. See `PLANET_INTERIOR.md` for the storage, collision, verification and remaining limits.

Below 96 metres altitude, 16-metre voxel chunks stream around the observer in all three axes. A contiguous 48-metre cube replaces the nearby surface only after its geometry is ready. That cube is the nearby collision window. Edited geometry renders separately out to a configurable 4096 metres, with a default 2048-chunk target budget. Moving in any direction streams new chunks. Neighbouring chunks share canonical lattice samples and halo gradients; they do not have artificial solid end caps.

Edits autosave approximately every two seconds and when leaving Play mode. F5 flushes pending edits; F9 reloads persisted chunks. Z undoes the last brush application (up to eight). There is one owner for every global sample, so edits cross chunk boundaries. Far-away surface LOD does not display excavation details until the voxel volume streams in.

The new format is `PlanetVolumes-v1/<definition-name>-<stable-generator-hash>/<x>_<y>_<z>.vxl` under Unity's persistent-data folder. Old shallow cell saves remain in `Planetary/` and the legacy scenes; they are not silently interpreted as global voxel data. The hash includes radius, height datum/source, terrain seed, relief and fine-detail settings.

## Bridge and verification

Use `Tools/Invoke-UnityBridge.ps1` with `flight`, `play`, `flight-descend`, `flight-ascend`, `flight-status`, `flight-validate-ground`, `flight-verify`, `volume-verify`, `lighting-verify`, `capture` or `stop`. `flight` selects the scene while outside Play mode. `flight-verify` temporarily locks input and records a controlled descent, excavation check and ascent, then releases input in orbit. Status reports altitude, frame count, camera and scene IDs, resident tiles, coverage and excavation readiness. All work uses this local allowlisted bridge and shell tools; no desktop automation is needed.

`Planetary.EditorTools.PlanetFlightSetup.BatchVerify` checks seven altitudes from 12,000 km to 2 metres, unchanged camera/scene identity, coordinate precision, refinement coverage, metre-scale landing LOD, bounded tile residency, monotonic flight and shaders. It also renders the alternate airless planet at a cube-face seam, near a pole and near the longitude wrap. `ValidateGround` checks density changes and undo after the volume is ready. `PlanetVolumeValidation` and `volume-verify` cover through-planet traversal, wall collision and persistent underground edits.

See `flight-validation.txt`, `flight-live-validation.txt` and the flight captures beside this document for results. Timings are development observations, not a guarantee of hitch-free performance on every machine.

The older `Venus.unity` explorer and its separate orbit/surface views remain available for comparison. `VENUS.md` describes that historical scene and the scientific data provenance; the new scene is the enabled build entry.
