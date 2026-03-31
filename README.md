# libghostty-ohos

`libghostty-ohos` is a HarmonyOS `HAR` library for embedding a terminal surface in your own app.

It packages a native renderer, a Ghostty-powered terminal core, and a small ArkTS control surface. You bring the app-level UX: tabs, splits, settings, toolbar actions, session management, and persistence.

<img width="3120" height="2080" alt="Screenshot_2026-03-30T221237" src="https://github.com/user-attachments/assets/42905cc8-2233-438d-a6e3-d461f7da06ce" />


## What This Library Provides

- `TerminalSurface`: an ArkTS component backed by an `XComponent` surface
- `TerminalController`: imperative control over terminal input/output wiring, scrollback, selection, theme, and config
- bundled terminal themes and glyph fallback font assets
- a native `.so` built around `libghostty-vt`

## What This Library Does Not Provide

- terminal tabs, pane splits, or window management
- settings screens, preferences storage, or app chrome
- PTY, shell, exec, or SSH transport drivers
- terminal session orchestration beyond the controller/driver boundary

Current behavior is intentionally small: one `TerminalController` controls one terminal instance, and one `TerminalSurface` presents it.

## Install

Install the package from OHPM:

```sh
ohpm install libghostty-ohos
```

Or add the HAR from OHPM in your consuming module:

```json5
{
  "dependencies": {
    "libghostty-ohos": "^0.1.0"
  }
}
```

Then run:

```sh
ohpm install
```

For local development, you can also add the HAR as a file dependency:

```json5
{
  "dependencies": {
    "libghostty-ohos": "file:../libghostty_ohos"
  }
}
```

If your project enables normalized OHM URLs, keep the dependency key exactly equal to the package name: `libghostty-ohos`.

## Quick Start

```ts
import { TerminalController, TerminalSurface } from 'libghostty-ohos';

@Entry
@Component
struct TerminalPage {
  private controller: TerminalController = new TerminalController();

  aboutToAppear(): void {
    this.controller.updateConfig({ fontSize: 16, scrollbackLines: 20000 });
    this.controller.setTheme('Aizen_Dark');
    this.controller.setInputListener((data: string) => {
      this.driver.write(data);
    });
  }

  build() {
    Stack() {
      TerminalSurface({
        controller: this.controller,
        surfaceId: 'main-terminal-surface',
        surfaceColor: '#0B0D10'
      })
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#0B0D10')
  }
}
```

## Public API

Exported from `libghostty-ohos`:

- `TerminalSurface`
- `TerminalController`
- `CursorPosition`
- `CellMetrics`
- `TerminalConfig`
- `TerminalConfigPatch`
- `TerminalInputListener`
- `TerminalAttachmentListener`
- `DEFAULT_TERMINAL_CONFIG`
- `DEFAULT_THEME_NAME`

Important usage rules:

- Create one `TerminalController` per terminal.
- Do not reuse the same `surfaceId` across visible surfaces.
- `TerminalSurface` owns the native bind/unbind lifecycle. App code should not call `bindNative()` or `unbindNative()`.
- Config is cached on the controller and applied when the surface binds.
- Query methods such as `getThemeList()`, `isRendererReady()`, and `getRendererError()` only become meaningful after the surface is attached.

## Multi-Terminal Apps

This library is meant to be composed into your own terminal UX.

- For tabs: keep one controller per tab and mount the active `TerminalSurface`.
- For splits: render multiple `TerminalSurface` instances side by side, each with its own controller and unique IDs.
- For custom actions: drive the controller directly with `write()`, `feed()`, `scrollView()`, `clearSelection()`, `setTheme()`, and `updateConfig()`.

## Current Runtime Behavior

- A terminal instance starts when the surface binds.
- User input from hardware keyboard, IME, paste, or `controller.write()` is emitted to your app-owned driver.
- Driver output is rendered by feeding it back through `controller.feed()`.
- Touch drag scrolls the viewport. Long-press enters selection mode. Hardware keyboard input is translated to terminal escape sequences.

## Build From Source

Refresh the upstream terminal archive:

```sh
./tools/build-ghostty-vt-docker.sh
```

Build the example app:

```sh
/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor/bin/hvigor.js assembleApp -m project --no-daemon
```

## Docs

- [Usage Guide](docs/USAGE.md)
- [Build Notes](docs/BUILD.md)
- [Third-Party Notices](THIRD_PARTY_NOTICES.md)
- [License](LICENSE)

## Project Layout

- `libghostty_ohos/`: reusable HAR library
- `example/`: minimal example app consuming the HAR and wiring an app-owned driver
- `tools/build-ghostty-vt-docker.sh`: refreshes the upstream `libghostty_vt.a` archive in Docker
- `tools/build-fish-ohos.sh`: rebuilds the bundled fish/starship/fastfetch HNP used by the example app
