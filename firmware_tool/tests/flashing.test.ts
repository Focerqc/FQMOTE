import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { ESPService } from '../src/services/espService';
import type { FirmwareFiles } from '../src/types';

const mocks = vi.hoisted(() => {
  const transport = {
    disconnect: vi.fn(async () => {}),
    connect: vi.fn(async () => {}),
    write: vi.fn(async () => {}),
  };
  return {
    transport,
    port: { addEventListener: vi.fn(), removeEventListener: vi.fn() },
    loader: {
      transport,
      main: vi.fn(async () => {}),
      sync: vi.fn(async () => {}),
      hardReset: vi.fn(async () => {}),
      readFlash: vi.fn(async () => new Uint8Array(16).fill(0xff)),
      eraseFlash: vi.fn(async () => {}),
      writeFlash: vi.fn<(options: unknown) => Promise<void>>(async () => {}),
      chip: {
        getChipDescription: vi.fn(async () => 'ESP32-S3'),
        readMac: vi.fn(async () => '28:84:85:85:c8:1c'),
        getChipFeatures: vi.fn(async () => 'Wi-Fi,BLE'),
        getCrystalFreq: vi.fn(async () => 40),
      },
    },
  };
});

vi.mock('esptool-js', () => ({
  Transport: class {
    constructor() {
      return mocks.transport;
    }
  },
  ESPLoader: class {
    constructor() {
      return mocks.loader;
    }
  },
}));
vi.mock('../src/utils/delay', () => ({ delay: async () => {} }));
vi.mock('../src/services/stacktraceService', () => ({
  StacktraceService: class {},
}));

beforeEach(() => {
  vi.clearAllMocks();
  mocks.loader.readFlash.mockResolvedValue(new Uint8Array(16).fill(0xff));
  vi.stubGlobal('navigator', {
    serial: { requestPort: async () => mocks.port },
  });
});
afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

function device() {
  const service = new ESPService();
  vi.spyOn(service, 'addSerialMonitor').mockImplementation(() => {});
  vi.spyOn(service, 'getVersionInfo').mockResolvedValue({
    version: '0.9.5',
    variant: 'dev',
    hardware: 'test',
  });
  vi.spyOn(service, 'checkCoredump').mockResolvedValue(false);
  vi.spyOn(
    service as unknown as { readFileAsString(file: File): Promise<string> },
    'readFileAsString',
  ).mockResolvedValue('firmware bytes');
  return service;
}

const firmware: FirmwareFiles = {
  bootloader: { name: 'bootloader.bin' } as File,
  partitionTable: { name: 'partitions.bin' } as File,
  application: { name: 'firmware.bin' } as File,
  elf: null,
};

test('a fresh-device install reuses the synchronized bootloader without closing or resetting it', async () => {
  const service = device();
  expect((await service.connect()).hasFirmware).toBe(false);
  await service.flash(firmware, true, vi.fn());

  expect(mocks.loader.main).toHaveBeenCalledTimes(1);
  expect(mocks.loader.sync).toHaveBeenCalledTimes(1);
  expect(mocks.transport.disconnect).not.toHaveBeenCalled();
  expect(mocks.loader.eraseFlash).toHaveBeenCalledTimes(1);
  expect(mocks.loader.writeFlash).toHaveBeenCalledWith(
    expect.objectContaining({
      fileArray: [
        { data: 'firmware bytes', address: 0 },
        { data: 'firmware bytes', address: 0x8000 },
        { data: 'firmware bytes', address: 0x10000 },
      ],
    }),
  );
  expect(mocks.loader.hardReset).toHaveBeenCalledTimes(1);
  expect(mocks.loader.hardReset.mock.invocationCallOrder[0]).toBeGreaterThan(
    mocks.loader.writeFlash.mock.invocationCallOrder[0],
  );
});

test('a running firmware upgrade still re-enters the bootloader', async () => {
  mocks.loader.readFlash.mockResolvedValue(Uint8Array.from([0xe9, ...new Array(15).fill(0)]));
  const service = device();
  expect((await service.connect()).hasFirmware).toBe(true);
  await service.flash(firmware, false, vi.fn());

  expect(mocks.loader.main).toHaveBeenCalledTimes(2);
  expect(mocks.loader.sync).toHaveBeenCalledTimes(2);
  expect(mocks.transport.disconnect).toHaveBeenCalledTimes(2);
  expect(mocks.loader.eraseFlash).not.toHaveBeenCalled();
  expect(mocks.loader.writeFlash).toHaveBeenCalledTimes(1);
});

test("console commands cannot corrupt a fresh device's bootloader session", async () => {
  const service = device();
  await service.connect();
  await expect(service.sendCommand('settings')).rejects.toThrow('bootloader mode');
  expect(mocks.transport.write).not.toHaveBeenCalled();
});

test('disconnect clears the ready bootloader state before a later normal connection', async () => {
  const service = device();
  await service.connect();
  await service.disconnect();
  mocks.loader.readFlash.mockResolvedValue(Uint8Array.from([0xe9, ...new Array(15).fill(0)]));
  await service.connect();
  await service.sendCommand('settings');
  expect(mocks.transport.write).toHaveBeenCalledTimes(1);
  await service.flash(firmware, false, vi.fn());
  expect(mocks.loader.main).toHaveBeenCalledTimes(3);
});
