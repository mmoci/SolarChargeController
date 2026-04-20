#pragma once

#include <vector>
#include "Device.h"
#include "ActuatorIf.h"
#include "Utility.h"

namespace DPSxDcConverterConfig
{
    static constexpr ActuatorIf::ControlMode CONTROL_MODE {ActuatorIf::ControlMode::VOLTAGE_SETPOINT};
    // DPS5005 Modbus register resolution:
    //   U_SET / UOUT / UIN : 0.01V/bit   → register 5000 = 50.00V
    //   I_SET              : 0.001A/bit  → register 5000 = 5.000A  (write setpoint)
    //   IOUT               : 0.01A/bit   → register  500 = 5.00A   (read display value)
    // MAX values represent the register value written at 100% control output.
    static constexpr int MAX_MPPT_VOLTAGE_CONTROL_VALUE   {5000}; // 50.00V in 0.01V/bit units
    static constexpr int MAX_MPPT_CURRENT_CONTROL_VALUE   {5000}; // 5.000A in 0.001A/bit units
    static constexpr int MAX_SOFT_STEP                    {1};    // Conservative: Modbus RTU ~50-100ms per cycle → slow response, so we limit soft step to prevent overshooting. May need tuning based on system response and stability.

    constexpr int selectControlValueFromControlMode()
    {
        if constexpr (CONTROL_MODE == ActuatorIf::ControlMode::VOLTAGE_SETPOINT) 
            return MAX_MPPT_VOLTAGE_CONTROL_VALUE;
        else if constexpr (CONTROL_MODE == ActuatorIf::ControlMode::CURRENT_SETPOINT)
            return MAX_MPPT_CURRENT_CONTROL_VALUE;
        else
        {
            static_assert(true, "Unsupported control mode");
            return 0;
        }    
    }   

    static constexpr int MAX_MPPT_CONTROL_VALUE     {selectControlValueFromControlMode()};
    static constexpr int DEFAULT_MPPT_CONTROL_VALUE {MAX_MPPT_CONTROL_VALUE / 2};

    // DPS5005 factory default Modbus slave address = 1 (configurable on the device menu).
    static constexpr uint8_t  SLAVE_ADDRESS              {0x01};
    static constexpr uint16_t MESSAGE_TMO                {100};   // ms — time to wait for a Modbus response
    static constexpr uint8_t  MAX_READS_BEFORE_WRITE     {3};     // read cycles between write cycles
    static constexpr uint16_t ERROR_RECOVERY_TMO         {10000}; // ms — pause duration after too many errors
    static constexpr uint8_t  CONSECUTIVE_ERRORS_THRESHOLD {5};   // errors before triggering recovery pause
}

/**
 * Important timing note:
 * - Modbus latency: ~50-100ms per read/write cycle
 * - This creates ~100ms feedback delay for voltage adjustment
 * - ChargeController soft ramp (5 units/cycle) is NOT appropriate for this latency
 * - Recommend: Increase MAX_CONTROL_SOFT_STEP to 1 or 2 for DPS variant
 */
class DPSxDcConverter : public Device, public ActuatorIf
{
    public:
    // Device overrides
    void init() override;
    void update() override;
    
    // ActuatorIf overrides
    ControlMode getControlMode() const override;
    int getMinControl() const override;
    int getMaxControl() const override;
    bool hasMeasurements() const override;
    int  getMaxSoftStep()  const override { return DPSxDcConverterConfig::MAX_SOFT_STEP; }
    void applyControl(int controlValue) override;
    
    int getInVoltage_mV() const {return m_inVoltage_mV;}
    int getVoltage_mV() const {return m_outVoltage_mV;};
    int getCurrent_mA() const {return m_outCurrent_mA;};
    unsigned long getLastUpdateTime() const {return m_lastUpdateTime;};

    private:
    enum class Register : uint16_t
    {
        // DPS5005 Modbus RTU register map
        U_SET   = 0x0000, ///< Voltage setpoint         (R/W, 0.01V/bit,  e.g. 2400 = 24.00V)
        I_SET   = 0x0001, ///< Current setpoint         (R/W, 0.001A/bit, e.g. 5000 = 5.000A)
        UOUT    = 0x0002, ///< Output voltage           (R,   0.01V/bit)
        IOUT    = 0x0003, ///< Output current           (R,   0.01A/bit)
        POWER   = 0x0004, ///< Output power             (R,   0.1W/bit)
        UIN     = 0x0005, ///< Input voltage            (R,   0.01V/bit)
        LOCK    = 0x0006, ///< Key-lock: 0=off 1=on     (R/W)
        PROTECT = 0x0007, ///< Protection flags: 0=OK 1=OVP 2=OCP 3=OPP (R)
        CV_CC   = 0x0008, ///< CV/CC status: 0=CV 1=CC  (R, read-only — NOT the output switch)
        ON_OFF  = 0x0009  ///< Output enable: 0=off 1=on (R/W)
    };

    enum class Function : uint8_t
    {
        READ_HOLDING_REGISTER  = 0x03,
        WRITE_SINGLE_REGISTER  = 0x06
    };

    enum class ModbusState
    {
        IDLE,
        WAITING,
        ERROR
    };

