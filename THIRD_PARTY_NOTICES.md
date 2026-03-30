# Third-Party Notices

This project redistributes or links third-party software and assets as part of the `libghostty-ohos` HarmonyOS library.

This notice is organized by how each dependency enters the build:

- components merged into the generated `libghostty_vt.a`
- packaged runtime assets shipped with the HAR

## 1. Components Merged Into `libghostty_vt.a`

The script `tools/build-ghostty-vt-docker.sh` builds upstream Ghostty in Docker and merges multiple static archives into:

`libghostty_ohos/prebuilt/arm64-v8a/libghostty_vt.a`

| Component | Role In This Project | License | Upstream |
| --- | --- | --- | --- |
| Ghostty / libghostty-vt | terminal core and terminal state machine | MIT | https://github.com/ghostty-org/ghostty |
| simdutf | SIMD UTF processing used by the upstream Ghostty build | Apache-2.0 OR MIT | https://github.com/simdutf/simdutf |
| Google Highway | SIMD support used by the upstream Ghostty build | Apache-2.0 OR BSD-3-Clause | https://github.com/google/highway |

Notes:

- `libghostty_vt.a` is generated outside this repository's source tree and then copied into the HarmonyOS module.
- If the upstream Ghostty build begins pulling in additional redistributable libraries, update this notice.

## 2. Packaged Runtime Assets

| Asset | Role In This Project | License Status | Source | Local Path |
| --- | --- | --- | --- | --- |
| Symbols Nerd Font Mono Regular | glyph fallback font used by the renderer | See upstream release metadata and embedded font metadata | https://github.com/ryanoasis/nerd-fonts | `libghostty_ohos/src/main/resources/rawfile/fonts/SymbolsNerdFontMono-Regular.ttf` |

Notes:

- The bundled font metadata identifies it as `Symbols Nerd Font Mono 3.4.0`, patched with Nerd Fonts.
- The Nerd Fonts project aggregates and patches materials from multiple upstream font and icon sources, so license handling for released font binaries should be treated as asset-specific rather than assumed from a single repo-wide rule.
- Before external redistribution, confirm the exact license terms for the bundled font asset you ship.

## 3. Theme Files

This project ships theme definitions under:

`libghostty_ohos/src/main/resources/rawfile/themes/`

These files are third-party terminal theme assets in Ghostty theme format. This repository does not currently preserve per-theme provenance or individual author attribution in-tree.

If you redistribute this library outside your own internal use, audit these theme files and extend this notice with per-theme source attribution where required.

## 4. System Libraries

The native module also links HarmonyOS/OpenHarmony SDK libraries such as `ace_ndk.z`, `ace_napi.z`, `hilog_ndk.z`, `rawfile.z`, `native_window`, `native_drawing`, `native_buffer`, and `native_fence`.

Those platform libraries are not vendored in this repository and are governed by the HarmonyOS/OpenHarmony SDK terms.
