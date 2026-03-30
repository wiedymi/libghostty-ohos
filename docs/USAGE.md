# Usage Guide

This guide covers the current public API of `libghostty-ohos`.

## Install The HAR

In the consuming module's `oh-package.json5`:

```json5
{
  "dependencies": {
    "libghostty-ohos": "file:../libghostty_ohos"
  }
}
```

Then install dependencies:

```sh
ohpm install
```

## Basic Embed

```ts
import {
  DEFAULT_TERMINAL_CONFIG,
  TerminalController,
  TerminalSurface
} from 'libghostty-ohos';

@Entry
@Component
struct TerminalPage {
  private controller: TerminalController = new TerminalController();

  aboutToAppear(): void {
    this.controller.setConfig({
      fontSize: 15,
      scrollbackLines: 15000,
      bgColor: DEFAULT_TERMINAL_CONFIG.bgColor,
      fgColor: DEFAULT_TERMINAL_CONFIG.fgColor,
      cursorStyle: DEFAULT_TERMINAL_CONFIG.cursorStyle,
      cursorBlink: DEFAULT_TERMINAL_CONFIG.cursorBlink
    });
    this.controller.setTheme('Aizen_Dark');
  }

  build() {
    Column() {
      TerminalSurface({
        controller: this.controller,
        surfaceId: 'terminal-surface-main',
        surfaceColor: '#0B0D10'
      })
    }
    .width('100%')
    .height('100%')
  }
}
```

## TerminalSurface

`TerminalSurface` is the only UI component exposed by the library.

Props:

- `controller: TerminalController`
- `surfaceId?: string`
- `surfaceColor?: string`

Notes:

- `surfaceId` must be unique per visible terminal instance.
- `surfaceColor` only affects the surrounding ArkUI surface. The terminal palette itself still comes from the terminal config and theme.
- The component manages the `XComponent`, native IME bridge, and lifecycle internally.

## TerminalController

Create one controller per terminal instance.

### Input And Viewport Control

- `write(data: string)`
- `feed(data: string)`
- `resize(cols: number, rows: number)`
- `scrollView(delta: number)`
- `resetScroll()`

### Appearance

- `setConfig(config: TerminalConfig)`
- `updateConfig(patch: TerminalConfigPatch)`
- `getConfig(): TerminalConfig`
- `setTheme(themeName: string): boolean`
- `getThemeName(): string`
- `getThemeList(): string[]`
- `setConfigListener(listener)`
- `setInputListener(listener)`
- `setAttachmentListener(listener)`

### Selection And Queries

- `startSelection(row: number, col: number)`
- `updateSelection(row: number, col: number)`
- `clearSelection()`
- `getSelectedText(): string`
- `getScreenContent(): string`
- `getCursorPosition(): CursorPosition`
- `getCellMetrics(): CellMetrics`
- `getScrollbackSize(): number`
- `isRendererReady(): boolean`
- `getRendererError(): string`

### Lifecycle Notes

- `bindNative()` and `unbindNative()` are public because the surface uses them, but app code should treat them as internal.
- `setConfig()` is safe before the surface attaches. The controller caches the value and applies it later.
- `setTheme()` also caches the chosen theme name before attach, but its return value is only meaningful once the native surface is attached.
- `write()` sends terminal input out to your driver boundary.
- `feed()` pushes driver output into the terminal for rendering and VT state updates.
- Query methods return empty/default values until the controller is attached.

## Multiple Terminals

For tabs or splits, create one controller per terminal and give every surface a distinct ID pair.

```ts
import { TerminalController, TerminalSurface } from 'libghostty-ohos';

@Component
struct SplitTerminalPage {
  private leftController: TerminalController = new TerminalController();
  private rightController: TerminalController = new TerminalController();

  build() {
    Row() {
      TerminalSurface({
        controller: this.leftController,
        surfaceId: 'split-left-surface',
        surfaceColor: '#111318'
      })

      TerminalSurface({
        controller: this.rightController,
        surfaceId: 'split-right-surface',
        surfaceColor: '#111318'
      })
    }
    .width('100%')
    .height('100%')
  }
}
```

Recommended pattern:

- keep controllers in your own tab or pane model
- mount and unmount `TerminalSurface` where you need it
- keep library concerns at the terminal-instance level, not the app-shell level

## Themes

Theme files are bundled as raw resources. The default theme is `Aizen_Dark`.

Example:

```ts
this.controller.setTheme('Builtin_Dark');
```

To list available themes after the surface binds:

```ts
const themes = this.controller.getThemeList();
```

## Selection, Touch, And IME Behavior

- drag vertically to scroll
- long-press to start a selection
- hardware keyboard input is translated into terminal escape sequences
- system IME input is handled through the native IMEKit bridge

## Current Session Model

The library is intentionally transport-neutral.

Today, the native layer:

- creates a terminal when the surface attaches
- translates HarmonyOS input events into terminal byte streams
- emits those byte streams to your app through `setInputListener()`
- renders output only when your app feeds it back through `feed()`

Your app owns PTY, exec, shell, SSH, multiplexing, and session lifecycle.
