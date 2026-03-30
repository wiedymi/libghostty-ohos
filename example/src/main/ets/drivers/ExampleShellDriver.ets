import { common } from '@kit.AbilityKit';
import { TerminalAttachmentListener, TerminalController, TerminalInputListener, TerminalSize } from 'libghostty_ohos';
import exampleDriver from 'libexample_driver.so';

interface ExampleDriverNativeBridge {
  initialize: (filesDir: string) => void;
  start: (cols: number, rows: number) => boolean;
  stop: () => void;
  writeInput: (data: string) => void;
  drainOutput: () => string;
  setOutputCallback: (callback: ((data: string) => void) | null) => void;
  resize: (cols: number, rows: number) => void;
}

const nativeBridge = exampleDriver as ExampleDriverNativeBridge;

export class ExampleShellDriver {
  private controller: TerminalController | null = null;
  private context: common.UIAbilityContext | null = null;
  private started: boolean = false;
  private startRetryTimer: number = -1;
  private lastCols: number = 0;
  private lastRows: number = 0;

  private readonly inputListener: TerminalInputListener = (data: string): void => {
    nativeBridge.writeInput(data);
  };

  private readonly outputListener = (data: string): void => {
    if (data.length > 0 && this.controller) {
      this.controller.feed(data);
    }
  };

  private readonly attachmentListener: TerminalAttachmentListener = (attached: boolean): void => {
    if (attached) {
      this.start();
      return;
    }
    this.stop();
  };

  private readonly sizeListener = (size: TerminalSize): void => {
    if (!this.started) {
      return;
    }

    const cols = Math.max(1, size.cols || 0);
    const rows = Math.max(1, size.rows || 0);
    if (cols <= 0 || rows <= 0) {
      return;
    }
    if (cols === this.lastCols && rows === this.lastRows) {
      return;
    }

    console.info(`ExampleShellDriver resize cols=${this.lastCols}->${cols} rows=${this.lastRows}->${rows}`);
    this.lastCols = cols;
    this.lastRows = rows;
    nativeBridge.resize(cols, rows);
  };

  attach(controller: TerminalController, context: common.UIAbilityContext): void {
    this.detach();
    this.controller = controller;
    this.context = context;
    this.controller.setInputListener(this.inputListener);
    this.controller.setAttachmentListener(this.attachmentListener);
    this.controller.setSizeListener(this.sizeListener);
  }

  detach(): void {
    if (this.controller) {
      this.controller.setInputListener(null);
      this.controller.setAttachmentListener(null);
      this.controller.setSizeListener(null);
    }
    this.stop();
    this.controller = null;
    this.context = null;
  }

  private async start(): Promise<void> {
    if (this.started || !this.controller || !this.context) {
      return;
    }

    const size = this.controller.getTerminalSize();
    const cols = Math.max(0, size.cols || 0);
    const rows = Math.max(0, size.rows || 0);
    if (cols <= 0 || rows <= 0) {
      console.info(`ExampleShellDriver waiting for terminal size cols=${cols} rows=${rows}`);
      this.scheduleStartRetry();
      return;
    }
    console.info(`ExampleShellDriver starting cols=${cols} rows=${rows}`);
    nativeBridge.initialize(this.context.filesDir);
    this.started = nativeBridge.start(cols, rows);
    if (!this.started) {
      console.error(`ExampleShellDriver failed to start cols=${cols} rows=${rows}`);
      this.scheduleStartRetry();
      return;
    }
    this.stopStartRetry();
    this.lastCols = cols;
    this.lastRows = rows;
    nativeBridge.setOutputCallback(this.outputListener);
  }

  private stop(): void {
    this.stopStartRetry();
    nativeBridge.setOutputCallback(null);
    if (this.started) {
      nativeBridge.stop();
      this.started = false;
    }
  }

  private scheduleStartRetry(): void {
    if (this.startRetryTimer >= 0) {
      return;
    }
    this.startRetryTimer = setTimeout(() => {
      this.startRetryTimer = -1;
      void this.start();
    }, 50) as number;
  }

  private stopStartRetry(): void {
    if (this.startRetryTimer < 0) {
      return;
    }
    clearTimeout(this.startRetryTimer);
    this.startRetryTimer = -1;
  }
}
