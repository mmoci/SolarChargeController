#include "DPSxDcConverter.h"
#include "Arduino.h"
#include "Logger.h"

static constexpr char TAG[] = "DPSxDcConverter";

void DPSxDcConverter::init()
{
    m_messageState = ModbusState::IDLE;
    m_writeMessagePending = false;
    m_activeMessageType = ModbusMessageType::NONE;
    m_readsSinceLastWrite = 0;
    m_lastUpdateTime = 0;
    m_consecutiveErrors = 0;
    m_pauseRetrying = false;
    m_outputEnabled   = false;
    m_onOffPending    = false;
    m_iSetInitPending = true;  // Set current limit ceiling to device max on startup
    m_errorRecoveryTimer.reset();
    m_messageTimer.reset();
}

void DPSxDcConverter::update()
{
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
}

void DPSxDcConverter::applyControl(int controlValue) 
{
    auto setPointValue{static_cast<int>(std::round(controlValue * getMaxControl() / 100.0))};

    // Floor: when output is enabled, never set U_SET below battery terminal voltage.
    // If U_SET < Uout the DPS cannot push current into the battery, Iout drops to 0,
    // pvPower = Uin×Iout = 0, the PV-unavailable timer fires and the system oscillates.
    if (controlValue > 0 && m_outVoltage_mV > 0)
    {
        const int battFloor = m_outVoltage_mV / 10 + DPSxDcConverterConfig::VOLTAGE_HEADROOM_BITS;
        setPointValue = std::max(setPointValue, battFloor);
    }

    if(m_setPointValue != setPointValue)
    {
        m_setPointValue = setPointValue;
        m_writeMessagePending = true;
    }

    // Manage output enable/disable: ON_OFF=1 when charging, ON_OFF=0 when stopped.
    // This ensures the DPS output relay tracks ChargeController intent, not DPS front-panel state.
    const bool shouldBeEnabled = (controlValue > 0);
    if (shouldBeEnabled != m_outputEnabled)
    {
        m_outputEnabled = shouldBeEnabled;
        m_onOffPending  = true;
        ESP_LOGI(TAG, "Output %s", shouldBeEnabled ? "ENABLED" : "DISABLED");
    }
}

ActuatorIf::ControlMode DPSxDcConverter::getControlMode() const
{
    return m_controlMode;
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
        // Priority 1: Disable output immediately — safety, never defer
        if (m_onOffPending && !m_outputEnabled)
        {
            m_activeWriteRegister = Register::ON_OFF;
            m_activeWriteData     = 0;
            m_onOffPending        = false;
            m_activeMessageType   = ModbusMessageType::WRITE;
            m_readsSinceLastWrite = 0;
        }
        // Priority 2: I_SET OCP ceiling — one-time init on startup
        else if (m_iSetInitPending)
        {
            m_activeWriteRegister = Register::I_SET;
            m_activeWriteData     = static_cast<uint16_t>(DPSxDcConverterConfig::MAX_MPPT_CURRENT_CONTROL_VALUE);
            m_iSetInitPending     = false;
            m_activeMessageType   = ModbusMessageType::WRITE;
            m_readsSinceLastWrite = 0;
        }
        // Priority 3: U_SET / I_SET setpoint — must be applied BEFORE enabling output
        // so the DPS never turns on with a stale or zero setpoint.
        else if(m_writeMessagePending && m_readsSinceLastWrite >= DPSxDcConverterConfig::MAX_READS_BEFORE_WRITE)
        {
            m_activeWriteRegister = selectRegisterType();
            m_activeWriteData     = static_cast<uint16_t>(m_setPointValue);
            m_activeMessageType   = ModbusMessageType::WRITE;
            m_readsSinceLastWrite = 0;
        }
        // Priority 4: Enable output — only after correct setpoint has been written
        else if (m_onOffPending && m_outputEnabled)
        {
            m_activeWriteRegister = Register::ON_OFF;
            m_activeWriteData     = 1;
            m_onOffPending        = false;
            m_activeMessageType   = ModbusMessageType::WRITE;
            m_readsSinceLastWrite = 0;
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
                    // UOUT/IOUT/UIN read registers: 0.01V/bit and 0.01A/bit → multiply by 10 to get mV / mA
                    // (I_SET write register uses 0.001A/bit — different resolution, write-only)
                    m_outVoltage_mV = buffer[0] * 10;
                    m_outCurrent_mA = buffer[1];     // IOUT: 0.001A/bit → 1 bit = 1mA
                    m_inVoltage_mV  = buffer[3] * 10;

                    m_consecutiveErrors = 0; // Reset error count on successful read
                    m_lastUpdateTime = millis(); // Update last successful read time
                    ESP_LOGD(TAG, "Read OK — Uout=%dmV Iout=%dmA Uin=%dmV", m_outVoltage_mV, m_outCurrent_mA, m_inVoltage_mV);
                    m_messageState = ModbusState::IDLE;
                    m_messageTimer.reset();
                    break;
                }
            }
            else
            {
                if(receiveRegisterWriteRsp(m_activeWriteRegister, m_activeWriteData))
                {
                    m_writeMessagePending = false;
                    m_consecutiveErrors = 0; // Reset error count on successful write
                    // Do NOT update m_lastUpdateTime here — writes don't produce measurement data.
                    // hasMeasurements() must stay false until the first successful READ so that
                    // isMeasurementValid() doesn't return true with Vbatt=0, which would trip
                    // BatteryManager into Fault before any real voltage has been observed.
                    ESP_LOGD(TAG, "Write OK \u2014 reg=0x%02X val=%d", static_cast<int>(m_activeWriteRegister), m_activeWriteData);
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
        ESP_LOGW(TAG, "Received register READ parse failed [%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X]",
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

    ESP_LOGD(TAG, "Send register WRITE [%02X %02X %02X %02X %02X %02X %02X %02X]",
        buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
    return Serial2.write(buffer, FRAME_SIZE);
}

bool DPSxDcConverter::receiveRegisterWriteRsp(Register address, uint16_t data, uint8_t slaveAddress)
{
    uint8_t buffer[FRAME_SIZE]{};

    if(Serial2.available() < FRAME_SIZE) 
        return false;

    if(Serial2.readBytes(buffer, FRAME_SIZE) != FRAME_SIZE)
        return false;

    ESP_LOGD(TAG, "Received register WRITE [%02X %02X %02X %02X %02X %02X %02X %02X]",
        buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);

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