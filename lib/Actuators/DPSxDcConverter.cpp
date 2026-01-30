#include "DPSxDcConverter.h"

void DPSxDcConverter::init()
{
    m_messageState = ModbusState::IDLE;
    m_pendigMessageType = ModbusMessageType::READ;
    m_lastMessageType = ModbusMessageType::NONE;
    m_messageTimer.reset();
}

void DPSxDcConverter::update()
{
    if(m_messageState == ModbusState::IDLE)
    {
        if(m_pendigMessageType == ModbusMessageType::WRITE && m_lastMessageType != ModbusMessageType::WRITE)
        {
            //setCurrent(m_controlValue);
        }
        else
            updateVoltageAndCurrentData();
    }
}

void DPSxDcConverter::applyControl(int controlValue) 
{
    if(m_controlValue != controlValue)
    {
        m_controlValue = controlValue;
        m_pendigMessageType = ModbusMessageType::WRITE;
    }
}

void DPSxDcConverter::updateVoltageAndCurrentData()
{
    const uint16_t nrOfRegisters = 4;

    switch(m_messageState)
    {
        case ModbusState::IDLE:
        m_lastMessageType = ModbusMessageType::READ;
        clearRxLine();
        if(sendRegisterReadReq(Register::UOUT, nrOfRegisters) != FRAME_SIZE)
        {
            m_messageState = ModbusState::ERROR;
            break;
        }
            
        m_messageState = ModbusState::WAITING;
        m_messageTimer.trigger();
        break;

        case ModbusState::WAITING:
        {
            m_messageTimer.update();

            std::vector<uint16_t> results{receiveRegisterReadRsp(nrOfRegisters)};
            if(!results.empty())
            {
                m_outVoltage_mV = results[0] * 1000;
                m_outCurrent_mA = results[1] * 1000;
                m_inVoltage_mV  = results[3] * 1000;
                m_messageState = ModbusState::IDLE;
                m_messageTimer.reset();
            }
            else if(m_messageTimer.getDuration() >= MESSAGE_TMO)
            {
                m_messageState = ModbusState::ERROR;
            }
            break;
        }

        case ModbusState::ERROR:
        Serial.println("[DPSxDcConverter]: Voltage and current update failed!");
        m_messageState = ModbusState::IDLE;
        m_messageTimer.reset();
        break;
    }
}

void DPSxDcConverter::setOutputCurrent()
{
    // TODO Function should implement state machine. It is important that it sets 
    // m_lastMessageType = ModbusMessageType::WRITE; could we before invoking actual write
    // and it needs to remove ModbusMessageType::WRITE from m_pendigMessageType
    // m_pendigMessageType = ModbusMessageType::READ;
    // In updateVoltageAndCurrentData() we do not change m_pendigMessageType as we want that m_pendigMessageType is 
    // always ModbusMessageType::READ unless there is a specific request for writting received through applyControl().
    // we could cover this using std::queue probably
}

std::size_t DPSxDcConverter::sendRegisterReadReq(Register startAddress, uint16_t nrOfRegisters, uint8_t slaveAddress)
{
    uint8_t buffer[FRAME_SIZE]{};
    
    // Fills FRAME_SIZE - 2 bytes
    createFrame(buffer, startAddress, Function::READ_HOLDING_REGISTER, nrOfRegisters, slaveAddress);

    // Fills last 2 bytes of the FRAME_SIZE with CRC bytes.
    appendCRC16Bytes(buffer, FRAME_SIZE - 2);

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

    return Serial2.write(buffer, FRAME_SIZE);
}

bool DPSxDcConverter::receiveRegisterWriteRsp(Register address, uint16_t data, uint8_t slaveAddress)
{
    uint8_t buffer[FRAME_SIZE]{};

    if(Serial2.available() < FRAME_SIZE) 
        return false;

    if(Serial2.readBytes(buffer, FRAME_SIZE) != FRAME_SIZE)
        return false;

    if(!verifyCRC16(buffer, FRAME_SIZE))
    {
        clearRxLine();
        return false;
    }
    
    if( buffer[0] != slaveAddress)
        return false;
        
    if(buffer[1] != static_cast<uint8_t>(Function::WRITE_SINGLE_REGISTER))
    {
        clearRxLine();
        return false;
    }

    uint16_t receivedAddress = (buffer[2] << 8) | buffer[3];

    if(receivedAddress != static_cast<uint16_t>(address))
    {
        clearRxLine();
        return false;
    }

    uint16_t receivedData = (buffer[4] << 8) | buffer[5];
    if(receivedData != data)
        return false;

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
    uint16_t crcBytes {computeCRC16Bytes(buffer, size)};

    buffer[size] = static_cast<uint8_t>(crcBytes & 0xFF);
    buffer[size + 1] = static_cast<uint8_t>((crcBytes >> 8) & 0xFF);
}

bool DPSxDcConverter::verifyCRC16(uint8_t* buffer, uint16_t size)
{
    auto crcBytes{computeCRC16Bytes(buffer, size)};
    return crcBytes == 0x0000;
}

uint16_t DPSxDcConverter::computeCRC16Bytes(uint8_t* buffer, uint16_t size)
{
    // Modbus uses 2 bytes for CRC with default value 0xFFFF
    uint16_t crcBytes {CRC16_DEFAULT_VALUE};

    // Iterate over all bytes in the frame
    for(uint16_t byte{0}; byte < size; ++byte)
    {
        crcBytes ^= buffer[byte];

        for(uint8_t i{0}; i < 8; ++i)
        {
            if(crcBytes & 0x00001)
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


// NOT USED, WILL BE REMOVED LATTER

/*
bool DPSxDcConverter::readRegister(Register address, uint16_t& result, uint8_t slaveAddress)
{
    std::vector<uint16_t> results{};
    result = results[0];
    return readRegisters(address, results, 1, slaveAddress);
}

bool DPSxDcConverter::readRegisters(Register startAddress, std::vector<uint16_t>& results, uint16_t nrOfRegisters, uint8_t slaveAddress)
{
    if(sendRegisterReadReq(startAddress, nrOfRegisters, slaveAddress) != FRAME_SIZE)
        return false;

    unsigned long start {millis()};
    while(millis() - start < MESSAGE_TMO)
    {
        auto values {receiveRegisterReadRsp(slaveAddress, nrOfRegisters)};

        if(!values.empty())
        {
            results = std::move(values);
            return true;
        }
        delay(1);
    }
    return false;
}

bool DPSxDcConverter::writeRegister(Register address, uint16_t data, uint8_t slaveAddress)
{
    if(sendRegisterWriteReq(address, data, slaveAddress) != FRAME_SIZE)
        return false;

    unsigned long start {millis()};
    while(millis() - start < MESSAGE_TMO)
    {
        if(receiveRegisterWriteRsp(address, data, slaveAddress)) 
            return true;
        delay(1);
    }
    return false;
}
*/