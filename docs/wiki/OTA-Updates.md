# OTA Updates

Over-the-air (OTA) firmware updates allow you to flash new firmware to the ESP32 over WiFi without a USB connection. OTA is only compiled when `-D MQTT_CLIENT` is set (it requires WiFi, which is managed by `MqttClient`).

---

## How It Works

The `OtaHandler` class wraps `ArduinoOTA` from the ESP32 Arduino framework.

- Registers with mDNS under the hostname configured in `main.cpp`:

```cpp
OtaHandler otaHandler{{.hostname = DEVICE_ID, .password = OTA_PASSWORD}, &actuator};
// Device appears as: solar_controller_1.local
```

- A password is required. Set `OTA_PASSWORD` in `include/secrets.h`.
- `otaHandler.handle()` must be called every `loop()` iteration to service pending OTA requests.

---

## Safety Behaviour

When an OTA update begins, `OtaHandler` immediately drives the actuator output to **0** before the flash write starts:

```
ArduinoOTA.onStart callback
    └── actuator->applyControl(0)   // stop charging
```

This is a safety measure because `loop()` is blocked for the entire duration of the flash write. Without it, the converter would hold its last duty cycle for several seconds in an uncontrolled state, potentially overcharging a battery already in CV mode.

---

## Uploading via PlatformIO

Add the OTA upload target to `platformio.ini`:

```ini
[env:esp32dev-ota]
extends = env:esp32dev
upload_protocol = espota
upload_port = solar_controller_1.local   ; or use the IP: 192.168.1.50
upload_flags = --auth=yourOtaPassword
```

Then upload:

```bash
pio run -e esp32dev-ota -t upload
```

---

## Uploading via Arduino IDE

1. In the Arduino IDE, go to **Tools → Port**.
2. Under "Network ports", you should see **solar_controller_1 at 192.168.1.50** (or similar).
3. Select it.
4. Click **Upload**. When prompted for a password, enter the value of `OTA_PASSWORD`.

---

## Troubleshooting OTA

| Symptom | Cause | Fix |
|---|---|---|
| Device not visible in IDE / PlatformIO | mDNS not working on your network | Use the static IP address directly instead of `.local` |
| "Wrong password" error | `OTA_PASSWORD` mismatch | Check `secrets.h` and the upload config match |
| OTA starts but fails mid-flash | Large binary / WiFi interference | Try a wired Ethernet adapter or move ESP32 closer to the router |
| Device restarts but boots old firmware | Partition table issue | Erase flash fully and re-flash via USB |

---

## Security Notes

- Always set a strong `OTA_PASSWORD` — the default template value (`your_ota_password`) is **not safe for production**.
- OTA is only accessible while the device is on the same network as the uploader.
- There is no rollback mechanism — if the new firmware crashes on boot, you will need USB access to recover.
