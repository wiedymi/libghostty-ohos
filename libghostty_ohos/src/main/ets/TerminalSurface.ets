import { common } from '@kit.AbilityKit';
import { display } from '@kit.ArkUI';
import { pasteboard } from '@kit.BasicServicesKit';
import { TerminalController } from './TerminalController';
import { DEFAULT_TERMINAL_CONFIG, TerminalConfig, TerminalLinkListener, TerminalSearchStatus } from './TerminalTypes';

@Component
export struct TerminalSurface {
  controller: TerminalController = new TerminalController();
  surfaceId: string = 'terminalSurface';
  searchInputId: string = 'terminalSearchInput';
  surfaceColor: string = '#0B0D10';
  onLinkActivated: TerminalLinkListener | null = null;

  private context: common.UIAbilityContext | null = null;
  private searchInputController: TextInputController = new TextInputController();
  private fallbackTimer: number = -1;
  private contextMenuSelectedText: string = '';
  private contextMenuScreenContent: string = '';
  private contextMenuCanPaste: boolean = false;

  @State private appliedFontSize: number = DEFAULT_TERMINAL_CONFIG.fontSize;
  @State private useTextFallback: boolean = false;
  @State private fallbackContent: string = '';
  @State private fallbackError: string = '';
  @State private searchVisible: boolean = false;
  @State private searchText: string = '';
  @State private searchTotal: number = 0;
  @State private searchSelected: number = -1;

  aboutToAppear(): void {
    this.context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    this.controller.setConfigListener((config: TerminalConfig) => {
      this.appliedFontSize = config.fontSize;
    });
    this.controller.setLinkListener((url: string) => {
      this.handleLinkActivated(url);
    });
    this.appliedFontSize = this.controller.getConfig().fontSize;
  }

  aboutToDisappear(): void {
    this.stopFallbackPolling();
    this.controller.setConfigListener(null);
    this.controller.setLinkListener(null);
  }

  build() {
    Stack() {
      this.buildTerminalXComponent();
      if (this.useTextFallback) {
        this.buildTextFallback();
      }
      if (this.searchVisible) {
        this.buildSearchOverlay();
      }
    }
    .width('100%')
    .height('100%')
    .padding(0)
    .backgroundColor(this.surfaceColor)
  }

  @Builder
  private buildTerminalXComponent() {
    XComponent({
      id: this.surfaceId,
      type: XComponentType.SURFACE,
      libraryname: 'libghostty_ohos'
    })
      .onLoad((nativeContext?: Object) => {
        if (!nativeContext || !this.context) {
          return;
        }

        this.controller.bindNative(nativeContext, this.context, this.getDisplayDensity());
        this.useTextFallback = !this.controller.isRendererReady();
        this.fallbackError = this.controller.getRendererError();
        this.appliedFontSize = this.controller.getConfig().fontSize;
        if (this.useTextFallback) {
          this.syncFallbackScreen();
          this.startFallbackPolling();
        } else {
          this.stopFallbackPolling();
        }
      })
      .onDestroy(() => {
        this.stopFallbackPolling();
        this.controller.unbindNative();
      })
      .onAreaChange((_oldValue: Area, newValue: Area) => {
        const density = this.getDisplayDensity();
        const globalX = Number(newValue.globalPosition?.x ?? 0);
        const globalY = Number(newValue.globalPosition?.y ?? 0);
        const width = Number(newValue.width ?? 0);
        const height = Number(newValue.height ?? 0);
        if (Number.isFinite(globalX) && Number.isFinite(globalY) &&
          Number.isFinite(width) && Number.isFinite(height)) {
          this.controller.setSurfaceFrame(
            globalX * density,
            globalY * density,
            width * density,
            height * density
          );
        }
      })
      .onTouch((_event?: TouchEvent) => {
        if (this.searchVisible) {
          this.searchInputController.stopEditing();
        }
        this.requestTerminalFocus();
      })
      .focusable(true)
      .focusOnTouch(true)
      .defaultFocus(true)
      .width('100%')
      .height('100%')
      .backgroundColor(this.surfaceColor)
      .bindContextMenu(this.buildTerminalContextMenu(), ResponseType.RightClick)
  }

