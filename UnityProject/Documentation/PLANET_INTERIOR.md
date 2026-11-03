# Planet-wide interior excavation

The active PlanetaryFlight scene uses sparse, persistent three-dimensional terrain. There is no bedrock floor at 12, 50 or 100 metres, and no special barrier at the centre. The same global coordinate system continues to the far side. A real-scale planet still requires mining a real-scale distance.

## Playing

Descend with 1. Click to capture the mouse, look at rock, and hold left mouse to mine. WASD flies through the space you excavate; Space/Ctrl move along local radial up/down. Right mouse fills, Z undoes the last brush application, F5 saves and F9 reloads. Underground, the HUD shows depth. L toggles the shadowed headlamp; skylight remains visible through an open shaft. Orbit controls are disabled below ground so they cannot bypass solid rock. Follow the tunnel out manually.

Movement uses swept density queries against unmined rock. It no longer clamps your position to the original surface. The centre has a finite direction fallback, and the observer stays in double-precision coordinates throughout.

## Storage and streaming

- The base planet is an implicit solid. Only modified lattice samples consume save storage.
- One-metre samples belong to signed, global 16 × 16 × 16 chunks. Canonical sample ownership and a gradient halo keep shared mesh boundaries consistent.
- A 3 × 3 × 3 chunk neighbourhood follows the observer in X, Y and Z. The 48-metre active region is a streaming window, not a depth or world-size restriction. Existing visible coverage stays until its replacement is ready.
- At most two mesh jobs run concurrently. Unity meshes and colliders are committed on the main thread. Unchanged solid chunks need no interior triangles.
- The interactive density page cache holds at most 128 pages. Lighting also uses immutable copies of relevant edited samples, and the edited-chunk index scales with the number of saved chunks. Dirty pages flush before eviction; re-entering a mined area loads its saved changes.
- Chunk files use gzip, explicit identity/version checks and an atomic replacement write. Autosave flushes pending changes approximately every two seconds. Disk use grows with excavation; very large mines can still require substantial storage and I/O.
- Up to eight recent brush applications can be undone. Undo history itself is not persisted across sessions.

A generator fingerprint separates different planet radii and terrain sources. Older shallow-field saves are retained under their old paths and are not automatically converted into this global format.

## Verification

`Planetary.EditorTools.PlanetVolumeValidation.BatchVerify` excavates a continuous tunnel across a 256-metre-diameter test world, then moves the observer from outside one side to outside the other. It checks the centre crossing, wall collision, persistence, eviction/reload, signed chunk coordinates, unchanged camera/scene and bounded residency. It also probes the real-scale Venus model at 100 m, 1 km, 50 km, 1000 km, 3000 km, the centre, across the centre and beneath the opposite surface.

`volume-verify` on the local editor bridge runs a live descent, excavates a 140-metre shaft, follows it underground, reloads its saved volume, checks a wall collider and returns through the same shaft. Input is temporarily locked for that check. This is test automation through the project bridge, not desktop control.

Results: `volume-validation.txt`, `volume-live-validation.txt`; rendered evidence: `volume-centre.png`, `volume-deep-tunnel.png`. The complete 12,100 km Venus diameter was not mined end-to-end during verification; the complete traversal test uses the smaller planet and the same runtime.

## Remaining limits

This is an excavatable solid-planet model, not a simulation of mantle convection, molten cores, pressure, heat, gravity inside a planet, rock collapse or material conservation. Interior rock currently uses the shared rock material. Streaming and persistence have bounded memory use, but sustained planet-scale excavation has not been endurance-tested. Far surface meshes do not yet incorporate remote mines. Mining is one-metre-resolution CSG; the old shallow prototype's grading tool is not connected to the new global store yet.


Tunnel visibility, direct shadows and one-bounce GI are described in `TUNNEL_LIGHTING.md`. The old forced-black underground sky and 48-metre rendering cutoff have been removed.
