#include "DPSxDcConverter.h"
#include "Arduino.h"
#include "Logger.h"

static constexpr char TAG[] = "DPSxDcConverter";

void DPSxDcConverter::init()
{
    m_messageState        = ModbusState::IDLE;
    m_activeMessageType   = ModbusMessageType::NONE;
    m_readsSinceLastWrite = 0;
    m_lastUpdateTime      = 0;
    m_consecutiveErrors   = 0;
    m_pauseRetrying       = false;
    m_outputState         = OutputState::OFF;
    m_setPointValue       = DPSxDcConverterConfig::MAX_MPPT_CURRENT_CONTROL_VALUE;

    // Enqueue initial writes to ensure a known safe state on startup: output disabled, OCP ceiling set, and U_SET at a safe voltage. 
    // These are processed with priority over any new control writes that arrive around the same time, so the DPS is guaranteed to start in a safe state.
    m_writeRequestQueue = {}; // Clear any pending writes that might be in the queue from a previous run (e.g. after a soft reset triggered by the watchdog)
    enqueWriteRequest(Register::ON_OFF, 0, true); // Force output OFF on startup regardless of current hardware state (DPS retains state across ESP32 resets)
    enqueWriteRequest(Register::I_SET, static_cast<uint16_t>(DPSxDcConverterConfig::MAX_MPPT_CURRENT_CONTROL_VALUE));
    enqueWriteRequest(Register::U_SET, static_cast<uint16_t>(m_uSetVoltage_bits));
    
    m_errorRecoveryTimer.reset();
    m_messageTimer.reset();
    m_startupTimer.trigger();
    m_openCircuitVoltageTimer.trigger();

    m_startupComplete = false;
    m_ocvRefreshPending = false;
}

void DPSxDcConverter::update()
{
    if (!m_startupComplete)
    {
        m_startupTimer.update();
        if (m_startupTimer.getDuration() < DPSxDcConverterConfig::STARTUP_DELAY_MS)
        {
            updateOpenCircuitVoltage();
            return;
        }
        clearRxLine(); // flush any UART garbage accumulated during ESP32 boot
        m_startupComplete = true;
    }

    if(m_pauseRetrying)
    {
        m_errorRecoveryTimer.update();
        if(m_errorRecoveryTimer.getDuration() >= DPSxDcConverterConfig::ERROR_RECOVERY_TMO)
        {
            ESP_LOGI(TAG, "Resuming communication after pause");
            m_pauseRetrying = false;
            m_errorRecoveryTimer.reset();
        }
        return;
    }

    handleModbusMessages();
    updateOpenCircuitVoltage();
}

void DPSxDcConverter::enableOutput(bool enable, bool priority)
{
    // Avoid redundant writes that would reset the DPS internal timer and cause unnecessary wear on the relay.
    // State is confirmed by periodic reads, so this check may lag hardware reality by one read cycle (~450ms),
    // but enqueWriteRequest deduplication ensures at most one pending write per register regardless.
    if ((enable && m_outputState == OutputState::ON) || (!enable && m_outputState == OutputState::OFF))
        return;

    enqueWriteRequest(Register::ON_OFF, static_cast<uint16_t>(enable), priority);
    ESP_LOGI(TAG, "Output %s", enable ? "ENABLED" : "DISABLED");
}

void DPSxDcConverter::applyControl(int controlValue) 
{
    auto setPointValue{static_cast<int>(std::round(controlValue * getMaxControl() / 100.0))};

    // Floor: only relevant in VOLTAGE_SETPOINT mode. U_SET must exceed Vbatt to push current into the battery; if the control value is too low, the DPS cannot regulate and the system oscillates.
    if (m_controlMode == ControlMode::VOLTAGE_SETPOINT && controlValue > 0 && m_outVoltage_mV > 0)
    {
        const int battFloor = m_outVoltage_mV / 10 + DPSxDcConverterConfig::VOLTAGE_HEADROOM_BITS;
        setPointValue = std::max(setPointValue, battFloor);
    }

    if(m_setPointValue != setPointValue)
    {
        m_setPointValue = setPointValue;
        enqueWriteRequest(selectRegisterType(), static_cast<uint16_t>(m_setPointValue));
    }
}

