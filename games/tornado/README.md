# Tornado Field Lab

First visual simulation prototype, 2 November 2026. This is a separate game consuming TheOne's Vulkan renderer. It does not implement a complete, predictive tornado or storm-atmosphere solver.

## Run

Double-click `Play-Tornado.cmd`, or run `./Play-Tornado.ps1`. Use `-Build` after source changes. The Release executable is `../../build/bin/tornado/Release/tornado.exe`.

The opening scene is a mature tornado above a flat rural landscape, with a 1,250 m reference cloud-base height, adjustable 140 m default vortex core radius, and 68 m/s selected peak tangential wind. These are model parameters, not measurements of a recreated historical event. Core radius is neither the visible funnel radius nor a damage-path width.

| Control | Action |
| --- | --- |
| Right mouse held | Look around |
| WASD | Fly |
| Q / E | Descend / ascend |
| Shift | Move faster |
| 1 / 2 / 3 / 4 | Roadside / close / aerial / opposite-side camera |
| O | Toggle orbit camera |
| Space | Pause |
| Tab | Show simulation controls |
| H | Hide interface |
| F2 | Renderer profiler |
| F12 | Write `capture.ppm` in the working directory |
| Escape | Exit |

The controls panel adjusts wind, core radius, development, and 48–144 ray samples. Its optional 190-second lifecycle extends the funnel downward, holds maturity, and dissipates it. This lifecycle is authored; it does not emerge from atmospheric instability.

## What is implemented

- A three-dimensional, ray-marched funnel, cloud deck and ground dust volume. Rotating procedural noise produces evolving detail. Cloud and funnel extinction are combined in depth order. The camera can move around the volume.
- A smooth analytical tangential vortex, radial inflow and vertical updraft. Tangential speed follows `2 V x / (1 + x*x)`, with `x = radius / height-dependent core radius`, a near-ground speed reduction, and a development multiplier.
- 2,400 independently tracked scraps with quadratic drag, gravity and ground contact, updated at 120 Hz. A heuristic aerodynamic pickup term acts near the ground. Particles leaving the domain recycle into the supply. Initial particles are seeded airborne for the mature showcase.
- Flat procedural agricultural fields, road markings, fences, power lines, simple farm buildings and trees as scale references.
- Separate game-owned shaders deployed over this game's shader copies. No engine source modifications were needed.

## Engine review and the remaining realism gap

The current engine provides the reusable Vulkan/SDL window, frame synchronization, shader pipeline, ImGui integration, screenshots and GPU timings. Its existing particle effect is a small fixed-purpose effect; it does not supply a weather solver. This prototype therefore owns its vortex model, debris integration and volume rendering.

The following are **not implemented**: pressure evolution, a divergence-constrained fluid solver, buoyancy and temperature transport, humidity and condensation, latent heat, precipitation microphysics, parent-supercell dynamics, resolved boundary-layer turbulence, spontaneous subvortex formation, terrain feedback, two-way debris coupling, structural failure, debris-to-debris collisions, vehicle dynamics or storm audio. The visible rain curtain is shading, not simulated precipitation. Prop obscuration and sunlight attenuation are approximations. There is no temporal antialiasing or volumetric multiple scattering; fine noise and thin geometry may shimmer.

Real-world metre scale is achievable here; literal one-to-one atmospheric fidelity is not demonstrated. This scene should be judged as a visual and interaction baseline, not as evidence that an actual tornado has been reproduced. It is not a forecasting or engineering safety tool.

For a higher-fidelity next phase, first define a bounded validation case and target hardware/frame budget. Build a separate local fluid-solver experiment with measured divergence, conservation, timestep and grid-convergence checks. Couple temperature/moisture and boundary forcing before claiming physically generated condensation or tornado formation. Compare radial/vertical wind profiles and time evolution to research or observations. Integrate that result only after it passes those gates; structural destruction is a separate mechanics task.

For scale: a 4 × 4 × 2 km domain at 2 m spacing contains four billion cells. Eight single-precision fields alone require about 128 GB before solver buffers, pressure iterations and rendering. This arithmetic is illustrative, not a claim that a particular resolution is sufficient. A practical game will need nested domains, adaptive resolution or a calibrated reduced model.

## Research references

- [NOAA NSSL tornado research](https://www.nssl.noaa.gov/research/tornadoes/) describes the continuing research into tornado processes and near-ground wind measurements. It provides context for why visual resemblance alone does not establish physical accuracy.
- [VORTEX2 science](https://www.nssl.noaa.gov/projects/vortex2/science/) identifies near-ground wind structure and observational/numerical work as important research areas.
- [NOAA repository: terrain and tornado dynamics](https://repository.library.noaa.gov/view/noaa/31711) documents terrain-related changes in simulated tornado structure, motion and wind variability. Flat terrain in this build omits that interaction.
- [NWS Enhanced Fujita scale](https://www.weather.gov/oun/efscale) explains its use of damage indicators and estimated winds. This prototype exposes m/s rather than assigning an EF rating from a wind slider.

These references inform scope and terminology. They do not validate this prototype's formulas or visuals.

## Focused verification

Release build and the `tornado_model` CTest passed. Checks cover selected core wind, outer decay, no upward flow at the ground, dissipated wind, finite debris, ground contact, deterministic replay over 10 simulated seconds and 60/120 Hz integration agreement.

Roadside, close and aerial GPU captures were rendered and visually reviewed. Vulkan validation in the roadside and aerial checks reported no errors; an unused vertex-attribute performance warning remains in the existing pipeline setup. The first image review led to reduced spiral distortion, self-shadowing changes and corrected volume/cloud overlap. An elevated-view terrain-distance cutoff was subsequently removed.

On the local RTX 3080, the final 240-frame roadside live run at 1440 × 900 with 96 volume samples measured **58.7 fps**, with **4.94 ms mean sampled GPU time**. Vsync was enabled. This is a short local run, not a minimum-frame-rate guarantee or an endurance result. Capture-inclusive smoke runs are deliberately excluded from the performance claim because synchronous PPM readback/writing dominates them. Live window observation also showed approximately 60 fps. UI automation stopped on detected user input; complete manual control validation is not claimed.

From the Games 2 root:

```powershell
cmake --build build --config Release --target tornado tornado_model_check --parallel 8
ctest --test-dir build -C Release -R '^tornado_model$' --output-on-failure
```

From the executable directory:

```powershell
./tornado.exe --benchmark --clean
./tornado.exe --smoke-test --clean --view 0
./tornado.exe --smoke-test --clean --view 1
./tornado.exe --smoke-test --clean --view 2
# Optional deterministic render parameters:
./tornado.exe --smoke-test --clean --time 18 --maturity 0.5
```

`--validate` requires a Vulkan validation layer installation; the PowerShell launcher locates the local Vulkan SDK when called with `-Validate`. Screenshot checks reuse the engine's scene-coverage heuristic, which can reject nearly uniform cloud views even when rendering succeeds.
