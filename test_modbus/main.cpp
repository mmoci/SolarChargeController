#include <Arduino.h>
#include <ModbusMaster.h>

// ── Pins: adjust if your wiring differs ──────────────────────────────────────
static constexpr int RX_PIN  = 16;  // ESP32 GPIO16 → DPS5005 TX
static constexpr int TX_PIN  = 17;  // ESP32 GPIO17 → DPS5005 RX

// ── DPS5005 Modbus defaults ───────────────────────────────────────────────────
static constexpr uint8_t  SLAVE_ADDR = 1;
static constexpr uint32_t BAUD_RATE  = 9600;

ModbusMaster node;

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== DPS5005 Modbus RTU reference test ===");
    Serial.printf("Serial2 RX=GPIO%d  TX=GPIO%d  %lu baud  slave=%u\n",
                  RX_PIN, TX_PIN, BAUD_RATE, SLAVE_ADDR);

    Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
    node.begin(SLAVE_ADDR, Serial2);
}

void loop()
{
    // Read 6 holding registers starting at 0x0000:
    //   [0] U_SET  — voltage setpoint  (0.01 V/bit)
    //   [1] I_SET  — current setpoint  (0.001 A/bit)
    //   [2] UOUT   — output voltage    (0.01 V/bit)
    //   [3] IOUT   — output current    (0.01 A/bit)
    //   [4] POWER  — output power      (0.1  W/bit)
    //   [5] UIN    — input voltage     (0.01 V/bit)
    uint8_t result = node.readHoldingRegisters(0x0000, 6);

    if (result == ModbusMaster::ku8MBSuccess)
    {
        Serial.printf("[OK]   U_SET=%.2fV  I_SET=%.3fA  Uout=%.2fV  Iout=%.2fA  Power=%.1fW  Uin=%.2fV\n",
            node.getResponseBuffer(0) * 0.01f,
            node.getResponseBuffer(1) * 0.001f,
            node.getResponseBuffer(2) * 0.01f,
            node.getResponseBuffer(3) * 0.01f,
            node.getResponseBuffer(4) * 0.1f,
            node.getResponseBuffer(5) * 0.01f);
    }
    else
    {
        // Common error codes from ModbusMaster:
        //   0xE0 ku8MBIllegalFunction     — device rejected the function code
        //   0xE1 ku8MBIllegalDataAddress  — register address rejected
        //   0xE2 ku8MBInvalidSlaveID      — timeout, no response at all
        //   0xE4 ku8MBResponseTimedOut    — timeout
        //   0xE7 ku8MBInvalidCRC          — response received but CRC failed
        Serial.printf("[FAIL] code=0x%02X  ", result);
        switch (result)
        {
            case 0xE2: Serial.println("→ No response (timeout) — check baud rate, slave address, TX/RX wiring, GND connection"); break;
            case 0xE4: Serial.println("→ Response timeout — device too slow or baud mismatch"); break;
            case 0xE7: Serial.println("→ CRC mismatch — data corruption or baud rate mismatch"); break;
            default:   Serial.println("→ See ModbusMaster error codes"); break;
        }
    }

    delay(1000);
}
