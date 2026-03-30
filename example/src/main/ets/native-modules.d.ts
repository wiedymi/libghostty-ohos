declare module 'libexample_driver.so' {
  const exampleDriver: {
    initialize(filesDir: string): void;
    start(cols: number, rows: number): boolean;
    stop(): void;
    writeInput(data: string): void;
    drainOutput(): string;
    setOutputCallback(callback: ((data: string) => void) | null): void;
    resize(cols: number, rows: number): void;
  };

  export default exampleDriver;
}
