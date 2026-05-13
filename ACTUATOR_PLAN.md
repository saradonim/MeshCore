# Plan: Persistent Binary Actuator Config + Control via Repeater Admin CLI

## Context

The repeater firmware has no way to remotely configure or control GPIO output pins. The existing `MainBoard::setGpio()` is a hardcoded bitmask tied to specific board variants, with no persistence and no remote CLI access. We want a higher-level abstraction that allows an admin to:

1. **Configure** which pins are binary actuators (persisted to flash, survives reboots)
2. **Control** those actuators remotely via admin CLI
3. **Query** actuator state via telemetry

This is phase 1: **Binary Actuators** only (1 pin, HIGH/LOW). Data structures are designed to accommodate future types (Analog/PWM, Positional, Directional).

---

## New File: `src/helpers/ActuatorManager.h`

Header-only (following the pattern of other helpers like `SensorManager.h`).

### Constants and Data Structures

```cpp
#define ACTUATOR_TYPE_NONE          0x00
#define ACTUATOR_TYPE_BINARY        0x01  // 1 pin, HIGH/LOW
// Future:
// #define ACTUATOR_TYPE_ANALOG      0x02  // 1 PWM pin, 0-255
// #define ACTUATOR_TYPE_POSITIONAL  0x03  // 2 pins (e.g. H-bridge)
// #define ACTUATOR_TYPE_DIRECTIONAL 0x04  // 2-3 pins (e.g. stepper)

#define MAX_ACTUATORS       8
#define ACTUATOR_NAME_LEN   10
#define ACTUATOR_CFG_FILE   "/actuator_cfg"

struct ActuatorEntry {
  uint8_t type;                  // ACTUATOR_TYPE_*
  uint8_t pin1;                  // primary GPIO pin
  uint8_t pin2;                  // secondary (future, 0 = unused)
  uint8_t pin3;                  // tertiary (future, 0 = unused)
  uint8_t state;                 // last-commanded state (0/1 for binary)
  uint8_t flags;                 // bit 0: active-low invert; bits 1-7: reserved
  char name[ACTUATOR_NAME_LEN];  // human-readable label
};
// 16 bytes per entry
```

### ActuatorManager Class

```cpp
class ActuatorManager {
  ActuatorEntry _entries[MAX_ACTUATORS];
  uint8_t _count;
  FILESYSTEM* _fs;

public:
  ActuatorManager() : _count(0), _fs(nullptr) {}

  void begin(FILESYSTEM* fs);       // Load from /actuator_cfg, apply pinMode + restore state
  void save();                       // Persist to /actuator_cfg

  uint8_t count() const;
  const ActuatorEntry* get(int index) const;

  // Config
  bool add(uint8_t type, uint8_t pin1, const char* name, uint8_t flags = 0);
  bool remove(int index);

  // Control
  bool setState(int index, uint8_t state);  // Set + digitalWrite
  uint8_t getState(int index) const;

  // Telemetry: append actuator states as LPP_DIGITAL_OUTPUT entries
  void appendTelemetry(CayenneLPP& telemetry, uint8_t start_channel);
};
```

**Boot behavior in `begin()`:**
1. Read `/actuator_cfg` — if missing, `_count = 0` (clean start)
2. Sanitize: validate type is known, state is 0 or 1
3. For each entry: `pinMode(pin1, OUTPUT)`, then `digitalWrite(pin1, state)` — restores last-commanded state

**`save()` pattern:** Same platform-specific file I/O as `/com_prefs` in `CommonCLI.cpp:125-132`:
- NRF52/STM32: `fs->remove()` then `fs->open(path, FILE_O_WRITE)`
- RP2040: `fs->open(path, "w")`
- ESP32: `fs->open(path, "w", true)`

**File format (132 bytes max):**
```
[count: 1 byte] [reserved: 3 bytes] [entries: count * 16 bytes]
```

---

## Modified File: `examples/simple_repeater/MyMesh.h`

- Add `#include <helpers/ActuatorManager.h>`
- Declare `extern ActuatorManager actuators;` (same pattern as `extern EnvironmentSensorManager sensors;` in variant target files)

---

## Modified File: `examples/simple_repeater/MyMesh.cpp`

### 1. CLI commands in `handleCommand()` (~line 1278, before the fallback to `_cli.handleCommand()`)

Add a new `else if` block:

