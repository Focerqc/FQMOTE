import { describe, expect, it } from 'vitest';
import { createSettingsBackup, mergeSettingsBackup } from '../src/services/settingsBackup';
import { type SettingsMetadata } from '../src/services/settingsProtocol';

const metadata: SettingsMetadata = {
  kind: 'settings',
  version: 1,
  warning: '',
  fields: [
    {
      key: 'wifi_password',
      label: 'Password',
      group: 'Wi-Fi',
      description: '',
      type: 'string',
      maxBytes: 64,
      secret: true,
    },
    {
      key: 'brightness',
      label: 'Brightness',
      group: 'Display',
      description: '',
      type: 'range',
      min: 10,
      max: 255,
      color: false,
    },
    {
      key: 'mode',
      label: 'Mode',
      group: 'Display',
      description: '',
      type: 'integer',
      options: [
        { value: 0, label: 'Off' },
        { value: 2, label: 'On' },
      ],
    },
  ],
  values: { wifi_password: '  "secret" \\ 雪  ', brightness: 200, mode: 0 },
};

const backup = (values: Record<string, unknown>) => ({
  format: 'pubmote-settings',
  version: 1,
  values,
});

describe('settings backups', () => {
  it('round trips all saved values, including credentials, without copying field definitions', () => {
    const exported = JSON.parse(createSettingsBackup(metadata, '0.9.5'));
    expect(exported.firmwareVersion).toBe('0.9.5');
    expect(exported.fields).toBeUndefined();
    expect(mergeSettingsBackup(metadata, exported)).toEqual({
      values: metadata.values,
      skipped: [],
      restored: 3,
    });
  });

  it('merges older backups while preserving settings introduced in current firmware', () => {
    const merged = mergeSettingsBackup(metadata, {
      ...backup({ brightness: 100 }),
      firmwareVersion: '0.1.0',
    });
    expect(merged.values).toEqual({ ...metadata.values, brightness: 100 });
    expect(metadata.values.brightness).toBe(200);
    expect(merged.restored).toBe(1);
  });

  it('skips removed fields and values incompatible with current ranges and choices', () => {
    const merged = mergeSettingsBackup(
      metadata,
      backup({
        removed: 1,
        brightness: 256,
        mode: 1,
        wifi_password: 'restored',
      }),
    );
    expect(merged.values).toEqual({
      ...metadata.values,
      wifi_password: 'restored',
    });
    expect(merged.skipped).toEqual(['removed', 'brightness', 'mode']);
    expect(merged.restored).toBe(1);
  });

  it('preserves falsey values such as an empty password and zero choice', () => {
    const current = { ...metadata, values: { ...metadata.values, mode: 2 } };
    expect(mergeSettingsBackup(current, backup({ wifi_password: '', mode: 0 })).values).toEqual({
      ...metadata.values,
      wifi_password: '',
    });
  });

  it('skips wrong types and strings exceeding current UTF-8 limits', () => {
    const merged = mergeSettingsBackup(
      metadata,
      backup({ brightness: '20', mode: {}, wifi_password: '雪'.repeat(22) }),
    );
    expect(merged.values).toEqual(metadata.values);
    expect(merged.restored).toBe(0);
    expect(merged.skipped).toHaveLength(3);
  });

  it.each([null, [], {}, { ...backup({}), format: 'other' }, { ...backup({}), version: 99 }])(
    'rejects malformed or unsupported backup envelopes: %j',
    (input) => {
      expect(() => mergeSettingsBackup(metadata, input)).toThrow();
    },
  );

  it('does not restore prototype properties', () => {
    const input = JSON.parse(
      '{"format":"pubmote-settings","version":1,"values":{"__proto__":{},"constructor":2,"brightness":100}}',
    );
    const result = mergeSettingsBackup(metadata, input);
    expect(result.values).toEqual({ ...metadata.values, brightness: 100 });
    expect(Object.getPrototypeOf(result.values)).toBe(Object.prototype);
  });
});
