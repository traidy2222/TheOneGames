# Excavation terrain prototype

Unity 6.3 LTS (6000.3.27f2). Open Assets/Scenes/Excavation.unity and press Play. Click the Game view to capture the mouse.

- WASD: move; mouse: look; Shift: faster; Space: jump.
- Left mouse: excavate. Right mouse: deposit ground. Wheel: brush radius.
- F: walking / survey flight. Space / left Ctrl: ascend / descend in flight.
- Z: undo the last stroke (eight snapshots). F5: save. F9: load. Home: return to spawn. Esc: release mouse.

Saves are explicit, local binary files in Application.persistentDataPath/excavation-ground.bin. Play-mode edits are not retained unless saved. Load checks dimensions, resolution, seed, length, finite densities and boundary values before replacing the live field.

## Implementation

DigTerrain stores one shared 97 x 49 x 97 density grid (0.75 m samples) for a 72 x 36 x 72 m volume. Positive values are empty space. Spherical CSG subtraction and union allow holes, tunnels and overhangs. Six tetrahedra per cell produce the surface. Chunks contain 16 cubed cells and share field samples and gradient normals. Edits queue surrounding chunks including a normal halo; at most three mesh/collider rebuilds run per frame. A stroke waits for outstanding chunk work before another cut. MeshColliders use the same generated meshes.

The ground has a fixed, closed outer boundary. Transform must remain at identity. Density resolution and size must not change after initialization. Materials are procedural visual strata; there is no ore inventory or geological material tracking. Filling is unrestricted material creation, not a conserved soil simulation. No slumping, collapse, excavators, bucket interaction, streaming, LOD, jobs/Burst or multiplayer yet. Rebuilds and saves run on the main thread. Large brushes or undo may cause a pause and brief collision lag during queued rebuilding. Performance is not production validated.

## Verification

Excavation > Validate terrain checks underground cavity and roof, raycast collision after digging/filling, edits on a chunk junction, undo, binary save/load, malformed-save rejection without changing live data, and finite mesh normals. The test creates and removes a temporary terrain object. Run outside Play mode.

TerrainBridge.BatchVerify can be called through Unity -batchmode -quit -executeMethod. It validates and renders an excavated landscape in an isolated project copy. terrain-validation.txt records the initial editor validation; it is not a general test runner log.

## Local editor bridge

After Unity imports TerrainBridge.cs, it polls Library/TerrainBridge/request.json every half second while idle. Write a JSON object such as {"action":"validate"}. Allowed actions: validate, play, stop, capture, build. No shell execution or network endpoint. Result: response.json. Capture: preview.png (camera only, excludes the IMGUI HUD). Build: Builds/Excavation/Excavation.exe. Submit one request at a time and wait for its response. Play/stop acknowledge the request; the editor changes mode asynchronously. Unity must finish its initial asset import before the bridge can run.

Source inspiration: the terrain interaction described in the official Out of Ore announcements, https://steamcommunity.com/app/2009350/announcements/. This is an independent prototype, not a reproduction of that game's proprietary terrain implementation.

A terminal client is provided at Tools/Invoke-UnityBridge.ps1. Example: `./Tools/Invoke-UnityBridge.ps1 -Action capture`. It serializes client calls, discards stale acknowledgements, publishes requests atomically, and waits for success or failure with a timeout. Use `-TimeoutSeconds 600` for builds. After timeout a command may still execute; inspect its response before retrying. No computer-use automation is needed. Avoid stop while there are unsaved play-mode terrain edits. Camera captures omit the HUD. Script changes still require Unity's normal asset import/recompilation; the bridge must already be loaded.