```cpp
} else if (memcmp(command, "actuator ", 9) == 0) {
    const char* sub = command + 9;

    if (memcmp(sub, "list", 4) == 0) {
        // Format: "N actuators\n0:binary pin5 relay1=0\n..."
        // Paginated like sensor list if needed

    } else if (memcmp(sub, "add ", 4) == 0) {
        // Parse: "add binary <pin> [name]"
        // Call actuators.add(ACTUATOR_TYPE_BINARY, pin, name)
        // actuators.save() on success
        // Reply: "OK - added #N" or "Err - max reached" / "Err - bad params"

    } else if (memcmp(sub, "remove ", 7) == 0) {
        // Parse index
        // Call actuators.remove(index)
        // actuators.save() on success
        // Reply: "OK - removed" or "Err - bad index"

    } else if (memcmp(sub, "set ", 4) == 0) {
        // Parse: "set <index> <0|1>"
        // Call actuators.setState(index, value)
        // actuators.save() on success (persist state for reboot restore)
        // Reply: "OK - #N=1" or "Err - bad index"

    } else if (memcmp(sub, "get", 3) == 0) {
        // Optional index: "get" (all) or "get <index>" (one)
        // Format state into reply

    } else {
        strcpy(reply, "Err - unknown actuator cmd");
    }
```

### 2. Telemetry in `handleRequest()` (~line 253, after `sensors.querySensors()`)

```cpp
// After sensors.querySensors(perm_mask, telemetry); at line 253:
actuators.appendTelemetry(telemetry, 20);  // channel 20+ to avoid sensor channel conflicts
```

`appendTelemetry()` calls `telemetry.addDigitalOutput(channel, state)` for each configured binary actuator. Uses `LPP_DIGITAL_OUTPUT` (type 1), which is already defined in CayenneLPP and distinct from sensor types.

### 3. Initialization in `begin()` (~line 904)

```cpp
// After _cli.loadPrefs(_fs); at line 908:
actuators.begin(_fs);
```

---

## Modified File: `examples/simple_repeater/main.cpp`

- Add `#include <helpers/ActuatorManager.h>`
- Declare global: `ActuatorManager actuators;`
- No changes needed in `setup()` or `loop()` — `actuators.begin()` is called from `MyMesh::begin()`, and actuators don't need a `loop()` call (binary outputs are set-and-forget)

---

## Modified File: `src/helpers/sensors/LPPDataHelpers.h`

No changes needed — `LPP_DIGITAL_OUTPUT` (1) is already defined at line 6. The CayenneLPP library already has `addDigitalOutput(channel, value)`.

---

## NOT Modified

- `src/helpers/CommonCLI.h` — CLI stays in repeater only per user preference
- `src/helpers/CommonCLI.cpp` — no changes
- Board variant files — actuator pins are configured at runtime via CLI, not at compile time
- Companion protocol — admin CLI over mesh is the interface for now

---

## CLI Command Reference

| Command | Example | Reply |
|---------|---------|-------|
| `actuator list` | `actuator list` | `2 actuators\n0:binary pin5 relay1=0\n1:binary pin12 pump=1` |
| `actuator add binary <pin> [name]` | `actuator add binary 5 relay1` | `OK - added #0` |
| `actuator remove <index>` | `actuator remove 0` | `OK - removed` |
| `actuator set <index> <0\|1>` | `actuator set 0 1` | `OK - #0=1` |
| `actuator get [index]` | `actuator get 0` | `#0:binary pin5 relay1=1` |
| `actuator get` (no index) | `actuator get` | All states, same format as list |

Error replies: `Err - max actuators reached`, `Err - bad index`, `Err - bad params`, `Err - unknown type`

---

## Telemetry Reporting

When a remote node requests telemetry (`REQ_TYPE_GET_TELEMETRY_DATA`), actuator states are appended as `LPP_DIGITAL_OUTPUT` entries starting at channel 20:

- Actuator #0 → channel 20, `LPP_DIGITAL_OUTPUT`, value 0 or 1
- Actuator #1 → channel 21, `LPP_DIGITAL_OUTPUT`, value 0 or 1
- etc.

This is 3 bytes per actuator (channel + type + value). With 8 max actuators = 24 bytes, well within packet limits.

---

## Verification

1. **Build**: Compile `simple_repeater` for any board variant — should compile cleanly. Boards without actuators configured simply have 0 actuators.

2. **Serial CLI test**:
   ```
   actuator list           → "0 actuators"
   actuator add binary 5 relay1  → "OK - added #0"
   actuator add binary 12 pump   → "OK - added #1"
   actuator list           → "2 actuators\n0:binary pin5 relay1=0\n1:binary pin12 pump=0"
   actuator set 0 1        → "OK - #0=1"    (pin 5 goes HIGH)
   actuator get 0           → "#0:binary pin5 relay1=1"
   actuator set 0 0        → "OK - #0=0"    (pin 5 goes LOW)
   actuator remove 1       → "OK - removed"
   ```

3. **Reboot persistence**: After `actuator set 0 1` + reboot, `actuator list` should show state=1, and pin should be HIGH.

4. **Remote CLI test**: From companion app, send admin commands to repeater over mesh — same commands should work.

5. **Telemetry test**: Request telemetry from the repeater — response should include `LPP_DIGITAL_OUTPUT` entries for configured actuators.

6. **Edge cases**: Add >8 actuators (error), remove from empty list (error), set invalid index (error), add with no name (should use default like "out0").
