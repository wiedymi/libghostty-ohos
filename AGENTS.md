# AGENTS.md

## Repo Summary

This repository contains a HarmonyOS terminal library and a minimal example app:

- `libghostty_ohos/`: reusable HAR module published as `@wiedymi/libghostty-ohos`
- `example/`: example app that consumes the HAR and provides shell / PTY / SSH wiring
- `tools/build-ghostty-vt-docker.sh`: rebuilds the bundled native `libghostty_vt.a` archive in Docker

The library boundary matters:

- `libghostty_ohos` owns terminal rendering, controller state, native bindings, themes, and bundled assets.
- `example` owns app UX and transport integration. Do not move app-specific session logic into the library unless the task explicitly requires expanding the public API.

## Key Source Layout

### ArkTS surface and controller

- `libghostty_ohos/src/main/ets/TerminalSurface.ets`
- `libghostty_ohos/src/main/ets/TerminalController.ets`
- `libghostty_ohos/src/main/ets/TerminalTypes.ets`

### Native terminal / renderer

- `libghostty_ohos/src/main/cpp/napi_init.cpp`
- `libghostty_ohos/src/main/cpp/terminal/terminal.cpp`
- `libghostty_ohos/src/main/cpp/terminal/terminal_state.cpp`
- `libghostty_ohos/src/main/cpp/renderer/native_drawing_renderer.cpp`
- `libghostty_ohos/src/main/cpp/CMakeLists.txt`

### Example app integration

- `example/src/main/ets/pages/Index.ets`
- `example/src/main/ets/drivers/ExampleShellDriver.ets`
- `example/src/main/cpp/example_driver.cpp`
- `example/src/main/cpp/pty/pty_handler.cpp`
- `example/src/main/cpp/ssh/ssh_session.cpp`

## Working Rules

- Preserve the library/example split. Put reusable terminal behavior in `libghostty_ohos`; keep app demo behavior in `example`.
- Prefer minimal public API changes. If you change exported behavior in `TerminalController`, `TerminalSurface`, or native N-API bindings, keep ArkTS and C++ in sync.
- Treat `libghostty_ohos/prebuilt/arm64-v8a/libghostty_vt.a` as generated input. Rebuild it with the script; do not hand-edit prebuilt artifacts.
- Do not add noisy per-frame or per-cell logs in rendering paths. Logging inside `beginFrame`, `endFrame`, paint loops, or input hot paths must be gated to real failures or explicit debug-only instrumentation.
- Keep changes scoped. This repo is performance-sensitive in the renderer and state update paths.

## Build And Validation

Install dependencies:

```sh
ohpm install
```

Rebuild the upstream terminal archive when native Ghostty integration changes:

```sh
./tools/build-ghostty-vt-docker.sh
```

Build the full project:

```sh
/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor/bin/hvigor.js assembleApp -m project --no-daemon
```

Notes:

- The checked-in root `build-profile.json5` does not include signing config for packaging the example app.
- HAR-only work may be partially validated by building the project graph, but full app packaging requires local signing config.

## Change Guidance

For ArkTS changes:

- Verify controller/surface lifecycle behavior, especially attach/detach, input listener wiring, and config propagation.
- Keep exported types and defaults coherent with the README and usage docs.

For native C++ changes:

- Verify `libghostty_ohos/src/main/cpp/CMakeLists.txt` still matches any new files or link requirements.
- Preserve error handling around `OH_NativeWindow`, `OH_NativeBuffer`, and N-API resource ownership.
- Prefer fixing behavior at the source instead of masking issues in the example app.

For example app changes:

- Keep the example minimal and representative of library usage.
- Do not introduce demo-only assumptions into the reusable HAR API unless required by the task.

## Documentation Hygiene

Update docs when behavior changes materially:

- `README.md` for public API or setup changes
- `docs/USAGE.md` for integration guidance
- `docs/BUILD.md` for toolchain or build workflow changes
- `THIRD_PARTY_NOTICES.md` when third-party libraries, fonts, or bundled materials change

## Useful Context

- Root `build-profile.json5` defines the project modules: `libghostty_ohos` and `example`.
- `libghostty_ohos/build-profile.json5` points native builds at `src/main/cpp/CMakeLists.txt`.
- The example app is the fastest place to validate end-to-end behavior, but the library should remain independently reusable.
