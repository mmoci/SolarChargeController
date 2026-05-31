#pragma once

#include <string>

#ifdef SD_CARD

#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include "Logger.h"

class SdLogger
{
    public:
    static constexpr unsigned long FLUSH_INTERVAL_MS = 500;

    SdLogger(uint8_t csPin, const std::string& logFileName = "/log") : m_csPin{csPin}, m_logFileName{logFileName}
    {}

    void init();
    void flush();
    unsigned long getLastFlushTime() const { return m_lastFlushTime; }

    private:
    static constexpr char TAG[] = "SdLogger";

    uint32_t readAndUpdateNvsBootCounter();
    void setupLogFileName(uint32_t bootCounter);
    void log(const std::string& level, const std::string& tag, const std::string& message);

    uint8_t m_csPin{};
    File m_logFile{};
    std::string m_logFileName{};
    unsigned long m_lastFlushTime{0};
};

#else

// Stub: SD_CARD not defined — all calls compile away to nothing
class SdLogger
{
    public:
    static constexpr unsigned long FLUSH_INTERVAL_MS = 500;

    SdLogger(uint8_t, const std::string& = {}) {}

    void init() {}
    void flush() {}
    unsigned long getLastFlushTime() const { return 0; }
};

#endif