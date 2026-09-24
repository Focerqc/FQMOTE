# Settings console protocol (version 1)

`settings` returns one compact JSON line containing `kind: "settings"`,
`version: 1`, `fields`, `values`, and `warning`. Each field describes its key,
label, group, description and type. Bounded integers use `type: "range"`, `min`, `max`, and a `color` hint.
Strings supply `maxBytes` (UTF-8) and
`secret`; integers supply numeric `options` with labels. GPIO options come
from the running board's capability masks.

`firmware/src/remote/settings_api.c` owns the descriptors used for both
metadata and save validation. Add console settings there. The web tool builds
its controls and Zod value schema from these descriptors; it has no list of
setting keys or board pin tables.

`settings_describe_json()` and `settings_apply_json()` are the shared settings
API. The console adapter only handles command arguments and response printing.
The API owns string validation and persistence; both JSON saves and the typed
Wi-Fi setters call `settings_save_string()`. Descriptors carry the NVS key and
legacy length key, so another string setting needs no new save branch. The API
uses the NVS primitives in `settings.c` and the existing live input application
function. Pin persistence stores the complete mapping and calibration in one versioned
NVS blob and propagates storage errors to the caller. Existing per-key settings
are read until the first successful pin or calibration save; subsequent boots
prefer the new record. Downgrading to old firmware uses the last legacy values. Wi-Fi byte limits are
shared constants in `settings_types.h`, used by storage and metadata alike.

Device preferences include brightness (10?255), rotation, theme colour, battery
and secondary-stat display, high brightness mode, auto-off, pocket mode, units,
startup sound, double-press action, and LED behaviour. Their descriptors share
persistence with `save_device_settings()`. Option labels reuse the on-device
settings/menu labels; unsupported HBM and LED modes cannot be selected.

Device preferences apply immediately after successful persistence. Display and
menu updates run on the Slint UI thread; power, units and input behavior use the
updated runtime settings. Auto-off changes stop/rearm the timer immediately,
including cancelling already queued expiry callbacks when disabled. Startup sound selects what plays on the next startup.
If a later write fails, earlier saved changes remain live and reload reports them.

Save a JSON object containing changed values as **one console argument**:

```text
save_settings "{\"wifi_ssid\":\"My network\",\"js_x_gpio\":-1}"
```

Use `settingsSaveCommand` in the tool: JSON serialization is followed by escaping
backslashes and double quotes for ESP-IDF's argument parser. The command is
limited to 2047 UTF-8 bytes. Values retain whitespace, Unicode and JSON escapes.
NUL characters, nested values, unknown/duplicate keys and invalid choices are
rejected. All fields and the combined pin assignment are validated before writes.

The response is one JSON line with `kind: "settings_result"`, `version: 1`, and
`ok`. Failure responses include `error`. Both `settings` and `save_settings`
accept an optional final request id (1-32 letters, digits, `_` or `-`), which
every reply echoes as `id`. The tool always sends one and ignores replies with
any other id, so a late reply to an abandoned request is never taken as current. The tool waits for acknowledgement and
reloads the applied values, including after a save failure. Storage writes and
live pin application are not a transaction across all settings, so a storage
failure may leave some values applied. A remap's new pins and reset calibration
are always stored together, so an interrupted save cannot combine the two
versions on reboot. Unchanged pins are not reapplied.

The tool queues complete console exchanges, including autocomplete and device
information requests. Cancellation stops waiting in the UI but drains the old
response through its console prompt before sending the next command. If no
prompt arrives within seven seconds, the tool logs an error and resyncs before
the next command: it sends a bare newline and waits until prompts stop arriving,
so output from the stuck command cannot be read as the new command's reply. A
prompt printed after a reboot also counts. If none arrives within three seconds,
the queued request fails as busy and the next one tries again. The connection
is kept; only a failed serial write discards it.

This replaces the old key/value console protocol. The updated tool requires
firmware implementing version 1; older firmware produces an update/connection
error rather than a form containing guessed defaults. JSON uses the firmware's
existing cJSON dependency and avoids a protobuf schema/code-generation layer.

## Config backups

**Save config backup** downloads the device's saved settings as JSON, including
Wi-Fi credentials. **Restore config backup** reads fresh settings from the device,
merges compatible backup values by key, and loads a draft for review and saving.
Settings added since the backup retain their current values. Unknown keys, changed
types, and values outside current limits/options are skipped and listed in the UI.
The backup envelope has its own format version; firmware version is informational.
Firmware validates the complete patch, including pin conflicts, when Save is pressed.

## Checks

- Full web-app type-check: `pnpm --filter @pubmote/firmware-tool typecheck`.
- Web-tool tests (Vitest): `pnpm --filter @pubmote/firmware-tool test`.
  Use `pnpm --filter @pubmote/firmware-tool test:watch` during development.
  Type-check the TypeScript tests with `pnpm --filter @pubmote/firmware-tool test:typecheck`.
- Firmware: `pio run -e pingumote_esp32s3_touch_amoled_132`.
- Host C tests use the actual cJSON and ESP-IDF console parser with device I/O
  stubbed out. Set `IDF_PATH` to an ESP-IDF checkout with its cJSON submodule,
  then build with a native C compiler (on Windows, use a Developer Command Prompt):

  ```sh
  cmake -S tests -B .pio/host-tests -DIDF_PATH="$IDF_PATH"
  cmake --build .pio/host-tests --config Debug
  ctest --test-dir .pio/host-tests -C Debug --output-on-failure
  SETTINGS_CONSOLE_TEST_BIN="$PWD/.pio/host-tests/settings_console_test" pnpm --filter @pubmote/firmware-tool test
  ```

  On Windows, set `$env:SETTINGS_CONSOLE_TEST_BIN` to the resulting `.exe` in
  PowerShell before running the Vitest command. Multi-configuration generators
  put that executable in the `Debug` subdirectory.

The host tests inject failures at every JSON allocation to check cleanup and
ensure metadata is either complete or rejected. They also cover invalid patches,
shared storage validation, and persistence errors. Vitest covers metadata-driven
validation, JSON escaping, acknowledgements, cancellation, timeouts, and cleanup.
Serial-monitor tests also cover fragmented Unicode, reconnects, failed commands,
response ownership, cancellation draining, and missing prompts. Native tests
exercise the sleep timer against simulated time and input-record persistence
with failures before and after a write becomes durable.

Setting `SETTINGS_CONSOLE_TEST_BIN` enables the cross-language test: metadata
comes from the actual C handler and a tool-generated save command passes through
the real ESP-IDF argument parser, then the returned values are checked in Zod.

The `Tests` PR workflow runs the C harness, Vitest (including the
cross-language test), app and test type-checking, full web-tool lint, and the web build.
