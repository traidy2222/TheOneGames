# Optimus Proving Ground

A small playable industrial test world using the imported Optimus character. This is a separate executable from the animation browser.

Run `./games/transformers/play.ps1` from the Games 2 folder. Add `-Build` to rebuild only this demo.

## Controls

| Control | Action |
|---|---|
| WASD | Camera-relative robot movement; throttle/reverse and steering in truck mode |
| Shift | Sprint / boost |
| Space | Jump in robot mode |
| T | Transform while grounded |
| Left mouse | Fire (mild target aim assistance) |
| Q | Switch dual lasers / paired shell cannons; hold then release left mouse to charge/fire shells |
| F | Melee nearby targets |
| Hold right mouse | Orbit the camera |
| Mouse wheel | Zoom |
| R | Respawn as robot; target and lap progress remain |
| F1 | Toggle controls |
| Esc | Pause / resume |

The teal pads mark a six-checkpoint driving circuit. The minimap highlights the next checkpoint. Hit each of the six small orange physics crates three times to turn it green. The central yard has containers, a ramp, catwalk and jumping platforms. Focus loss pauses the demo.

## Scope and modules

This is prototype gameplay, not original-game handling or mission logic. Both forms use the engine's player capsule; truck steering, acceleration and wheel rotation are simplified, without suspension or a truck-shaped rigid body. Weapon hits use local OBB raycasts and melee proximity checks. Source animations, paired transformation poses and event audio are reused from the character browser. Full atlas detail is selected locally for Optimus; engine residency defaults are unchanged.

| Module | Responsibility |
|---|---|
| `src/play/arena.*` | Authored geometry, collision, dynamic targets, raycasts and scene assembly |
| `src/play/controller.*` | Locomotion, animation selection, transformations, combat and circuit progress |
| `src/play/blend.*`, `src/play/aim.*` | Local TRS crossfades and source additive aim offsets |
| `src/play/weapons.*`, `src/play/weapon_heat.*` | Mounted loadout, muzzle origins, fire timing and heat |
| `src/play/presentation.*` | Orbit camera, collision avoidance, HUD, minimap and tracers |
| `src/play/main.cpp` | SDL input, fixed-step host, pause menu and render loop |
| `src/play/check.cpp` | Focused gameplay checks |

## Focused verification

`transformers_play_check <path-to-optimus.json>` passed spawn, walking, jumping, transformation, driving/steering, respawn, wall collision, raycast/target impulses, checkpoint laps, controller shooting/melee and combined GPU palette bounds.

The playable executable supports `--capture --validate --script drive` and `--capture --validate --script walk`. Both rendered at 1600x1000 with zero Vulkan validation errors; captures and logs are under `build/inventory/`. Existing shader output-not-consumed performance warnings remain. The world exposed a renderer binding defect when instancing leaves the camera list empty while shadow casters remain: `TheOne/src/render/vulkan/passes.cpp` now binds the geometry palette from the prepared scene rather than the filtered camera list. The same focused runs cover that path. No engine-wide or other-game suites were run.
