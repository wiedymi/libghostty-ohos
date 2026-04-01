# Example

This directory shows the minimal way to embed `libghostty-ohos` in a HarmonyOS app.

The full runnable sample app lives in the repo root at [`example/`](../../example), but the snippet below is enough to wire the HAR into your own page.

## Install

```sh
ohpm install libghostty-ohos
```

## Minimal Usage

```ts
import { TerminalController, TerminalSurface } from 'libghostty-ohos';

@Entry
@Component
struct TerminalPage {
  private controller: TerminalController = new TerminalController();

  aboutToAppear(): void {
    this.controller.updateConfig({
      fontSize: 16,
      scrollbackLines: 20000
    });
    this.controller.setTheme('Aizen_Dark');
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

## Driver Wiring

`libghostty-ohos` renders the terminal UI, but your app still owns the transport layer.

- Send terminal output back into the surface with `controller.feed(data)`.
- Capture terminal input with `controller.setInputListener(...)`.
- Provide PTY, shell, or SSH transport in your own app code.