void DPSxDcConverter::updateOpenCircuitVoltage()
{
    m_openCircuitVoltageTimer.update();

    const bool panelPresent = (m_inVoltage_mV > std::max(m_outVoltage_mV, 5000) + 1000);
    const bool hasLowOutputCurrent = hasMeasurements() && areMeasurementsSettled() && m_outCurrent_mA < 100;
    
    // Refresh open-circuit voltage if the timer has elapsed or if the panel is present but output current is very low
    if (!m_ocvRefreshPending && m_outputState == OutputState::ON &&
        ((m_openCircuitVoltageTimer.getDuration() >= DPSxDcConverterConfig::OPEN_CIRCUIT_VOLTAGE_REFRESH_RATE_MS) ||
        (panelPresent && hasLowOutputCurrent && m_openCircuitVoltageTimer.getDuration() >= DPSxDcConverterConfig::LOW_POWER_OCV_COOLDOWN_MS))) 
    {
        m_ocvRefreshPending = true; // Flag to indicate that an OCV refresh is pending
        enableOutput(false, true);  // Urgent OFF to let Vin rise to Voc
        m_openCircuitVoltageTimer.trigger();
    }

    // Once output is disabled and measurements have settled, capture the open-circuit voltage and re-enable output
    if (m_ocvRefreshPending && m_outputState == OutputState::OFF && areMeasurementsSettled())
    {
        m_openCircuitVoltage_mV = getInVoltage_mV();
        ESP_LOGI(TAG, "Open-circuit voltage refreshed: %dmV", m_openCircuitVoltage_mV);
        m_ocvRefreshPending = false;
        enableOutput(true);
    }

    // Startup capture: output is already OFF (natural Voc state), grab it directly
    if (!m_ocvRefreshPending && m_outputState == OutputState::OFF && hasMeasurements() && areMeasurementsSettled() && m_openCircuitVoltage_mV <= 0)
    {
        m_openCircuitVoltage_mV = getInVoltage_mV();
        ESP_LOGI(TAG, "Open-circuit voltage captured at startup: %dmV", m_openCircuitVoltage_mV);
    }
}

ActuatorIf::ControlMode DPSxDcConverter::getControlMode() const
{
    return m_controlMode;
}

void DPSxDcConverter::setBatteryProfile(const BatteryProfile& profile)
{
    // Compute the hardware OVP ceiling from the battery max voltage + headroom.
    // This is the fallback if the software CV controller fails to limit voltage.
    // Clamped to the device maximum so an out-of-range profile cannot produce an
    // invalid Modbus register value.
    const int requested = profile.maxVoltage_mV / 10 + DPSxDcConverterConfig::OVP_CEILING_HEADROOM_BITS;
    const int clamped   = std::min(requested, DPSxDcConverterConfig::MAX_MPPT_VOLTAGE_CONTROL_VALUE);

    if (clamped != m_uSetVoltage_bits)
    {
        m_uSetVoltage_bits = clamped;
        enqueWriteRequest(Register::U_SET, static_cast<uint16_t>(m_uSetVoltage_bits)); // Schedule a U_SET write with the new ceiling
        ESP_LOGI(TAG, "Battery profile updated — U_SET OVP ceiling set to %d (%.2fV)", m_uSetVoltage_bits, m_uSetVoltage_bits * 0.01f);
    }
}

int DPSxDcConverter::getMinControl() const
{
    return 0;
}

int DPSxDcConverter::getMaxControl() const
{
    return DPSxDcConverterConfig::MAX_MPPT_CONTROL_VALUE;
}

bool DPSxDcConverter::hasMeasurements() const
{
    return m_lastUpdateTime != 0;
}

