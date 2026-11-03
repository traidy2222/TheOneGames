# Public source and content provenance

This collection contains source and selected project data, not commercial game
installations or extracted asset packs. Public visibility is not a copyright
clearance or a blanket license for every included component.

| Content | Record and distribution boundary |
| --- | --- |
| Sandbox level, robot, crate, checker and interaction sound | Generated locally by `games/sandbox/tools/generate_fixtures.py`. Small originals are committed; copies used as engine-only fixtures are in TheOne tests. |
| Robots | Source, conversion/research tools and animation tests are committed. Extracted scenes, models, motion and audio remain under ignored `games/robots/build`. See `games/robots/docs` and the retained Eurochef MIT notice. |
| Transformers | Source, catalog/player logic and export scripts are committed. Disc images, extracted/exported models, textures and audio are not committed. Converter patches retain the upstream MIT notice in `games/transformers/tools/patches`. |
| Native Planets and Tornado | Procedural/source implementations and custom shaders are committed. Build products and saves remain local. |
| Unity terrain textures | Local `Assets/DiggableTerrain/Textures/provenance*.json` records identify Poly Haven source URLs, checksums and CC0 labels. These records accompany the committed textures. |
| Unity Venus relief | `Assets/Venus/Resources/VenusDem-provenance.json` records the USGS Astrogeology / NASA PDS Magellan source, checksum, resampling/encoding and public-domain attribution. The derived relief data is committed with that record. |
| Unity documentation captures | Prototype terrain/planet screenshots and historical validation output. These are development evidence, not a current performance or visual-quality guarantee. |
| Unity, Unreal, Blender, Fab and CMake dependencies | External applications/services/dependencies are not vendored as installed runtimes. Retained package manifests and tool documentation describe setup. Account credentials and installed bridge state live outside the repository. |

Native dependency pins and upstream notice locations are documented in
[TheOne dependency provenance](https://github.com/traidy2222/TheOne/blob/main/docs/DEPENDENCIES.md).
No project-wide open-source license is selected by making these repositories
public; existing third-party notices retain their own scopes.
