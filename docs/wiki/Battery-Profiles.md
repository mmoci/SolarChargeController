# Battery Profiles

The firmware supports multiple battery chemistries through a **profile system** backed by ESP-IDF NVS (non-volatile storage). The active profile survives power cycles and can be changed at runtime via MQTT.

---

## BatteryProfile Struct

Defined in `lib/Battery/BatteryProfile.h`:

```cpp
struct BatteryProfile
{
    BatteryChemistry chemistry;          // LiIon | LiFePO4

    int maxVoltage_mV;           // CC → CV transition; CV voltage clamp target
    int rechargeVoltage_mV;      // Done → CC hysteresis threshold
    int prechargeVoltage_mV;     // Idle/CC → Precharge threshold
    int minSafeVoltage_mV;       // Any mode → Fault (terminal)
    int loadDisconnectVoltage_mV; // Future: cut load to prevent deep-discharge

    int maxChargingCurrent_mA;   // CC mode current limit
    int cutoffCurrent_mA;        // CV → Done: charging current fell below this
    int prechargeCurrent_mA;     // Precharge mode current limit (typically 500 mA)
    int idleCurrent_mA;          // Maximum current in Idle mode
};
```

All voltages are in **millivolts (mV)**, all currents in **milliamps (mA)**.

---

## Built-in Profile Types

| Type enum | Description | Max voltage |
|---|---|---|
| `LIION_3S` | Li-Ion, 3 cells in series | 12 600 mV (4.20 V × 3) |
| `LIION_4S` | Li-Ion, 4 cells in series | 16 800 mV (4.20 V × 4) |
| `LIFEPO4_4S` | LiFePO₄, 4 cells in series | 14 000 mV (3.50 V × 4) |
| `CUSTOM` | User-defined; starts from 4S Li-Ion defaults | configurable |

Full default values for each type are documented in [Configuration](Configuration.md#libconfigh--built-in-battery-profiles).

---

## NVS Persistence

The `BatteryProfileSelector` class manages all NVS operations.

### NVS Namespace

```
"battery"
```

### NVS Keys (stored as individual integers, not as a blob)

| Key | Type | Description |
|---|---|---|
| `BATTERY_TYPE` | `int32` | Enum value of the selected `BatteryType` |
| `MAX_VOLTAGE` | `int32` | Override for `maxVoltage_mV` |
| `RECHARGE_VOLTAGE` | `int32` | Override for `rechargeVoltage_mV` |
| `PRECHARGE_VOLTAGE` | `int32` | Override for `prechargeVoltage_mV` |
| `LOAD_DISCONNECT_VOLTAGE` | `int32` | Override for `loadDisconnectVoltage_mV` |
| `MAX_CHARGING_CURRENT` | `int32` | Override for `maxChargingCurrent_mA` |

> The old blob format (`BATTERY_PROFILE`) is automatically detected and erased on init to migrate to the new per-field storage.

### Startup Sequence

```
BatteryProfileSelector::init()
    │
    ├── nvs_flash_init()  (erase + reinit on NVS_NO_FREE_PAGES)
    ├── nvs_open("battery", NVS_READWRITE)
    │
    └── loadProfileFromNVS()
            ├── reads BATTERY_TYPE key → determines base profile defaults
            ├── reads each override key (if present, applies on top of defaults)
            └── calls validateProfile() — rejects and falls back on failure
                    │
                    └── on failure: setProfileType(LIION_3S)  ← compiled-in default
```

---

## Type + Overrides Pattern

Selecting a type via `setProfileType(LIION_4S)` loads the full compiled-in defaults for that type. After that, individual fields can be overridden independently:

```
BatteryProfileSelector::setMaxVoltage(17000)
    │
    ├── copies current profile
    ├── applies new maxVoltage = 17 000 mV
    ├── calls validateProfile()
    │       ├── checks maxVoltage is within ±10% of the type default
    │       ├── checks maxChargingCurrent ≤ 10 000 mA
    │       └── checks logical ordering: minSafe < precharge < recharge < max
    ├── saveProfileToNVS()  ← only stores the override key, not the whole blob
    └── fires profileObserver callback → BatteryManager::updateBatteryProfile()
```

The observer ensures that the `BatteryManager` hot-reloads the new profile **without a device restart** when individual fields are changed.

> **Battery type changes** always trigger a device restart (via `esp_restart()`) because multiple thresholds change simultaneously and a running state machine could be in an inconsistent state.

---

## Validation Rules

| Rule | Detail |
|---|---|
| Voltage overrides | Must be within ±10 % of the corresponding default for the currently selected type |
| `maxChargingCurrent_mA` | Must be ≤ 10 000 mA (hard safety cap) |
| Voltage ordering | `minSafeVoltage < prechargeVoltage < rechargeVoltage < maxVoltage` |

Invalid values are rejected with a typed `Result` code and the profile is left unchanged.

---

## Changing the Profile via MQTT

See [MQTT and Home Assistant](MQTT-and-HomeAssistant.md) for the full topic reference.

**Quick reference — command topics:**

```
solar/{deviceId}/profile/battery_type/set          → "LIION_3S" | "LIION_4S" | "LIFEPO4_4S" | "CUSTOM"
solar/{deviceId}/profile/max_voltage/set           → integer in mV, e.g. "12600"
solar/{deviceId}/profile/recharge_voltage/set      → integer in mV
solar/{deviceId}/profile/precharge_voltage/set     → integer in mV
solar/{deviceId}/profile/load_disconnect_voltage/set → integer in mV
solar/{deviceId}/profile/max_charging_current/set  → integer in mA
```

---

## Changing the Profile Programmatically

```cpp
// Select a different type
profileSelector.setProfileType(BatteryConfig::BatteryType::LIFEPO4_4S);

// Override a single field (hot-reload, no restart)
BatteryProfileSelector::Result r = profileSelector.setMaxVoltage(14200);
if (r != BatteryProfileSelector::Result::SUCCESS)
{
    // Handle validation or NVS error
}
```

---

## Printing the Current Profile

```cpp
profileSelector.printCurrentProfile();
// Output (via Logger):
// [INFO] BatteryProfileSelector: type=1 maxV=16800mV rechargeV=16500mV maxI=10000mA ...
```

---

## Resetting to Defaults

To force a reset to the compiled-in default, erase the `"battery"` NVS namespace. The easiest way is to erase all NVS via the ESP-IDF monitor tool:

```bash
pio run -e esp32dev -t erase  # erases entire flash (use with caution)
```

Or erase only the NVS partition if you have a partition table tool available.
