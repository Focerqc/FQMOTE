import { z } from 'zod';

const fieldBase = z.object({
  key: z
    .string()
    .regex(/^[a-z][a-z0-9_]*$/)
    .refine((key) => !['constructor', 'prototype', '__proto__'].includes(key)),
  label: z.string(),
  group: z.string(),
  description: z.string(),
});
const fieldSchema = z.discriminatedUnion('type', [
  fieldBase.extend({
    type: z.literal('string'),
    maxBytes: z.number().int().positive(),
    secret: z.boolean(),
  }),
  fieldBase
    .extend({
      type: z.literal('range'),
      min: z.number().int().safe(),
      max: z.number().int().safe(),
      color: z.boolean(),
    })
    .refine(
      (field) =>
        field.min <= field.max && (!field.color || (field.min === 0 && field.max === 0xffffff)),
      'Invalid numeric range',
    ),
  fieldBase.extend({
    type: z.literal('integer'),
    options: z.array(z.object({ value: z.number().int(), label: z.string() })).nonempty(),
  }),
]);

export const settingsMetadataSchema = z.object({
  kind: z.literal('settings'),
  version: z.literal(1),
  fields: z
    .array(fieldSchema)
    .nonempty()
    .refine(
      (fields) => new Set(fields.map((field) => field.key)).size === fields.length,
      'Duplicate settings metadata',
    ),
  values: z.record(z.string(), z.union([z.string(), z.number()])),
  warning: z.string(),
});
export const settingsResultSchema = z.object({
  kind: z.literal('settings_result'),
  version: z.literal(1),
  ok: z.boolean(),
  error: z.string().optional(),
});
export type SettingsMetadata = z.infer<typeof settingsMetadataSchema>;
export type SettingsValues = Record<string, string | number>;

// No field names or device-specific validation rules live in the tool.
export function settingsValuesSchema(metadata: SettingsMetadata) {
  const shape: Record<string, z.ZodType<string | number>> = Object.create(null);
  for (const field of metadata.fields) {
    switch (field.type) {
      case 'string':
        shape[field.key] = z
          .string()
          .refine(
            (value) =>
              !value.includes('\0') && new TextEncoder().encode(value).length <= field.maxBytes,
            `${field.label} must be at most ${field.maxBytes} UTF-8 bytes and contain no NUL`,
          );
        break;
      case 'range':
        shape[field.key] = z.number().int().min(field.min).max(field.max);
        break;
      case 'integer':
        shape[field.key] = z
          .number()
          .int()
          .refine(
            (value) => field.options.some((option) => option.value === value),
            `Choose an allowed value for ${field.label}`,
          );
        break;
    }
  }
  return z.strictObject(shape);
}

export function parseSettings(payload: unknown): SettingsMetadata {
  const metadata = settingsMetadataSchema.parse(payload);
  settingsValuesSchema(metadata).parse(metadata.values);
  return metadata;
}

// esp_console_split_argv consumes one layer of backslash/quote escaping.
export function settingsSaveCommand(patch: SettingsValues): string {
  const json = JSON.stringify(patch);
  const command = `save_settings "${json.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
  if (new TextEncoder().encode(command).length >= 2048) {
    throw new Error('Settings payload exceeds the console command limit');
  }
  return command;
}

type LogListener = (line: string, type: 'info' | 'error' | 'success') => boolean;
// Structural subset keeps the protocol independent of the serial implementation.
export interface SettingsTransport {
  withConsoleTransaction?<T>(
    operation: (transport: SettingsTransport) => Promise<T>,
    signal?: AbortSignal,
  ): Promise<T>;
  addLogListener(listener: LogListener): void;
  removeLogListener(listener: LogListener): void;
  sendCommand(command: string, silent?: boolean): Promise<void>;
}

// The session prefix keeps replies to a previous page load from matching.
const requestPrefix = Math.random().toString(36).slice(2, 8);
let requestCount = 0;

export function requestSettingsJson(
  transport: SettingsTransport,
  command: string,
  kind: 'settings' | 'settings_result',
  signal?: AbortSignal,
): Promise<unknown> {
  if (transport.withConsoleTransaction) {
    return transport.withConsoleTransaction(
      (exclusive) => requestSettingsJson(exclusive, command, kind, signal),
      signal,
    );
  }
  const id = `${requestPrefix}${++requestCount}`;
  return new Promise((resolve, reject) => {
    let settled = false;
    const finish = (error?: Error, payload?: unknown) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      transport.removeLogListener(listener);
      signal?.removeEventListener('abort', abort);
      if (error) reject(error);
      else resolve(payload);
    };
    const abort = () => finish(new Error('Settings request cancelled'));
    const listener: LogListener = (line) => {
      if (!line.startsWith('{')) return false;
      let payload: unknown;
      try {
        payload = JSON.parse(line);
      } catch {
        return false;
      }
      if (!payload || typeof payload !== 'object' || !('kind' in payload)) return false;
      if (!('id' in payload) || payload.id !== id) return false;
      const reply: { kind?: unknown; id?: unknown } = { ...payload };
      delete reply.id;
      if (reply.kind === 'settings_result') {
        const result = settingsResultSchema.safeParse(reply);
        if (!result.success) {
          finish(new Error('Invalid settings acknowledgement from device'));
        } else if (!result.data.ok) {
          finish(new Error(result.data.error || 'Device rejected settings'));
        } else if (kind === 'settings_result') {
          finish(undefined, result.data);
        } else {
          return false;
        }
      } else if (reply.kind === kind) {
        finish(undefined, reply);
      } else return false;
      return true;
    };
    const timer = setTimeout(
      () =>
        finish(
          new Error(
            'No JSON settings response. Check the connection and update firmware if needed.',
          ),
        ),
      5000,
    );
    transport.addLogListener(listener);
    signal?.addEventListener('abort', abort, { once: true });
    if (signal?.aborted) {
      abort();
      return;
    }
    // Avoid logging commands containing Wi-Fi credentials.
    try {
      transport
        .sendCommand(`${command} ${id}`, true)
        .catch((error: unknown) =>
          finish(error instanceof Error ? error : new Error('Unable to send settings command')),
        );
    } catch (error) {
      finish(error instanceof Error ? error : new Error('Unable to send settings command'));
    }
  });
}
