#pragma once

#include <optional>
#include <vector>
#include <deque>
#include "Device.h"
#include "ActuatorIf.h"
#include "Utility.h"

namespace DPSxDcConverterConfig
{
    // CURRENT_SETPOINT: I_SET is the MPPT control variable. Increasing I_SET draws more
    // current from the PV source, causing Vin to drop along the panel I-V curve. P&O tracks
    // Vin×Iout and adjusts I_SET to find the MPP. This is the physically correct variable for
    // a CC/CV supply like the DPS5005: U_SET in voltage mode is always overridden by the battery
    // when U_SET >> Vbatt, giving MPPT no effective leverage over the operating point.
    static constexpr ActuatorIf::ControlMode CONTROL_MODE {ActuatorIf::ControlMode::CURRENT_SETPOINT};
    // DPS5005 Modbus register resolution:
    //   U_SET / UOUT / UIN : 0.01V/bit   → register 5000 = 50.00V
    //   I_SET              : 0.001A/bit  → register 5000 = 5.000A  (write setpoint)
    //   IOUT               : 0.001A/bit  → register 5000 = 5.000A  (read display value)
    // MAX values represent the register value written at 100% control output.
    static constexpr int MAX_MPPT_VOLTAGE_CONTROL_VALUE   {5000}; // 50.00V in 0.01V/bit units
    static constexpr int MAX_MPPT_CURRENT_CONTROL_VALUE   {3000}; // 3.000A in 0.001A/bit units

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

    // DPS5005 factory default Modbus slave address = 1 (configurable on the device menu).
    static constexpr uint8_t  SLAVE_ADDRESS              {0x01};
    static constexpr uint16_t MESSAGE_TMO                {700};   // ms — DPS5005 observed response time ~450ms; 700ms gives comfortable margin
    static constexpr uint8_t  MAX_READS_BEFORE_WRITE     {1};     // reads between write cycles.
    // Delay before the first Modbus message after init(). The ESP32 UART TX pin
    // can glitch during boot, corrupting any partial frame the DPS was processing
    // from a previous session. This delay lets the DPS's frame-timeout expire so
    // it returns to an idle state before we start talking.
    static constexpr uint16_t STARTUP_DELAY_MS           {1000};  // ms — 1s > DPS frame timeout (~100ms at 9600 baud)
    // When output is enabled, U_SET must always exceed the battery terminal voltage or DPS
    // cannot push current into the battery (Iout→0, MPPT sees P=0, system oscillates).
    // This headroom (in 0.01V/bit register units) ensures a minimum margin above Uout.
    static constexpr int VOLTAGE_HEADROOM_BITS           {50};    // 50 × 0.01V/bit = 0.50V above battery voltage
    // OVP ceiling headroom above battery maxVoltage (in 0.01V/bit units).
    // U_SET = (battery maxVoltage_mV + headroom) / 10, acting as a hardware backstop.
    // The software CV controller is the primary voltage limit; this fires only if the
    // software fails. Tight enough to protect the battery, loose enough that normal
    // CV regulation noise does not trigger it.
    static constexpr int      OVP_CEILING_HEADROOM_BITS    {50};    // 50 × 0.01V/bit = 0.50V above battery maxVoltage
    static constexpr uint16_t ERROR_RECOVERY_TMO           {10000}; // ms — pause duration after too many errors
    static constexpr uint8_t  CONSECUTIVE_ERRORS_THRESHOLD {5};     // errors before triggering recovery pause

    // Refresh open-circuit voltage every 30 minutes when measurements are valid, to adapt to changing panel conditions
    static constexpr unsigned long OPEN_CIRCUIT_VOLTAGE_REFRESH_RATE_MS {30 * 60 * 1000};

    static constexpr int LOW_OUTPUT_CURRENT_THRESHOLD_mA     {100};  // Threshold for "hasLowOutputCurrent" condition in OCV refresh logic
    static constexpr int PANEL_PRESENT_MIN_OUTPUT_VOLTAGE_mV {5000}; // Floor for Vout comparison when output is unloaded or at startup
    static constexpr int PANEL_PRESENT_VIN_HEADROOM_mV       {1000}; // Vin must exceed max(Vout, floor) by this margin to consider panel connected

