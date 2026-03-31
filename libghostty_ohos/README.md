# libghostty-ohos

`libghostty-ohos` is a HarmonyOS HAR library for embedding a Ghostty-powered terminal surface in your app.

It provides:

- `TerminalSurface`: an ArkTS component backed by an `XComponent` surface
- `TerminalController`: imperative control over terminal input/output wiring, scrollback, selection, search, theme, and config
- a native renderer and terminal core powered by `libghostty-vt`

## Install

Install the package from OHPM:

```sh
ohpm install libghostty-ohos
```

Or add it manually in your dependency list:

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
