#include "screens/menu_screen.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "generated/app-window.h"
#include "remote/comms.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/led.h"
#include "remote/powermanagement.h"
#include "remote/settings.h"
#include "remote/stats.h"
#include <atomic>

static const char *TAG = "PUBREMOTE-MENU_SCREEN";

static bool confirm_reset = false;

enum PendingAction {
  PENDING_NONE,
  PENDING_SHUTDOWN,
  PENDING_RESET
};
static PendingAction pending_action = PENDING_NONE;

extern "C" void handle_menu_shutdown_long_press() {
  confirm_reset = !confirm_reset;
  slint::invoke_from_event_loop(
      []() { get_slint_window()->global<UiState>().set_shutdown_text(confirm_reset ? "Factory reset?" : "Shutdown"); });
}

// stats_update() fires at telemetry rate (20Hz+) while connected. Posting to
// the Slint event loop on every tick invalidates and re-renders the menu (and
// any dialog overlaying it) continuously, starving touch input - the
// shutdown/reset dialog buttons stop responding. Only post on actual
// connection-state changes, with a pending guard as backstop.
static std::atomic<bool> menu_update_pending{false};
static int menu_last_connection_state = -1;

// Only DISCONNECTED offers to connect; the other states (including CONNECTING)
// offer to tear the attempt down.
static const char *menu_connect_label() {
  return connection_state == CONNECTION_STATE_DISCONNECTED ? "Connect" : "Disconnect";
}

static void menu_update_display() {
  if (!get_slint_window())
    return;

  int state = (int)connection_state;
  if (state == menu_last_connection_state) {
    return;
  }
  menu_last_connection_state = state;

  if (menu_update_pending.exchange(true)) {
    return;
  }
  slint::invoke_from_event_loop([]() {
    menu_update_pending.store(false);
    const auto &state = get_slint_window()->global<UiState>();
    state.set_connection_state((int)connection_state);
    state.set_menu_connect_label(menu_connect_label());
  });
}

extern "C" void setup_menu_properties() {
  if (!get_slint_window())
    return;

  const auto &state = get_slint_window()->global<UiState>();

  confirm_reset = false;
  pending_action = PENDING_NONE;
  menu_last_connection_state = -1; // Force a repaint on (re)entry
  menu_update_pending.store(false);
  state.set_shutdown_text("Shutdown");
  state.on_menu_shutdown_long_press([]() { handle_menu_shutdown_long_press(); });

  state.on_confirm_dialog_accepted([]() {
    slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_show_confirm_dialog(false); });
    if (pending_action == PENDING_RESET) {
      reset_all_settings();
      esp_restart();
    }
    else if (pending_action == PENDING_SHUTDOWN) {
      enter_sleep();
    }
    pending_action = PENDING_NONE;
  });

  state.on_confirm_dialog_rejected([]() {
    slint::invoke_from_event_loop([]() {
      const auto &state = get_slint_window()->global<UiState>();
      state.set_show_confirm_dialog(false);
      if (confirm_reset) {
        confirm_reset = false;
        state.set_shutdown_text("Shutdown");
      }
    });
    pending_action = PENDING_NONE;
  });

  if (pairing_state == PAIRING_STATE_PAIRED) {
    state.set_menu_show_connect(true);
  }
  else {
    state.set_menu_show_connect(false);
  }

  state.set_pocket_mode_active(is_pocket_mode_enabled());
  state.set_hbm_mode_label(hbm_mode_label(device_settings.hbm_mode));
  state.set_hbm_mode_supported(display_supports_hbm());
  state.set_led_mode_label(led_mode_label(device_settings.led_mode));
  state.set_led_mode_supported(led_is_supported());

  char bl_duty_buf[16];
  snprintf(bl_duty_buf, sizeof(bl_duty_buf), "%u%%", device_settings.duty_backlight_threshold);
  state.set_backlight_duty_label(bl_duty_buf);

  char yellow_duty_buf[16];
  snprintf(yellow_duty_buf, sizeof(yellow_duty_buf), "%u%%", device_settings.duty_yellow_threshold);
  state.set_yellow_duty_label(yellow_duty_buf);

  char red_duty_buf[16];
  snprintf(red_duty_buf, sizeof(red_duty_buf), "%u%%", device_settings.duty_red_threshold);
  state.set_red_duty_label(red_duty_buf);

  stats_register_update_cb(menu_update_display);
  menu_update_display();
}

extern "C" void handle_menu_connect() {
  ESP_LOGI(TAG, "Connect button pressed");
  if (connection_state == CONNECTION_STATE_DISCONNECTED) {
    connection_set_auto_reconnect(true);
    connection_connect_to_default_peer();
  }
  else {
    // Explicit user disconnect: stop auto-reconnect and tear the link down
    // (for BLE this terminates the connection and stops retry timers)
    connection_set_auto_reconnect(false);
    connection_update_state(CONNECTION_STATE_DISCONNECTED);
    comms_disconnect_peer(pairing_settings.remote_addr);
  }

  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Stats); });
}

