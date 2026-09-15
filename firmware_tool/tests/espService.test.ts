import { describe, expect, test, vi } from "vitest";
import { ESPService } from "../src/services/espService";

vi.mock("../src/services/stacktraceService", () => ({
  StacktraceService: class {
    isElfLoaded() {
      return false;
    }
  },
}));

function monitorChunks(service: ESPService, chunks: Uint8Array[]) {
  const rawRead = vi.fn(async () => chunks.shift());
  // Supply only the serial transport; the production monitor still decodes,
  // buffers, classifies and dispatches every received chunk.
  Object.assign(service, { espLoader: { transport: { rawRead } } });
  service.addSerialMonitor();
  return rawRead;
}

describe("serial JSON frames", () => {
  test("preserves fragmented Unicode and does not mistake values for crash logs", async () => {
    const service = new ESPService();
    const received: string[] = [];
    const onReboot = vi.fn();
    service.onReboot = onReboot;
    service.addLogListener((line) => {
      received.push(line);
      return true;
    });
    const json = JSON.stringify({ value: "é雪😀\u0080 rst: Backtrace:" });
    const bytes = new TextEncoder().encode(json + "\r\npubconsole>");
    const chunks = Array.from(bytes, (byte) => Uint8Array.of(byte));
    monitorChunks(service, chunks);
    await vi.waitFor(() => expect(received).toEqual([json, "pubconsole>"]));
    expect(onReboot).not.toHaveBeenCalled();
  });

  test("a new monitor discards an incomplete frame from the previous connection", async () => {
    const service = new ESPService();
    const received: string[] = [];
    service.addLogListener((line) => {
      received.push(line);
      return true;
    });
    const firstRead = monitorChunks(service, [
      new TextEncoder().encode('{"old":'),
    ]);
    await vi.waitFor(() => expect(firstRead).toHaveBeenCalledTimes(2));
    monitorChunks(service, [new TextEncoder().encode('{"new":true}\n')]);
    await vi.waitFor(() => expect(received).toEqual(['{"new":true}']));
  });

  test("a failed silent command releases its listener", async () => {
    const service = new ESPService();
    vi.spyOn(service, "sendCommand").mockRejectedValue(
      new Error("Disconnected"),
    );
    await expect(service.executeCommand("version")).rejects.toThrow(
      "Disconnected",
    );
    const listener = vi.fn(() => true);
    service.addLogListener(listener);
    monitorChunks(service, [new TextEncoder().encode("after failure\n")]);
    await vi.waitFor(() =>
      expect(listener).toHaveBeenCalledWith("after failure", "info"),
    );
  });
});

describe("console write queue", () => {
  test("concurrent commands wait for the writable stream lock and retain order", async () => {
    const service = new ESPService();
    const received: string[] = [];
    let release = () => {};
    const pending = new Promise<void>((resolve) => {
      release = resolve;
    });
    const stream = new WritableStream<Uint8Array>({
      write(data) {
        received.push(new TextDecoder().decode(data));
        if (received.length === 1) return pending;
      },
    });
    const write = vi.fn(async (data: Uint8Array) => {
      const writer = stream.getWriter();
      try {
        await writer.write(data);
      } finally {
        writer.releaseLock();
      }
    });
    Object.assign(service, { espLoader: { transport: { write } } });
    const results = Promise.allSettled([
      service.sendCommand("version"),
      service.sendCommand("settings", true),
      service.sendCommand("settings", true),
    ]);
    await vi.waitFor(() => expect(received).toEqual(["version\n"]));
    expect(write).toHaveBeenCalledTimes(1);
    expect(stream.locked).toBe(true);
    release();
    expect((await results).map((result) => result.status)).toEqual([
      "fulfilled",
      "fulfilled",
      "fulfilled",
    ]);
    expect(received).toEqual(["version\n", "settings\n", "settings\n"]);
    expect(stream.locked).toBe(false);
  });

  test("a rejected write does not poison the queue", async () => {
    const service = new ESPService();
    const write = vi
      .fn()
      .mockRejectedValueOnce(new Error("Write failed"))
      .mockResolvedValue(undefined);
    Object.assign(service, { espLoader: { transport: { write } } });
    const results = await Promise.allSettled([
      service.sendCommand("version"),
      service.sendCommand("settings"),
    ]);
    expect(results[0]).toMatchObject({
      status: "rejected",
      reason: new Error("Write failed"),
    });
    expect(results[1].status).toBe("fulfilled");
    expect(write).toHaveBeenCalledTimes(2);
  });

  test("disconnect cancels queued commands and drains the active write before closing", async () => {
    const service = new ESPService();
    let release = () => {};
    const pending = new Promise<void>((resolve) => {
      release = resolve;
    });
    const write = vi.fn(() => pending);
    const disconnect = vi.fn(async () => {});
    Object.assign(service, { espLoader: { transport: { write, disconnect } } });
    const first = service.sendCommand("version");
    const cancelled = expect(service.sendCommand("settings")).rejects.toThrow(
      "Connection changed",
    );
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(1));
    const closing = service.disconnect();
    expect(service.isConnected()).toBe(false);
    expect(disconnect).not.toHaveBeenCalled();
    const nextWrite = vi.fn(async () => {});
    Object.assign(service, { espLoader: { transport: { write: nextWrite } } });
    release();
    await Promise.all([first, cancelled, closing]);
    expect(write).toHaveBeenCalledTimes(1);
    expect(disconnect).toHaveBeenCalledTimes(1);
    await service.sendCommand("settings");
    expect(nextWrite).toHaveBeenCalledTimes(1);
  });
});
