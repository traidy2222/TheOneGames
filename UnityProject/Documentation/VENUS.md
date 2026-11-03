# Venus — measured planetary foundation

Open `Assets/Scenes/Venus.unity`. This replaces the miniature planet as the active scene. It represents Venus at its actual physical scale; it is an early exploration and excavation foundation, not a completed whole-planet simulator.

## Controls

- Click to capture the mouse; Esc releases it.
- **1**: visit the Venera 13 landing region. **2**: return to orbit.
- Orbit: mouse rotates the view; wheel changes distance. **G** hides the opaque clouds for an elevation study (not a natural-colour surface photograph).
- Surface: WASD walk; Shift faster; Space jump; F survey flight; Space / Ctrl rise / descend; Home returns to the starting point.
- Left mouse excavates; right mouse deposits; Alt+left mouse grades; wheel adjusts brush; Z undoes a stroke.
- F5 saves; F9 loads. Returning to orbit also saves the local excavation.

## Physical scale and source data

The physical coordinate model uses double-precision metres and a **6,051,800 m mean radius**. Surface gravity is **8.87 m/s²**. Orbital rendering uses kilometres per Unity unit; surface rendering uses metres per Unity unit around a double-precision geographic anchor. The two cameras never mix coordinate units. There is currently an explicit view change, not a seamless descent.

Measured global heights come from USGS **Venus Magellan Global Topography 4641m v02**, an 8192 × 4096 signed 16-bit grid. Its radius datum is **6,051,000 m**, so geocentric surface radius is `6051000 + elevation`, not the mean radius plus the elevation. No vertical exaggeration is applied. Longitude wraps and pixel-centre bilinear sampling are implemented.

The source contains valid data at about **91.7119%** of pixels. Missing samples remain identifiable. Bilinear sampling uses valid neighbours where available; wholly missing regions use the datum radius and appear purple in the elevation-study view. The orbital render mesh samples the dataset onto a 512 × 256 sphere; it does not display every DEM pixel as a vertex. Fine-distance adaptive planetary LOD is not implemented.

The initial site is near **7.5° S, 303° E**, the published Venera 13 landing region. The site uses measured regional elevation plus explicitly procedural metre-scale lava-plate detail. The global DEM cannot resolve individual rocks, dig sites, or the exact lander panorama. Basalt textures are terrestrial CC0 proxies, not images collected on Venus.

## Present limits

The detailed landing region is a 2.56 km square around one geographic anchor. Local excavation uses a finite density field, with a guarded central editing area and bounded underground depth. Regional ground outside the editable field is walkable but not deformable. The current explorer returns to the site when travelling beyond one kilometre. Planet-wide terrain streaming, seamless descent, arbitrary landing locations and persistent excavation across multiple sites are future work.

Cloud patterns, diffuse amber lighting, haze and metre-scale ground shapes are visual reconstructions. The displayed temperature and pressure are reference values, not simulated thermodynamics. There is no vegetation, liquid-water ocean, heat damage, chemical simulation, rock-fracture mechanics or conserved bulk-soil physics. Excavation uses CSG density edits; deposition and grading are approximations.

Saves use the version-2 density format and validate grid dimensions, sample spacing, seed, terrain type, planet-radius parameter and generator version before modifying the field. Old prototype saves are not silently converted. Local saves live in Unity's persistent-data folder as `venus-<latitude>-<longitude>.bin`.

## Bridge

`Tools/Invoke-UnityBridge.ps1` talks to the local editor through `Library/TerrainBridge`. Supported actions include `status`, `refresh`, `venus`, `play`, `stop`, `venus-surface`, `venus-orbit`, `capture`, `validate`, `validate-planet`, and `build`. Scene changes require leaving Play mode. `refresh` is available after this bridge version has been imported; do not recompile while holding unsaved play-mode edits.

The `save` action addresses the live DigTerrain; Venus also saves automatically on `venus-orbit`. Captures render the active camera without the on-screen HUD. Request processing is local and allowlisted; no UI automation, network control endpoint or arbitrary shell execution is used.

## Validation and evidence

`VenusWorld.EditorTools.VenusSetup.BatchVerify` checks dataset dimensions, datum handling, one-metre precision at the global anchor, longitude continuity, geographic round trips, excavation collision, undo, surface traversal and shader compilation. It renders orbital, surface and excavation previews. Results and captures are in `Documentation/venus-*`.

## Sources and attribution

- NASA Venus fact sheet: https://nssdc.gsfc.nasa.gov/planetary/factsheet/venusfact.html
- USGS Magellan DEM metadata: https://astrogeology.usgs.gov/search/map/Venus/Magellan/RadarProperties/Venus_Magellan_Topography_Global_4641m_v02.cub.xml
- Data: https://planetarymaps.usgs.gov/mosaic/Venus_Magellan_Topography_Global_4641m_v02.tif
- NASA Venera 13 location and surface reference: https://nssdc.gsfc.nasa.gov/imgcat/html/object_page/v13_yg06847.html
- USGS / Magellan Global Topography Data Record / Ford and Pettengill / NASA PDS. Source metadata identifies the dataset as public domain and requests author citation.
- Poly Haven Dark Rock, CC0: https://polyhaven.com/a/dark_rock . Map download provenance and hashes are stored alongside the textures.

`Assets/Venus/Resources/VenusDem-provenance.json` records the original GeoTIFF SHA-256, coverage, encoding and datum. The runtime asset retains the original signed samples in a gzip payload. Earlier landscape and miniature-planet scenes remain development prototypes; Venus is the only enabled build scene.

## New active flight scene

`Assets/Scenes/PlanetaryFlight.unity` now provides continuous space-to-surface flight using the same Magellan source. This document describes the retained historical Venus explorer. See `CONTINUOUS_FLIGHT.md` for the current scene, controls, shared planet definitions, streaming and limits.