  @Builder
  private buildTextFallback() {
    Scroll() {
      Text(this.fallbackContent.length > 0 ? this.fallbackContent : this.fallbackError)
        .width('100%')
        .textAlign(TextAlign.Start)
        .fontSize(this.appliedFontSize)
        .fontFamily('monospace')
        .fontColor('#D8DEE9')
        .lineHeight(this.appliedFontSize * 1.35)
        .padding(12)
    }
    .scrollBar(BarState.Off)
    .edgeEffect(EdgeEffect.None)
    .width('100%')
    .height('100%')
    .backgroundColor(this.surfaceColor)
  }

  @Builder
  private buildSearchOverlay() {
    Column() {
      Row({ space: 8 }) {
        TextInput({ text: this.searchText, controller: this.searchInputController })
          .id(this.searchInputId)
          .width(220)
          .height(38)
          .fontSize(15)
          .backgroundColor('#FF1C222B')
          .fontColor('#FFF4F7FB')
          .borderRadius(8)
          .onChange((value: string) => {
            this.searchText = value;
            this.controller.updateSearch(value);
            this.syncSearchStatus();
          })
          .onSubmit(() => {
            this.navigateSearch(true);
          })

        Text(this.searchLabel())
          .fontSize(13)
          .fontColor('#FFB8C4D9')
          .width(64)
          .textAlign(TextAlign.Center)

        Button('Prev')
          .height(34)
          .fontSize(13)
          .onClick(() => {
            this.navigateSearch(false);
          })

        Button('Next')
          .height(34)
          .fontSize(13)
          .onClick(() => {
            this.navigateSearch(true);
          })

        Button('Close')
          .height(34)
          .fontSize(13)
          .onClick(() => {
            this.closeSearch();
          })
      }
      .padding({ left: 12, right: 12, top: 10, bottom: 10 })
      .backgroundColor('#EE11161D')
      .borderRadius(12)
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Start)
    .alignItems(HorizontalAlign.End)
    .padding({ top: 12, right: 12 })
    .hitTestBehavior(HitTestMode.Transparent)
  }

  private getDisplayDensity(): number {
    try {
      return display.getDefaultDisplaySync().densityPixels;
    } catch (err) {
      console.error('Failed to get display density: ' + JSON.stringify(err));
      return 1;
    }
  }

  private requestTerminalFocus(): void {
    focusControl.requestFocus(this.surfaceId);
  }

  private startFallbackPolling(): void {
    if (this.fallbackTimer >= 0) {
      return;
    }

    this.fallbackTimer = setInterval(() => {
      this.syncFallbackScreen();
    }, 100) as number;
  }

  private stopFallbackPolling(): void {
    if (this.fallbackTimer < 0) {
      return;
    }

    clearInterval(this.fallbackTimer);
    this.fallbackTimer = -1;
  }

  private syncFallbackScreen(): void {
    if (!this.controller.isAttached()) {
      return;
    }

    try {
      const content = this.controller.getScreenContent();
      if (content.length > 0) {
        this.fallbackContent = content;
      } else if (this.fallbackError.length > 0) {
        this.fallbackContent = this.fallbackError + '\n\nWaiting for terminal output...';
      }
    } catch (err) {
      this.fallbackContent = 'Failed to read terminal buffer';
      console.error('Failed to sync fallback screen: ' + JSON.stringify(err));
    }
  }

  private handleLinkActivated(url: string): void {
    if (url.length === 0) {
      return;
    }

    if (this.onLinkActivated) {
      this.onLinkActivated(url);
      return;
    }

    if (!this.context) {
      return;
    }

    this.context.openLink(url).catch((err: Object) => {
      console.error('Failed to open terminal link: ' + JSON.stringify(err));
    });
  }

