#include "Device.h"
#include "ActuatorIf.h"
#include "MeasurementsIf.h"
#include "Utility.h"

class DPSxDcConverter : public Device, public ActuatorIf, public MeasurementsIf
{
    public:
    void init() override;
    void update() override;
    int getInVoltage_mV() const {return m_inVoltage_mV;}
    int getVoltage_mV() const override {return m_outVoltage_mV;};
    int getCurrent_mA() const override {return m_outCurrent_mA;};

    // Apply a control value decided by ChargeController
    void applyControl(int controlValue) override;

    private:
    enum class Register : uint16_t
    {
        // Don't know if the values are correct
        UOUT   = 0x0002,
        IOUT   = 0x0003,
        POWER  = 0x0004,
        UIN    = 0x0005,
        U_SET  = 0x0050,
        I_SET  = 0x0051,
        S_OVP  = 0x0052,
        S_OVC  = 0x0053,
        S_INI  = 0x0057
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

    int m_inVoltage_mV{};
    int m_outVoltage_mV{};
    int m_outCurrent_mA{};
    int m_controlValue{};
    ModbusState m_messageState{};
    ModbusMessageType m_pendigMessageType{};
    ModbusMessageType m_lastMessageType{};
    Timer m_messageTimer{};

    static constexpr uint8_t  SLAVE_ADDRESS {0xFF};
    static constexpr uint16_t CRC16_DEFAULT_VALUE {0xFFFF};
    static constexpr uint16_t CRC16_POLYNOMIAL_VALUE {0xA001};
    static constexpr uint16_t FRAME_SIZE {8}; // bytes
    static constexpr uint16_t MESSAGE_TMO {100}; // milliseconds

    /**
     * @brief 
     * 
     */
    void updateVoltageAndCurrentData();

    /**
     * @brief 
     * 
     */
    void setOutputCurrent();

    /**
     * @brief 
     * 
     * @param address 
     * @param results 
     * @param nrOfRegisters 
     * @param slaveAddress 
     * @return true 
     * @return false 
     */
    bool readRegister(Register address, uint16_t& result, uint8_t slaveAddress = SLAVE_ADDRESS);

    /**
     * @brief 
     * 
     * @param startAddress 
     * @param results 
     * @param nrOfRegisters 
     * @param slaveAddress 
     * @return true 
     * @return false 
     */
    bool readRegisters(Register startAddress, std::vector<uint16_t>& results, uint16_t nrOfRegisters, uint8_t slaveAddress = SLAVE_ADDRESS);

    /**
     * @brief 
     * 
     * @param address 
     * @param data 
     * @param slaveAddress 
     * @return true 
     * @return false 
     */
    bool writeRegister(Register address, uint16_t data, uint8_t slaveAddress = SLAVE_ADDRESS);

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
    std::size_t sendRegisterReadReq(Register startAddress, uint16_t nrOfRegisters, uint8_t slaveAddress = SLAVE_ADDRESS);

    /**
     * @brief Message format:
     *        [slave] [0x03] [byte_count] [data_hi][data_lo] ... repeated [crc_lo][crc_hi]
     * 
     * @param slaveAddress 
     * @param nrOfRegisters 
     * @return std::vector<uint16_t> 
     */
    std::vector<uint16_t> receiveRegisterReadRsp(uint16_t nrOfRegisters, uint8_t slaveAddress = SLAVE_ADDRESS);

    /**
     * @brief Synchronous, blocking with timeout is the correct first design...
     *        Message fromat: [slave] [0x06] [reg_hi][reg_lo] [value_hi][value_lo] [crc_lo][crc_hi]
     * 
     * @param address 
     * @param data 
     * @param slaveAddress 
     * @return std::size_t 
     */
    std::size_t sendRegisterWriteReq(Register address, uint16_t data, uint8_t slaveAddress = SLAVE_ADDRESS);

    /**
     * @brief Message format:
     *        [slave] [0x06] [reg_hi][reg_lo] [value_hi][value_lo] [crc_lo][crc_hi]
     * 
     */
    bool receiveRegisterWriteRsp(Register address, uint16_t data, uint8_t slaveAddress = SLAVE_ADDRESS);

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
    void createFrame(uint8_t* buffer, Register startAddress,  Function readWrite, uint16_t data, uint8_t slaveAddress = SLAVE_ADDRESS);

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
    uint16_t computeCRC16Bytes(uint8_t* buffer, uint16_t size);
};