extern "C" void handle_menu_pocket_mode() {
  ESP_LOGI(TAG, "Pocket mode button pressed");
  if (device_settings.pocket_mode == POCKET_MODE_DISABLED) {
    device_settings.pocket_mode = POCKET_MODE_ENABLED;
  }
  else {
    device_settings.pocket_mode = POCKET_MODE_DISABLED;
  }
  save_device_settings();

  setup_menu_properties();
}

extern "C" void handle_menu_toggle_hbm() {
  if (!display_supports_hbm()) {
    return;
  }
  ESP_LOGI(TAG, "HBM mode button pressed");
#if IMU_ENABLED
  device_settings.hbm_mode = (HbmModeOptions)((device_settings.hbm_mode + 1) % HBM_MODE_COUNT);
#else
  if (device_settings.hbm_mode == HBM_MODE_OFF) {
    device_settings.hbm_mode = HBM_MODE_ON;
  }
  else {
    device_settings.hbm_mode = HBM_MODE_OFF;
  }
#endif
  save_device_settings();

  // Apply immediately: HBM is only active if HBM mode is set to ON
  if (device_settings.hbm_mode == HBM_MODE_ON) {
    display_set_hbm(true);
  }
  else {
    display_set_hbm(false);
  }

  setup_menu_properties(); // update the text
}

extern "C" void handle_menu_toggle_led() {
  if (!led_is_supported()) {
    return;
  }
  device_settings.led_mode = (LedModeOptions)((device_settings.led_mode + 1) % LED_MODE_COUNT);
  ESP_LOGI(TAG, "LED mode button pressed - now %s", led_mode_label(device_settings.led_mode));
  save_device_settings();

  // Apply immediately so the new mode is visible while still on the menu
  led_apply_mode();

  setup_menu_properties(); // update the text
}

extern "C" void handle_menu_toggle_backlight() {
  uint8_t current = device_settings.duty_backlight_threshold;
  current += 10;
  if (current > 100 || current < 50) {
    current = 50;
  }
  device_settings.duty_backlight_threshold = current;
  ESP_LOGI(TAG, "Duty backlight threshold now %u%%", current);
  save_device_settings();

  setup_menu_properties(); // update the text
}

extern "C" void handle_menu_toggle_yellow_duty() {
  uint8_t current = device_settings.duty_yellow_threshold;
  current += 5;
  if (current > 75 || current < 40) {
    current = 40;
  }
  device_settings.duty_yellow_threshold = current;
  if (device_settings.duty_red_threshold <= current) {
    device_settings.duty_red_threshold = current + 5;
  }
  ESP_LOGI(TAG, "Yellow duty threshold now %u%%", current);
  save_device_settings();

  setup_menu_properties(); // update the text
}

extern "C" void handle_menu_toggle_red_duty() {
  uint8_t current = device_settings.duty_red_threshold;
  current += 5;
  if (current > 95 || current < 60) {
    current = 60;
  }
  device_settings.duty_red_threshold = current;
  if (device_settings.duty_yellow_threshold >= current) {
    device_settings.duty_yellow_threshold = current - 5;
  }
  ESP_LOGI(TAG, "Red duty threshold now %u%%", current);
  save_device_settings();

  setup_menu_properties(); // update the text
}

extern "C" void handle_open_settings() {
  ESP_LOGI(TAG, "Open settings pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Settings); });
}

extern "C" void handle_open_input_calibration() {
  ESP_LOGI(TAG, "Open input calibration pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::InputCalibration); });
}

extern "C" void handle_open_pairing() {
  ESP_LOGI(TAG, "Open pairing pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Boards); });
}

extern "C" void handle_open_about() {
  ESP_LOGI(TAG, "Open about pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::About); });
}

extern "C" void handle_menu_shutdown() {
  ESP_LOGI(TAG, "Shutdown button pressed");
  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    if (confirm_reset) {
      pending_action = PENDING_RESET;
      state.set_confirm_dialog_title("Factory Reset");
      state.set_confirm_dialog_message("This will wipe all settings. Proceed?");
      state.set_confirm_dialog_confirm_text("Reset");
      state.set_confirm_dialog_variant("danger");
    }
    else {
      pending_action = PENDING_SHUTDOWN;
      state.set_confirm_dialog_title("Power Off");
      state.set_confirm_dialog_message("Are you sure you want to turn off the remote?");
      state.set_confirm_dialog_confirm_text("Shutdown");
    }
    state.set_show_confirm_dialog(true);
  });
}

extern "C" void teardown_menu_properties() {
  ESP_LOGI(TAG, "Tearing down menu screen properties");
  stats_unregister_update_cb(menu_update_display);
  if (!get_slint_window())
    return;
  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.on_menu_shutdown_long_press([]() {});
    state.on_confirm_dialog_accepted([]() {});
    state.on_confirm_dialog_rejected([]() {});
  });
}
