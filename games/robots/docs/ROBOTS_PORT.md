# Robots port investigation

Status: a **playable exploration prototype** runs in The One; it is not a
complete port of the original game. The supplied Windows archive contains the
original executable and data, not the game source.
Running the original executable is not a port. The source ZIP and extracted
copyrighted game data remain local in `build/` and are not part of this project.

## Archive inventory

`tools/robots_archive.py` reads the packed little-endian version 7 descriptors
from this copy. It validates record boundaries, locations and container sizes,
lists decoded original paths, and can extract using safe generated filenames:

```powershell
python tools/robots_archive.py path/to/Filelist.bin > inventory.json
python tools/robots_archive.py path/to/Filelist.bin --extract path/to/empty-output > inventory.json
```

It requires the matching numbered containers alongside the descriptor. It does
not overwrite existing extracted files. The original embedded paths are metadata
only and are never used as output paths. Filename decoding follows
[eurotools/eurochef](https://github.com/eurotools/eurochef), `eurochef-filelist/src/path.rs`,
commit `71dca6b7b6f673e7c3027d7f88579b8b7d6c3f61`, MIT licensed.

For this copy, `Filelist.bin` indexes 253 resources: 179 `MOEG` (EngineX EDB),
69 `MUSX` audio banks and five other resources. `File_USA.bin` indexes 15 more
`MUSX` banks. All 268 base outputs were compared by SHA-256 with their
indexed source byte ranges. EDB headers can describe additional trailing data:
154 resources have such a range, bounded before the next indexed entry in this
copy. Pass `--full-edb` when extracting those resources for conversion. Entry
81 is `m01_vill.edb` and entry 165 is `p01_rod.edb`.

Eurochef was built locally at the commit above. The base-only files caused read
failures; with trailing data included, Rodney's entities and the village and
city maps convert. Three village and 14 city placement models are missing due
to unsupported entity types. `8200000f.gltf` reports 6,410 triangles and four
images, but no skin or clips; Rodney currently remains in a static pose.

The original `hashcodes.h` names `0x020001c9` as a silver scrap bolt; its
converted model replaces the prototype boxes. Eurochef's `triggers_robots.yml`
identifies type 25 as a pickup. We place that bolt at all 203 village and 241
city type-25 trigger positions. The type's exact subtypes, activation flags,
values and progression dependencies have not been recovered; using one model
and proximity collection for all of them is an approximation. Counts are not
original mission objectives and may include inaccessible or gated items.

The text database was extracted into `build/robots-converted/text`; its initial
village dialogue mentions double jumping, Wonderbot parts and meeting Rodney's
father. The double jump is a new approximation informed by that text, not a
recovered implementation. Original missions, dialogue interaction, combat,
camera logic, most NPCs and character animation remain unimplemented.

Local [vgmstream](https://github.com/vgmstream/vgmstream) release `r2168`
decoded the game's village-diner and city music from MUSX/DAT4 to PCM WAV.
The ZIP SHA-256 is
`692d45070852a35c1fa42726aa3c9de528e44eec925d37d99b1074256b99a229`.
Music repeats the complete decoded stream; original loop boundaries and
context-dependent track switching are not reproduced. The audio stream was
submitted to SDL's dummy output during testing, not independently heard.

## Play the local prototype

Run `./Play-Robots.ps1` for Village, or `./Play-Robots.ps1 -Level City` for
Robot City. Move with WASD, hold right mouse to look, and press Space twice for
a double jump. Touch the spinning bolt models to collect them; R resets position.
F1 opens the sandbox editor. There is no in-game transition between the maps.
The launcher uses local generated assets and stores saves separately under
`build/robots-runtime/village` and `build/robots-runtime/city`.

Both assembled maps rendered in isolated 120-frame Vulkan validation runs with
music submitted to dummy output. The City capture shows the chosen original
player-trigger location after scene reload. Automated core checks exercise the
double-jump budget and an airborne jump. These tests do not establish a full
playthrough, fidelity, complete collision coverage or that every pickup is
reachable. The graphics, movement, camera and original audio behavior need
further tuning.

## Next port milestones

1. Convert and inspect the village and character EDB resources, including
   textures, mesh geometry, scale, coordinate system and animation. Eurochef
   advertises partial Robots 248 and PC mesh support; its coverage is not a
   guarantee for these specific files.
2. Load a representative converted scene in The One, validate collision and
   rendering, and measure performance. Recover or recreate character animation.
3. Reimplement and compare controls, camera, interactions, enemies, objectives,
   audio and game progression against original behavior. The binary does not
   automatically translate into the engine's C++ gameplay module.
4. Build and test a complete vertical slice before attempting the remaining
   levels, cinematics, saves and platform-specific behavior.
