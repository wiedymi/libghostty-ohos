# libghostty-ohos

`libghostty-ohos` is a HarmonyOS HAR library for embedding a Ghostty-powered terminal surface in your app.

It provides:

- `TerminalSurface`: an ArkTS component backed by an `XComponent` surface
- `TerminalController`: imperative control over terminal input/output wiring, scrollback, selection, search, theme, and config
- a native renderer and terminal core powered by `libghostty-vt`

## Install

Add the HAR as a dependency:

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

## Basic Usage

```ts
import { TerminalController, TerminalSurface } from 'libghostty-ohos';

@Entry
@Component
struct TerminalPage {
  private controller: TerminalController = new TerminalController();

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
  }
}
```

## License

MIT