void DPSxDcConverter::handleModbusMessages()
{
    const uint16_t nrOfRegisters{NUM_OF_REGISTERS_TO_READ};

    switch(m_messageState)
    {
        case ModbusState::IDLE:
        if (!m_writeRequestQueue.empty()) // Prioritise writes if there are too many reads without a write, but don't interrupt an in-flight write
        {
            const auto& request = m_writeRequestQueue.front();

            if(request.urgent || m_readsSinceLastWrite >= DPSxDcConverterConfig::MAX_READS_BEFORE_WRITE)
            {
                m_activeMessageType   = ModbusMessageType::WRITE;
                m_activeWriteRegister = request.address;
                m_activeWriteData     = request.data;
                m_readsSinceLastWrite = 0;
            }
            else
            {
                m_activeMessageType = ModbusMessageType::READ;
                ++m_readsSinceLastWrite;
            }
        }
        else
        {
            m_activeMessageType = ModbusMessageType::READ;
            ++m_readsSinceLastWrite;
        }
            
        clearRxLine();

        // sendRegisterReadReq and sendRegisterWriteReq could be joined into same function, they have same signiture.
        // Difference is that sendRegisterReadReq requires nrOfRegisters while other requires data to be written
        if(m_activeMessageType == ModbusMessageType::READ)
        {
            if(sendRegisterReadReq(Register::UOUT, nrOfRegisters) != FRAME_SIZE)
            {
                m_messageState = ModbusState::ERROR;
                ++m_consecutiveErrors;
                ESP_LOGE(TAG, "Failed to send read request (error #%d)", m_consecutiveErrors);
                break;
            }
        }
        else
        {
            if(sendRegisterWriteReq(m_activeWriteRegister, m_activeWriteData) != FRAME_SIZE)
            {
                m_messageState = ModbusState::ERROR;
                ++m_consecutiveErrors;
                ESP_LOGE(TAG, "Failed to send write request (error #%d)", m_consecutiveErrors);
                break;
            }
        }
            
        m_messageState = ModbusState::WAITING;
        m_messageTimer.trigger();
        break;

        case ModbusState::WAITING:
        {
            m_messageTimer.update();

            if(m_activeMessageType == ModbusMessageType::READ)
            {
                std::vector<uint16_t> buffer = receiveRegisterReadRsp(nrOfRegisters);
                if(!buffer.empty())
                {
                    // UOUT/IOUT/UIN/ON_OFF read registers: 0.01V/bit, 0.001A/bit, 0.01V/bit, and bit 0 = ON/OFF, respectively
                    m_outVoltage_mV = buffer[0] * 10;
                    m_outCurrent_mA = buffer[1];  // Different resolution IOUT: 0.001A/bit → 1 bit = 1mA
                    m_inVoltage_mV  = buffer[3] * 10;
                    m_outputState   = (buffer[7] & 0x0001) ? OutputState::ON : OutputState::OFF;

                    m_consecutiveErrors = 0; // Reset error count on successful read
                    m_lastUpdateTime = millis(); // Update last successful read time
                    ESP_LOGD(TAG, "Read OK — OutputState=%s Vout=%dmV Iout=%dmA Vin=%dmV", 
                        m_outputState == OutputState::ON ? "ON" : "OFF", m_outVoltage_mV, m_outCurrent_mA, m_inVoltage_mV);
                    m_messageState = ModbusState::IDLE;
                    m_messageTimer.reset();
                    break;
                }
            }
            else
            {
                if(receiveRegisterWriteRsp(m_activeWriteRegister, m_activeWriteData))
                {
                    m_writeRequestQueue.pop_front(); // Remove the completed write request from the queue
                    m_consecutiveErrors = 0; // Reset error count on successful write
                    // Do NOT update m_lastUpdateTime here — writes don't produce measurement data.
                    // hasMeasurements() must stay false until the first successful READ so that
                    // isMeasurementValid() doesn't return true with Vbatt=0, which would trip
                    // BatteryManager into Fault before any real voltage has been observed.
                    ESP_LOGD(TAG, "Write OK \u2014 register %s value=%d", registerAddressToString(m_activeWriteRegister).c_str(), m_activeWriteData);
                    m_messageState = ModbusState::IDLE;
                    m_messageTimer.reset();
                    break;
                }
            }

            if(m_messageTimer.getDuration() >= DPSxDcConverterConfig::MESSAGE_TMO)
            {
                m_messageState = ModbusState::ERROR;
                ++m_consecutiveErrors;
                ESP_LOGW(TAG, "Message timeout (error #%d)", m_consecutiveErrors);
            }
            break;
        }

        case ModbusState::ERROR:
        clearRxLine(); // Flush any late-arriving stale response before next request
        if(m_consecutiveErrors >= DPSxDcConverterConfig::CONSECUTIVE_ERRORS_THRESHOLD)
        {
            ESP_LOGE(TAG, "Too many consecutive errors: %d. Pausing...", m_consecutiveErrors);
            m_pauseRetrying = true;
            m_consecutiveErrors = 0;
            m_errorRecoveryTimer.trigger();
        }
        m_messageState = ModbusState::IDLE;
        m_messageTimer.reset();
        break;
    }
}

