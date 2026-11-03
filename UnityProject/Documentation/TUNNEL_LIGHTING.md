# Tunnel visibility, shadows and global illumination

The former 48-metre rendering box and forced-black underground sky are removed. Nearby collision still uses a small streaming neighbourhood, while excavated geometry has a separate **4096 m default view distance** and **2048-chunk target budget**, configurable on PlanetDefinition. The budget prioritizes nearby geometry; extensive mines can exhaust it before reaching the distance limit. Old local coverage can temporarily overlap the new neighbourhood while it builds.

A persistent edited-chunk index finds passages after save/reload. A GPU hash table identifies exactly which chunks have replacement geometry, including distant tunnel mouths, so the coarse planetary surface does not seal them. Empty space reveals the same atmospheric sky used above ground. Solid walls and plugs occlude it. Looking up a 140-metre shaft now shows its entrance even with the headlamp off.

## Direct light

The sun and headlamp are real Unity realtime lights. The terrain shader casts and receives shadows in its forward lighting passes, including the surface cutouts. The headlamp is a slightly offset spotlight with a 70-degree cone and 80-metre range near the surface. Press **L** to toggle it. Direct material shading uses roughness, a dielectric GGX specular term and triplanar rock normals.

Voxel sunlight visibility is also traced through the density field, so nearby raster shadow-map range does not by itself allow sunlight through deep rock. Traces have a finite distance and sampling tolerance. Atmospheric appearance remains an artistic reconstruction, not a physically integrated Venus scattering model.

## Indirect light

This is **approximate, diffuse, first-bounce GI**, not full path tracing or an unlimited multibounce solver:

- Worker jobs trace sky and sun visibility through an immutable snapshot of edited density. Surface samples gather diffuse sky illumination and one bounce from directly lit rock. Coarse results are interpolated over voxel meshes.
- A local 7 × 7 × 7 probe texture, spaced three metres apart, estimates one bounce from the moving headlamp. Rays evaluate the first wall hit, lamp orientation/falloff, surface orientation and visibility back to the lamp. Solid probes reject illumination.
- Density changes invalidate light results. Geometry is rebuilt before the more expensive light update, and tracing runs asynchronously. Lighting can take time to catch up with fast editing; probe interpolation and coarse mesh samples can still produce artifacts.
- Finite ray counts, fixed proxy rock reflectance and low-order probe interpolation limit accuracy. The system does not model multiple diffuse bounces, caustics, volumetric shafts, hardware ray tracing or thermodynamic emissions from a planet's core.

The renderer no longer supplies a uniform underground glow based on depth. A long, narrow shaft may be nearly black with the lamp off while its small sky opening remains visible. That is distinct from geometry being cut away close to the camera.

## Verification

`Planetary.EditorTools.TunnelLightingValidation.BatchVerify` checks:

- A 140 m entrance and its connecting walls stay rendered; edited geometry at 1 km remains resident.
- The sky appears through the shaft with the lamp off.
- Filling a plug blocks both the traced sky path and the rendered entrance; undo restores the opening.
- Lamp bounce produces nonzero indirect irradiance, while solid probes and blocked rays reject it.
- A displaced spotlight and an edited obstruction produce measurably darker shadow pixels than the same render with shadows disabled.
- Shader variants compile, and GPU resources are explicitly released by the batch harness.

The live `lighting-verify` bridge action checks the existing saved 140 m shaft, captures the sky with the lamp off, then restores the lamp and user input. It does not excavate or overwrite that saved shaft. The test positions the diagnostic camera at the existing shaft depth.

Evidence is stored in `tunnel-lighting-validation.txt`, `tunnel-lighting-live.txt`, `tunnel-open-sky.png`, `tunnel-sealed.png`, and `tunnel-shadowed-lamp.png`. The 4 km default is a configuration limit; the targeted visual test covers the 140 m shaft and a separate residency check at 1 km. Large, dense mines have not been endurance-tested.

Reference: [Unity 6.3 built-in shadow shader documentation](https://docs.unity.com/en-us/engine/6000.3/manual/materials-and-shaders/shaders/writing-shaders-birp/built-in-shader-examples/receive-shadows). The indirect-light solver is project code rather than Unity's baked-lighting system.
