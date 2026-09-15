import { z } from "zod";
import {
  settingsValuesSchema,
  type SettingsMetadata,
} from "./settingsProtocol";

// The backup format is independent of the firmware version and field list.
const backupSchema = z.object({
  format: z.literal("pubmote-settings"),
  version: z.literal(1),
  values: z.record(z.string(), z.unknown()),
});

export function createSettingsBackup(
  metadata: SettingsMetadata,
  firmwareVersion?: string,
) {
  return (
    JSON.stringify(
      {
        format: "pubmote-settings",
        version: 1,
        firmwareVersion,
        values: settingsValuesSchema(metadata).parse(metadata.values),
      },
      null,
      2,
    ) + "\n"
  );
}

export function mergeSettingsBackup(
  metadata: SettingsMetadata,
  input: unknown,
) {
  const backup = backupSchema.parse(input);
  const schema = settingsValuesSchema(metadata);
  const values = { ...metadata.values };
  const skipped: string[] = [];
  let restored = 0;
  for (const [key, value] of Object.entries(backup.values)) {
    if (!Object.prototype.hasOwnProperty.call(schema.shape, key)) {
      skipped.push(key);
      continue;
    }
    const result = schema.shape[key].safeParse(value);
    if (!result.success) {
      skipped.push(key);
      continue;
    }
    values[key] = result.data;
    ++restored;
  }
  return { values, skipped, restored };
}