void DPSxDcConverter::enqueWriteRequest(Register address, uint16_t data, bool urgent)
{
    for(auto& request : m_writeRequestQueue)
    {
        if(request.address == address)
        {
            // If there's already a pending write to the same register, update it with the new value and urgency
            request.data = data;
            request.urgent = request.urgent || urgent; // Once a request is marked urgent, it stays urgent until sent
            ESP_LOGD(TAG, "Updated pending write request for register %s to value=%d (urgent=%s)", registerAddressToString(address).c_str(), data, request.urgent ? "true" : "false");
            return; // Deduplicated — do not add a second entry for the same register
        }
    }
    if(urgent)
        m_writeRequestQueue.push_front({address, data, urgent});
    else
        m_writeRequestQueue.push_back({address, data, urgent});
}

// ----------------------------------
//  MODBUS RTU Communication Helpers
// ----------------------------------

std::size_t DPSxDcConverter::sendRegisterReadReq(Register startAddress, uint16_t nrOfRegisters, uint8_t slaveAddress)
{
    uint8_t buffer[FRAME_SIZE]{};
    
    // Fills FRAME_SIZE - 2 bytes
    createFrame(buffer, startAddress, Function::READ_HOLDING_REGISTER, nrOfRegisters, slaveAddress);

    // Fills last 2 bytes of the FRAME_SIZE with CRC bytes.
    appendCRC16Bytes(buffer, FRAME_SIZE - 2);

    ESP_LOGD(TAG, "Send register READ  [%02X %02X %02X %02X %02X %02X %02X %02X]",
        buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
    return Serial2.write(buffer, FRAME_SIZE);
}

std::vector<uint16_t> DPSxDcConverter::receiveRegisterReadRsp(uint16_t nrOfRegisters, uint8_t slaveAddress)
{
    constexpr uint16_t FRAME_FIXED_BYTES = 5;
    const uint16_t SIZE{static_cast<uint16_t>(FRAME_FIXED_BYTES + 2 * nrOfRegisters)};
    uint8_t buffer[SIZE]{};
    std::vector<uint16_t> rawValues{};
    rawValues.reserve(nrOfRegisters);

    if(Serial2.available() < SIZE)
        return {};

    if(Serial2.readBytes(buffer, SIZE) != SIZE ||
        buffer[0] != slaveAddress ||
        buffer[1] != static_cast<uint8_t>(Function::READ_HOLDING_REGISTER) ||
        buffer[2] != nrOfRegisters * 2 ||
        !verifyCRC16(buffer, SIZE))
    {
        ESP_LOGW(TAG, "Received register READ parse failed [%s %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X]",
            registerAddressToString(static_cast<Register>(buffer[2])).c_str(),
            buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6],
            buffer[7], buffer[8], buffer[9], buffer[10], buffer[11], buffer[12]);
        clearRxLine();
        return {};
    }
    
    for(int byte{3}; byte < SIZE - 2; byte+=2)
    {
        rawValues.push_back((buffer[byte] << 8) | buffer[byte+1]);
    }

    return rawValues;
}