  @Builder
  private buildTerminalContextMenu() {
    Menu() {
      MenuItem({ content: 'Find' })
        .onClick(() => {
          this.openSearch(this.searchVisible ? this.searchText : '');
        })
      if (this.hasTerminalSelection()) {
        MenuItem({ content: 'Find Selection' })
          .onClick(() => {
            this.openSearch(this.controller.getSelectedText());
          })
      }
      if (this.hasTerminalSelection()) {
        MenuItem({ content: 'Copy' })
          .onClick(() => {
            this.copyToClipboard(this.controller.getSelectedText());
          })
      }
      if (this.hasClipboardContents()) {
        MenuItem({ content: 'Paste' })
          .onClick(() => {
            this.pasteFromClipboard();
          })
      }
      if (this.hasTerminalSelection()) {
        MenuItem({ content: 'Clear Selection' })
          .onClick(() => {
            this.controller.clearSelection();
            this.requestTerminalFocus();
          })
      }
      if (this.hasTerminalScreenContent()) {
        MenuItem({ content: 'Copy Screen' })
          .onClick(() => {
            this.copyToClipboard(this.controller.getScreenContent());
          })
      }
    }
  }

  private hasTerminalSelection(): boolean {
    return this.controller.getSelectedText().length > 0;
  }

  private searchLabel(): string {
    if (this.searchTotal <= 0) {
      return '0/0';
    }
    if (this.searchSelected >= 0) {
      return `${this.searchSelected + 1}/${this.searchTotal}`;
    }
    return `-/${this.searchTotal}`;
  }

  private openSearch(initialQuery: string): void {
    const nextQuery = initialQuery.length > 0 ? initialQuery : this.searchText;
    this.searchVisible = true;
    this.searchText = nextQuery;
    this.controller.startSearch(nextQuery);
    this.syncSearchStatus();
    setTimeout(() => {
      focusControl.requestFocus(this.searchInputId);
    }, 0);
  }

  private navigateSearch(next: boolean): void {
    this.controller.navigateSearch(next);
    this.syncSearchStatus();
    setTimeout(() => {
      focusControl.requestFocus(this.searchInputId);
    }, 0);
  }

  private closeSearch(): void {
    this.searchInputController.stopEditing();
    this.controller.endSearch();
    this.searchVisible = false;
    this.searchText = '';
    this.searchTotal = 0;
    this.searchSelected = -1;
    setTimeout(() => {
      this.requestTerminalFocus();
    }, 0);
  }

  private syncSearchStatus(): void {
    const status: TerminalSearchStatus = this.controller.getSearchStatus();
    this.searchVisible = status.active || this.searchVisible;
    this.searchTotal = status.total;
    this.searchSelected = status.selected;
    this.searchText = status.query;
  }

  private hasTerminalScreenContent(): boolean {
    return this.controller.getScreenContent().trim().length > 0;
  }

  private hasClipboardContents(): boolean {
    try {
      return pasteboard.getSystemPasteboard().hasDataSync();
    } catch (err) {
      console.error('Failed to query terminal clipboard state: ' + JSON.stringify(err));
      return false;
    }
  }

  private copyToClipboard(text: string): void {
    if (text.length === 0) {
      return;
    }
    const clipboard = pasteboard.getSystemPasteboard();
    try {
      clipboard.setDataSync(pasteboard.createPlainTextData(text));
    } catch (err) {
      console.error('Failed to copy terminal text: ' + JSON.stringify(err));
    }
    this.requestTerminalFocus();
  }

  private pasteFromClipboard(): void {
    const clipboard = pasteboard.getSystemPasteboard();
    try {
      if (!clipboard.hasDataSync()) {
        return;
      }

      const data = clipboard.getDataSync();
      let text = data.getPrimaryText();
      if (text.length === 0 && data.getRecordCount() > 0) {
        const primaryRecord = data.getRecord(0);
        text = primaryRecord.toPlainText();
      }
      if (text.length > 0) {
        this.controller.write(text);
      }
    } catch (err) {
      console.error('Failed to paste into terminal: ' + JSON.stringify(err));
    }
    this.requestTerminalFocus();
  }
}