    enum class ModbusMessageType
    {
        NONE,
        READ,
        WRITE
    };

    // ===== Configuration =====
    static constexpr uint16_t CRC16_DEFAULT_VALUE    {0xFFFF};
    static constexpr uint16_t CRC16_POLYNOMIAL_VALUE {0xA001};
    static constexpr uint16_t FRAME_SIZE             {8}; // bytes — fixed by Modbus RTU spec
    static constexpr uint8_t  NUM_OF_REGISTERS_TO_READ {4};

    // ===== State Variables =====
    ControlMode m_controlMode{DPSxDcConverterConfig::CONTROL_MODE};
    int m_inVoltage_mV{};
    int m_outVoltage_mV{};
    int m_outCurrent_mA{};
    int m_setPointValue{};
    ModbusState m_messageState{};
    ModbusMessageType m_activeMessageType{};
    bool m_writeMessagePending{};
    Register m_activeWriteRegister{Register::U_SET}; ///< Captured at write dispatch time
    uint16_t m_activeWriteData{};                    ///< Captured at write dispatch time
    bool m_outputEnabled{false};                     ///< Tracks current ON_OFF state; toggled by applyControl()
    bool m_onOffPending{false};                      ///< Queues an ON_OFF write when output enable state changes
    bool m_iSetInitPending{};                        ///< Write I_SET=max on first init (OCP ceiling)
    uint8_t m_readsSinceLastWrite{};
    Timer m_messageTimer{};

    // ===== Error Recovery Variables =====
    unsigned long m_lastUpdateTime{};
    int m_consecutiveErrors{};
    bool m_pauseRetrying{};
    Timer m_errorRecoveryTimer{};

    /**
     * @brief 
     * 
     */
    void handleModbusMessages();

    /**
     * @brief Synchronous, blocking with timeout is the correct first design...
     *        Message format:
     *        [slave_adr] [0x03] [reg_hi][reg_lo] [count_hi][count_lo] [crc_lo][crc_hi]
     * 
     * @param startAddress 
     * @param nrOfRegisters 
     * @param slaveAddress 
     * @return std::size_t 
     */
    std::size_t sendRegisterReadReq(Register startAddress, uint16_t nrOfRegisters, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

    /**
     * @brief Message format:
     *        [slave] [0x03] [byte_count] [data_hi][data_lo] ... repeated [crc_lo][crc_hi]
     * 
     * @param slaveAddress 
     * @param nrOfRegisters 
     * @return std::vector<uint16_t> 
     */
    std::vector<uint16_t> receiveRegisterReadRsp(uint16_t nrOfRegisters, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

    /**
     * @brief Synchronous, blocking with timeout is the correct first design...
     *        Message fromat: [slave] [0x06] [reg_hi][reg_lo] [value_hi][value_lo] [crc_lo][crc_hi]
     * 
     * @param address 
     * @param data 
     * @param slaveAddress 
     * @return std::size_t 
     */
    std::size_t sendRegisterWriteReq(Register address, uint16_t data, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

    /**
     * @brief Message format:
     *        [slave] [0x06] [reg_hi][reg_lo] [value_hi][value_lo] [crc_lo][crc_hi]
     * 
     */
    bool receiveRegisterWriteRsp(Register address, uint16_t data, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

     /**
     * @brief 
     * 
     */
    void clearRxLine();

    /**
     * @brief Creates a MODBUS-RT frame for sending.
     * 
     * @param buffer       Buffer where the frame will be placed
     * @param startAddress Address of the starting register
     * @param readWrite    Function bit that defines whether frame requests register read or write
     * @param data         When sending register read message this represents number of registers to read, 
     *                     when writting it represents data written in register
     * @param slaveAddress Address of the slave device towards which request is send
     */
    void createFrame(uint8_t* buffer, Register startAddress, Function readWrite, uint16_t data, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

    /**
     * @brief Uses MODBUS CRC16 algorithm to compute CRC bytes and appends them at the end of buffer.
     * 
     * @param buffer Buffer in which CRC bytes will be appended
     * @param size Size of the buffer without appended CRC bytes (make sure that you allocated size+2 array)
     */
    void appendCRC16Bytes(uint8_t* buffer, uint16_t size);

    /**
     * @brief Performs CRC16 verification (used on a received messages). 
     *        When compute CRC on all received bytes, including last two CRC bytes, 
     *        computation should result with 0 if there is no chnages in the received bytes.
     * 
     * @param buffer Represents byte array on which computation will be performed
     * @param size Size of the array
     * @return true if computed CRC bytes are equal to 0
     * @return false otherwhise
     */
    bool verifyCRC16(uint8_t* buffer, uint16_t size);

    /**
     * @brief Contains MODBUS CRC16 algorithm for computing CRC bytes
     * 
     * @param buffer Represents byte array on which computation will be performed
     * @param size Size of the array
     * @return uint16_t computed CRC bytes
     */
    uint16_t computeModbusCRC16(uint8_t* buffer, uint16_t size);

    /**
     * @brief 
     * 
     * @param controlMode 
     * @return Register 
     */
    Register selectRegisterType();
};