    static constexpr uint16_t SETTLE_DELAY_MS                {500};
};

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
    void setBatteryProfile(const BatteryProfile& profile) override;
    int getMinControl() const override;
    int getMaxControl() const override;
    bool hasMeasurements() const override;
    void enableOutput(bool enable, bool priority = false) override;
    bool isOutputEnabled() const override { return m_outputState == OutputState::ON; }
    void applyControl(int controlValue) override;
    std::optional<int> getOpenCircuitVoltage_mV() const { return m_openCircuitVoltage_mV; }
    int getInVoltage_mV() const {return m_inVoltage_mV;}
    int getVoltage_mV() const {return m_outVoltage_mV;};
    int getCurrent_mA() const {return m_outCurrent_mA;};
    unsigned long getLastUpdateTime() const {return m_lastUpdateTime;};
    bool isMeasurementSettled() const {return m_measurementSettled;}

    private:
    enum class Register : uint16_t
    {
        // DPS5005 Modbus RTU register map
        U_SET   = 0x0000, // Voltage setpoint         (R/W, 0.01V/bit,  e.g. 2400 = 24.00V)
        I_SET   = 0x0001, // Current setpoint         (R/W, 0.001A/bit, e.g. 5000 = 5.000A)
        UOUT    = 0x0002, // Output voltage           (R,   0.01V/bit)
        IOUT    = 0x0003, // Output current           (R,   0.001A/bit)
        POWER   = 0x0004, // Output power             (R,   0.1W/bit)
        UIN     = 0x0005, // Input voltage            (R,   0.01V/bit)
        LOCK    = 0x0006, // Key-lock: 0=off 1=on     (R/W)
        PROTECT = 0x0007, // Protection flags: 0=OK 1=OVP 2=OCP 3=OPP (R)
        CV_CC   = 0x0008, // CV/CC status: 0=CV 1=CC  (R, read-only — NOT the output switch)
        ON_OFF  = 0x0009  // Output enable: 0=off 1=on (R/W)
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

    enum OutputState
    {
        OFF,
        ON
    };

    struct WriteRequest
    {
        Register address;
        uint16_t data;
        bool urgent{false}; // Used to prioritize certain writes (e.g. ON_OFF) over others (e.g. I_SET/U_SET)
    };

    // ===== Configuration =====
    static constexpr uint16_t CRC16_DEFAULT_VALUE    {0xFFFF};
    static constexpr uint16_t CRC16_POLYNOMIAL_VALUE {0xA001};
    static constexpr uint16_t FRAME_SIZE             {8}; // bytes — fixed by Modbus RTU spec
    static constexpr uint8_t  NUM_OF_REGISTERS_TO_READ {8}; // UOUT, IOUT, POWER, UIN, LOCK, PROTECT, CV_CC, ON_OFF

    // ===== State Variables =====
    ControlMode m_controlMode{DPSxDcConverterConfig::CONTROL_MODE};
    int m_inVoltage_mV{};
    int m_outVoltage_mV{};
    int m_outCurrent_mA{};
    int m_setPointValue{};
    std::optional<int> m_openCircuitVoltage_mV{};  // nullopt until first valid Voc capture
    bool m_ocvRefreshPending{false}; // Flag to indicate that an OCV refresh is pending, which temporarily disables output until the refresh is complete. This prevents the DPS from pushing current into the battery during the OCV measurement, which would cause the voltage to drop and yield an invalid reading.
    ModbusState m_messageState{};
    ModbusMessageType m_activeMessageType{};
    Register m_activeWriteRegister{}; 
    uint16_t m_activeWriteData{};
    OutputState m_outputState{OutputState::OFF};
    bool m_startupComplete{}; ///< Blocks Modbus until STARTUP_DELAY_MS has elapsed after init()
    bool m_measurementSettled{true};
    int  m_uSetVoltage_bits{DPSxDcConverterConfig::MAX_MPPT_VOLTAGE_CONTROL_VALUE}; ///< Runtime U_SET target (device max until setBatteryProfile() is called)
    uint8_t m_readsSinceLastWrite{};
    Timer m_messageTimer{};
    Timer m_startupTimer{};
    Timer m_openCircuitVoltageTimer{};
    Timer m_measurementSettlingTimer{};
    std::deque<WriteRequest> m_writeRequestQueue{};

    // ===== Error Recovery Variables =====
    unsigned long m_lastUpdateTime{};
    int m_consecutiveErrors{};
    bool m_pauseRetrying{};
    Timer m_errorRecoveryTimer{};

    /**
     * @brief  Handles the Modbus message state machine, including sending requests, waiting for and processing responses, handling timeouts, 
     *         and error recovery. Called from update() to allow non-blocking operation with timeouts. The state machine prioritizes write 
     *         requests over reads to ensure timely application of control changes, while also enforcing a maximum number of reads between
     *         writes to prevent starvation of control updates. Error handling includes counting consecutive errors and pausing communication
     *         after a threshold is reached to allow transient issues to resolve.
     */
    void handleModbusMessages();

    /**
     * @brief Enqueues a write request to be sent to the Modbus device. Urgent requests are prioritized over regular requests.
     * 
     * @param address The register address to write to.
     * @param data The data to write to the register.
     * @param urgent Whether the request is urgent and should be prioritized.
     */
    void enqueWriteRequest(Register address, uint16_t data, bool urgent = false);

    /**
     * @brief Updates the open-circuit voltage measurement. This should be called periodically to ensure the OCV value is current.
     *        The DPS output is temporarily disabled during the measurement to allow the voltage to stabilize.
     */
    void updateOpenCircuitVoltage();

    /**
     * @brief Synchronous, blocking with timeout is the correct first design...
     *        Message format:
     *        [slave_adr] [0x03] [reg_hi][reg_lo] [count_hi][count_lo] [crc_lo][crc_hi]
     * 
     * @param startAddress The starting register address to read from.
     * @param nrOfRegisters The number of registers to read.
     * @param slaveAddress The address of the slave device.
     * @return std::size_t The number of bytes written.
     */
    std::size_t sendRegisterReadReq(Register startAddress, uint16_t nrOfRegisters, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

    /**
     * @brief Message format:
     *        [slave] [0x03] [byte_count] [data_hi][data_lo] ... repeated [crc_lo][crc_hi]
     * 
     * @param slaveAddress The address of the slave device.
     * @param nrOfRegisters The number of registers to read.
     * @return std::vector<uint16_t> A vector containing the read register values.
     */
    std::vector<uint16_t> receiveRegisterReadRsp(uint16_t nrOfRegisters, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

    /**
     * @brief Synchronous, blocking with timeout is the correct first design...
     *        Message fromat: [slave] [0x06] [reg_hi][reg_lo] [value_hi][value_lo] [crc_lo][crc_hi]
     * 
     * @param address The register address to write to.
     * @param data The data to write to the register.
     * @param slaveAddress The address of the slave device.
     * @return std::size_t The number of bytes written.

     */
    std::size_t sendRegisterWriteReq(Register address, uint16_t data, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

    /**
     * @brief Message format:
     *        [slave] [0x06] [reg_hi][reg_lo] [value_hi][value_lo] [crc_lo][crc_hi]
     * 
     */
    bool receiveRegisterWriteRsp(Register address, uint16_t data, uint8_t slaveAddress = DPSxDcConverterConfig::SLAVE_ADDRESS);

     /**
     * @brief Flushes the UART RX line to clear any stale or corrupted data. Important to call before sending a new request, 
     *        especially after timeouts or errors, to ensure that the next response is properly aligned and parsed. 
     *        This is particularly relevant for the DPS5005, which can return partial or corrupted frames if the ESP32's 
     *        UART glitches during boot or if there are communication issues. By clearing the RX line, we reduce the risk of
     *        misinterpreting stale data as valid responses, which can lead to erroneous behavior in the control loop.
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
     * @brief Selects the appropriate register for the control setpoint based on the configured control mode. 
     *        For VOLTAGE_SETPOINT mode, it returns the U_SET register; for CURRENT_SETPOINT mode, it returns the I_SET register. 
     *        This abstraction allows applyControl() to write to the correct register without needing to check the control mode each time, 
     *        centralizing the logic for register selection in one place.
     * 
     * @param controlMode The current control mode of the converter.
     * @return Register The register corresponding to the current control mode.
     */
    Register selectRegisterType();

    /**
     * @brief Converts a register enum value to its corresponding string representation.
     * 
     * @param reg The register enum value.
     * @return std::string The string representation of the register.
     */
    std::string registerAddressToString(Register reg);
};