std::size_t DPSxDcConverter::sendRegisterWriteReq(Register address, uint16_t data, uint8_t slaveAddress)
{
    uint8_t buffer[FRAME_SIZE]{};

    // Fills FRAME_SIZE - 2 bytes
    createFrame(buffer, address, Function::WRITE_SINGLE_REGISTER, data, slaveAddress);

    // Fills last 2 bytes of the FRAME_SIZE with CRC bytes.
    appendCRC16Bytes(buffer, FRAME_SIZE - 2);

    ESP_LOGD(TAG, "Send register WRITE [%s %02X %02X %02X %02X %02X %02X %02X]",
        registerAddressToString(address).c_str(), buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
    return Serial2.write(buffer, FRAME_SIZE);
}

bool DPSxDcConverter::receiveRegisterWriteRsp(Register address, uint16_t data, uint8_t slaveAddress)
{
    uint8_t buffer[FRAME_SIZE]{};

    if(Serial2.available() < FRAME_SIZE) 
        return false;

    if(Serial2.readBytes(buffer, FRAME_SIZE) != FRAME_SIZE)
        return false;

    ESP_LOGD(TAG, "Received register WRITE [%s %02X %02X %02X %02X %02X %02X %02X]",
        registerAddressToString(address).c_str(), buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);

    if(!verifyCRC16(buffer, FRAME_SIZE))
    {
        ESP_LOGW(TAG, "Received register WRITE CRC failed");
        clearRxLine();
        return false;
    }
    
    if(buffer[0] != slaveAddress)
    {
        ESP_LOGW(TAG, "Received register WRITE wrong slave: got 0x%02X expected 0x%02X", buffer[0], slaveAddress);
        return false;
    }
        
    if(buffer[1] != static_cast<uint8_t>(Function::WRITE_SINGLE_REGISTER))
    {
        ESP_LOGW(TAG, "Received register WRITE wrong function: got 0x%02X", buffer[1]);
        clearRxLine();
        return false;
    }

    uint16_t receivedAddress = (buffer[2] << 8) | buffer[3];

    if(receivedAddress != static_cast<uint16_t>(address))
    {
        ESP_LOGW(TAG, "Received register WRITE wrong register: got 0x%04X expected 0x%04X", receivedAddress, static_cast<uint16_t>(address));
        clearRxLine();
        return false;
    }

    uint16_t receivedData = (buffer[4] << 8) | buffer[5];
    if(receivedData != data)
    {
        ESP_LOGW(TAG, "Received register WRITE wrong value: got %u expected %u", receivedData, data);
        return false;
    }

    return true;
}

void DPSxDcConverter::clearRxLine()
{
    while(Serial2.available())
        Serial2.read();
}

void DPSxDcConverter::createFrame(uint8_t* buffer, Register startAddress,  Function readWrite, uint16_t data, uint8_t slaveAddress)
{        
    buffer[0] = slaveAddress;

    buffer[1] = static_cast<uint8_t>(readWrite);

    buffer[2] = static_cast<uint16_t>(startAddress) >> 8 & 0xFF;
    buffer[3] = static_cast<uint16_t>(startAddress) & 0xFF;

    // When reading data represents nrOfRegisters, when writting it represents data written in the register
    buffer[4] = (data >> 8) & 0xFF;
    buffer[5] = data & 0xFF;
}

void DPSxDcConverter::appendCRC16Bytes(uint8_t* buffer, uint16_t size)
{
    uint16_t crcBytes {computeModbusCRC16(buffer, size)};

    buffer[size] = static_cast<uint8_t>(crcBytes & 0xFF);
    buffer[size + 1] = static_cast<uint8_t>((crcBytes >> 8) & 0xFF);
}

bool DPSxDcConverter::verifyCRC16(uint8_t* buffer, uint16_t size)
{
    auto crcBytes{computeModbusCRC16(buffer, size)};
    return crcBytes == 0x0000;
}

uint16_t DPSxDcConverter::computeModbusCRC16(uint8_t* buffer, uint16_t size)
{
    // Modbus uses 2 bytes for CRC with default value 0xFFFF
    uint16_t crcBytes {CRC16_DEFAULT_VALUE};

    // Iterate over all bytes in the frame
    for(uint16_t byte{0}; byte < size; ++byte)
    {
        crcBytes ^= buffer[byte];

        // Iterate over all bits in the current byte
        for(uint8_t i{0}; i < 8; ++i)
        {
            if(crcBytes & 0x0001)
            {
                crcBytes >>= static_cast<uint16_t>(1);
                crcBytes ^= static_cast<uint16_t>(CRC16_POLYNOMIAL_VALUE);
            }
            else
                crcBytes >>= static_cast<uint16_t>(1);
        }
    }
    return crcBytes;
}

DPSxDcConverter::Register DPSxDcConverter::selectRegisterType()
{
    return (m_controlMode == ControlMode::CURRENT_SETPOINT) ? Register::I_SET : Register::U_SET;
}

std::string DPSxDcConverter::registerAddressToString(Register reg)
    {
        switch(reg)
        {
            case Register::U_SET: return "U_SET";
            case Register::I_SET: return "I_SET";
            case Register::UOUT:  return "UOUT";
            case Register::IOUT:  return "IOUT";
            case Register::POWER: return "POWER";
            case Register::UIN:   return "UIN";
            case Register::LOCK:  return "LOCK";
            case Register::PROTECT: return "PROTECT";
            case Register::CV_CC: return "CV_CC";
            case Register::ON_OFF: return "ON_OFF";
            default: return "UNKNOWN_REGISTER";
        }
    }