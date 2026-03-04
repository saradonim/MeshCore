# Shipping Mode

Shipping mode prevents devices from being active during transport — no radio transmission, no BLE advertising, no battery drain. The device enters deep sleep on boot and stays there until the end user permanently unlocks it by holding the USER button for 3 seconds.

## Quick Start

Add these two build flags to your variant's `platformio.ini`:

```ini
build_flags =
  ...
  -D SHIPPING_MODE_ENABLED=1
  -D SHIPPING_BUILD_STAMP=$UNIX_TIME
```

`SHIPPING_MODE_ENABLED` activates the feature. `SHIPPING_BUILD_STAMP` ensures every build gets a unique ID so that re-flashing always re-enables shipping mode.

## How It Works

### Boot Flow

```
setup()
  ├── board.begin()
  ├── display.begin()          (if available)
  ├── radio_init()
  ├── filesystem init          (InternalFS / LittleFS / SPIFFS)
  ├── checkShippingMode()      ← inserted here, before store.begin()
  │     ├── shippingCheckNewFirmware()
  │     │     └── Build ID changed? → delete /shipping_unlocked
  │     ├── /shipping_unlocked exists? → return (normal boot)
  │     └── Not found → shipping mode:
  │           ├── Show unlock prompt on display
  │           ├── Wait 30s for 3-second button hold
  │           │     └── Held 3s → write /shipping_unlocked → reboot
  │           ├── Show "sleep" message on display
  │           └── Enter deep sleep (GPIO wake on USER button)
  │               └── Wake = full reboot → back to top
  ├── store.begin()
  ├── the_mesh.begin()
  └── ... normal boot continues
```

### Unlock Procedure (end user)

1. Press the USER button — device wakes from deep sleep and reboots
2. Display shows unlock instructions
3. Hold the USER button for 3 continuous seconds
4. Display shows "Unlocked!" — device reboots into normal mode
5. Device works normally from now on

### Re-locking on Re-flash

Every build produces a unique build ID (via `$UNIX_TIME`). On each boot, the firmware compares the stored build ID against the running firmware. If they differ, the `/shipping_unlocked` file is deleted and shipping mode re-activates.

This means:
- **Re-flash** (new build) → shipping mode re-activates automatically
- **Normal reboot** (same firmware) → stays unlocked

## Files Modified

### `examples/companion_radio/main.cpp`

All shipping mode logic lives here, gated behind `#ifdef SHIPPING_MODE_ENABLED`. No other files are modified.

**Functions added:**

| Function | Purpose |
|----------|---------|
| `shippingCheckNewFirmware()` | Compares stored build ID to current firmware; deletes unlock file on mismatch |
| `shippingUnlockFileExists()` | Checks if `/shipping_unlocked` exists on the filesystem |
| `shippingWriteUnlockFile()` | Creates `/shipping_unlocked` to mark device as unlocked |
| `shippingModeSleep()` | Enters platform-specific deep sleep with GPIO wake on USER button |
| `checkShippingMode()` | Main entry point — orchestrates display, button detection, and sleep |

**Insertion points in `setup()`** — one per platform block, after filesystem init and before `store.begin()`:

```cpp
#ifdef SHIPPING_MODE_ENABLED
  checkShippingMode();  // may never return (deep sleep or reboot)
#endif
```

### Variant `platformio.ini`

Add `-D SHIPPING_MODE_ENABLED=1` and `-D SHIPPING_BUILD_STAMP=$UNIX_TIME` to the build flags. See `variants/lilygo_techo/platformio.ini` (`LilyGo_T-Echo_companion_radio_ble` env) for a working example.

## Filesystem

Two files are stored on the platform filesystem (`InternalFS` / `LittleFS` / `SPIFFS`):

| File | Contents | Purpose |
|------|----------|---------|
| `/shipping_unlocked` | 1 byte | Presence indicates device is unlocked |
| `/shipping_build_id` | Build timestamp string | Compared on boot to detect re-flash |

Raw filesystem files are used (not `DataStore` prefs) because `DataStore` is not yet initialized at the point where shipping mode runs.

