# Stellar sandbox

A separate native game sandbox for the engine's finite stellar emitter model.
The star radiates from a physical sphere; the sandbox supplies original test-body
geometry and observer controls. It uses no directional sun or ambient sky fill.

```powershell
.\games\stellar-sandbox\Play-Stellar.ps1 -Validate
```

Right mouse looks; WASD flies; Q/E moves vertically; Shift boosts; R resets the
camera. The observer is a survey camera, not a simulated relativistic spacecraft.
The default opens the whole-star EUV diagnostic. **Whole star** frames the plasma;
**Convection close-up** resolves the surface cells. Use **Star + crescent** or
**Lit side** to return to the test-body arrangement.

**Observer** selects visible continuum, false-colour EUV or a visible-light
coronagraph. These are camera views; EUV colours do not recolour light falling on
the bodies. Visible continuum is mostly white and its corona is faint. The
coronagraph masks the disk and raises exposure to reveal scattered light.

**Surface + plasma** enables the optional activity model. Adjust magnetic activity,
convection contrast and plasma ray steps. Evolution starts at **120x** speed,
explicitly shown in the panel; **Pause evolution** freezes it and **Replay** resets
the deterministic clock. Granules have a representative ten-minute timescale at
1x. The arches are prescribed procedural plasma structures, not an MHD solver.

The panel changes distance in AU, star temperature and radius. **1 parsec** moves
the test region to a stellar distance; the same inverse-square law remains in use.
The CPU retains world positions in double-precision metres and rebases render
geometry into local 1000 km units. The default airless rocky test body has an
Earth-sized radius but is not an Earth simulation. Close to the star it would be
extremely hot; thermal evolution is not modeled.

**Automatic exposure** changes the camera's display multiplier as distance changes,
with a bounded range. Disable it to compare raw apparent brightness at a fixed
exposure. The physical flux readout always remains in W/m² independently of the
camera. A star's visible surface brightness and its angular size are distinct:
moving away makes it smaller and reduces received flux.

Enable **Eclipsing body** and change **Occulter offset** to inspect total and
partial shadow. Visible bodies are matched by analytic opaque sphere occluders.
There are no planetary atmospheric halos, dust glow or unrelated fill lights.

For targeted captures:

```powershell
$stellar = (Get-Content build/launch/stellar-Release.txt -Raw).Trim()
& $stellar --smoke-test --validate `
  --clean --scenario 0 --output build/verification/stellar-near
```

Scenarios 0..5: near star/crescent, 1 AU, total eclipse, partial eclipse, 1 parsec,
and the lit-side view. These six retain activity disabled as lighting regressions.
Scenarios 6..10 are whole-star EUV, convection close-up, coronagraph, whole-star
visible continuum and evolved EUV at 900 simulated seconds. `--time S` overrides
the stellar clock for deterministic captures; smoke tests freeze time by default.
`--frames N` controls the capture length. A final native
PPM and per-frame physical flux/visibility/exposure/timing CSV are written to the
selected output directory. No commercial assets are required.

See the engine [model and limits](https://github.com/traidy2222/TheOne/blob/main/docs/STELLAR_LIGHTING.md).
