# Reactive volume lab

A native consumer of `TheOne::Volumes` and the engine HDR renderer. All solver,
combustion and volume shaders live in the separate engine repository. This project
owns only scenarios, visible obstacle geometry, camera/UI and capture tooling.

```powershell
.\games\effects-lab\Play-Effects.ps1 -Validate
```

Right mouse looks; WASD flies, Q/E lowers/raises, Shift boosts. R replays; Space
pauses. The UI selects open air, wall, ceiling, crosswind, merging sources and
zero gravity, as well as grid width, source mass and exposure. Change fuel mass
then replay. The default grid is 48 × 72 × 48 over 12 × 18 × 12 metres.
Resolved initial air motion uses 0.8 m/s RMS at a 1.5 m scale; the solver transports
it through the source and plume.

For repeatable captures, build `effects_lab` and use:

```powershell
python games/effects-lab/tools/capture.py `
  --exe build/bin/effects-lab/Release/effects_lab.exe `
  --output build/verification/effects-open --scenario 0 --quality 48 --frames 360
```

The helper enables Vulkan synchronization validation, runs at 1280×720 with a
fixed requested 1/60 s clock and records real PPM frames, `frames.csv`, a final
mass/temperature summary, native log and JSON manifest. The manifest includes
before/after source identity, executable/shader SHA-256, GPU/driver, exact command
and separate simulation/light/ray GPU timings. A source change or native failure
returns nonzero. Build before capturing and avoid concurrent GPU work when
comparing timings. `--view 1` uses the opposite direction; `--view 2` starts the
camera inside the source. Scenario numbers are 0..5 in the UI order above.
`--view 3` gives a closer view. `--light 0..3` selects the default, opposite sun,
high sun, or dark illumination reference.
`--view 4` frames the rising plume through four seconds. `--capture-every 2`
saves actual frames at a 30 Hz cadence for a fixed 60 Hz requested clock; it
increases readback cost and should be measured separately from sparse captures.
`--max-speed` controls the CFL velocity bound (default 12 m/s). Check clamps when
changing it; a faster run with clipped velocities is not equivalent physics.

The native executable also accepts `--smoke-test --validate --clean --scenario N
--view N --quality N --frames N --output PATH`. Captures occur at frames 8, 20,
60, 150 and the final frame. Output directories are local build artifacts.

This is a bounded local low-Mach deflagration and smoke model. It does not solve
blast shock waves or liquid fuel sprays. See the engine's
[implemented model and limits](https://github.com/traidy2222/TheOne/blob/main/docs/VOLUMES.md).
Numerical checks, image review and measured timings have distinct meanings;
passing validation alone is not a photorealism claim.