## Platform Support

### NRF52 (tested on LilyGo T-Echo)

- **Sleep**: SYSTEMOFF via `sd_power_system_off()` — draws ~0.3 uA
- **Wake**: GPIO sense on `PIN_USER_BTN` via `nrf_gpio_cfg_sense_input()` — triggers full reboot
- **Display**: E-ink (GxEPD2) retains image without power; `display.turnOff()` cuts controller power
- **Bootloader**: `PIN_USER_BTN` (GPIO 42) is NOT the RESET button. The Adafruit bootloader only enters DFU via double-tap RESET, so holding the USER button during wake does not trigger DFU

`board.powerOff()` on TechoBoard handles LED, backlight, and `PIN_PWR_EN` cleanup before calling `sd_power_system_off()`.

### ESP32

- **Sleep**: `esp_deep_sleep_start()` with `esp_sleep_enable_ext0_wakeup()` — draws ~10 uA
- **Wake**: EXT0 wakeup on `PIN_USER_BTN` (active LOW) — triggers full reboot
- **Requirement**: `PIN_USER_BTN` must be an RTC-capable GPIO. If not, the device enters deep sleep with no way to wake via button

### RP2040 / STM32

- **Sleep**: Falls through to `board.powerOff()` — behavior depends on the board implementation
- **Wake**: No GPIO wake is configured for these platforms. Would need platform-specific wake code added to `shippingModeSleep()` to be fully functional
- **Status**: Compiles cleanly but sleep/wake needs work if shipping mode is needed on these platforms

## Design Decisions

1. **File-based unlock** (not NodePrefs) — `NodePrefs`/`DataStore` aren't initialized yet when shipping mode runs. A raw filesystem file is simpler and avoids changing the prefs serialization format.

2. **Check after FS init, before radio/mesh** — The radio hardware is initialized by `radio_init()` (which runs earlier), but no data is transmitted. The mesh networking, BLE advertising, and WiFi connections all happen after `store.begin()` and `the_mesh.begin()`, which shipping mode blocks.

3. **`digitalRead()` instead of `MomentaryButton`** — The `MomentaryButton` class requires `begin()` and periodic `check()` calls. For simple hold-detection, raw GPIO reads avoid any dependency on the UI button infrastructure.

4. **`$UNIX_TIME` build stamp** — `__DATE__` and `__TIME__` only change when the source file is recompiled. PlatformIO incremental builds skip recompilation if the source didn't change, making the timestamp identical across re-flashes of the same code. `$UNIX_TIME` in build flags forces a unique value per build and triggers recompilation.

5. **Active-LOW button assumption** — Most boards wire USER buttons with `INPUT_PULLUP` and active-low. The code uses `digitalRead(PIN_USER_BTN) == LOW` for press detection.

6. **30-second unlock window** — After displaying the unlock prompt, the device waits 30 seconds for a 3-second button hold before entering deep sleep. This gives the user 27 seconds to start pressing. The brief CPU time before sleep is negligible compared to months of deep sleep.

## Customizing Display Text

The display messages are in `checkShippingMode()` in `main.cpp`. There are three display states:

1. **Unlock prompt** — shown on boot while waiting for button hold
2. **Unlocked confirmation** — shown briefly after successful unlock before reboot
3. **Sleep message** — shown after timeout, just before entering deep sleep (persists on e-ink during sleep)

All display code is guarded by `#ifdef DISPLAY_CLASS`. Devices without a display work fine — the user holds the button blindly for 3 seconds.

## Testing Checklist

1. Build with `SHIPPING_MODE_ENABLED=1` and `SHIPPING_BUILD_STAMP=$UNIX_TIME`
2. Flash and boot — device should show unlock prompt, then sleep after 30s
3. Press USER button — device wakes, shows unlock prompt
4. Hold USER button 3+ seconds — device shows confirmation, reboots into normal mode
5. Reboot — device boots normally (unlock file persists)
6. Re-flash — device should enter shipping mode again (new build stamp)
7. Build WITHOUT `SHIPPING_MODE_ENABLED` — no shipping mode behavior, zero impact
