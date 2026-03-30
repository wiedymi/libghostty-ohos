import { common } from '@kit.AbilityKit';
import { TerminalController, TerminalSurface } from 'libghostty_ohos';
import { ExampleShellDriver } from '../drivers/ExampleShellDriver';

@Entry
@Component
struct TerminalExamplePage {
  private controller: TerminalController = new TerminalController();
  private driver: ExampleShellDriver = new ExampleShellDriver();
  private context: common.UIAbilityContext | null = null;

  aboutToAppear(): void {
    this.context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    if (!this.context) {
      return;
    }
    this.driver.attach(this.controller, this.context);
  }

  aboutToDisappear(): void {
    this.driver.detach();
  }

  build() {
    Stack() {
      TerminalSurface({
        controller: this.controller,
        surfaceId: 'exampleSurface',
        surfaceColor: '#0B0D10'
      })
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#0B0D10')
  }
}
