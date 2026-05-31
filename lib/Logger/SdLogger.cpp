#include "SdLogger.h"

#ifdef SD_CARD

void SdLogger::init()
{
    if (!SD.begin(m_csPin))
    {
        Serial.printf("[ERROR][SdLogger] SD card initialization failed!\n");
        return;
    }

    auto bootCounter{readAndUpdateNvsBootCounter()};

    setupLogFileName(bootCounter);

    m_logFile = SD.open(m_logFileName.c_str(), FILE_WRITE);
    if (!m_logFile)        
    {
        Serial.printf("[ERROR][SdLogger] Failed to create log file on SD card!\n");
        return;
    }

    Logger::setSdCardLogHandler([this](const std::string& level, const std::string& tag, const std::string& message)
    {
        log(level, tag, message);
    });

    Serial.printf("[INFO][SdLogger] SD card initialized successfully.\n");
}

void SdLogger::log(const std::string& level, const std::string& tag, const std::string& message)
{
    if(!m_logFile)
        return;

    m_logFile.printf("[%s][%s] %s: %s\n", level.c_str(), _LOG_TS(), tag.c_str(), message.c_str());
}

void SdLogger::flush()
{
    if(!m_logFile)
        return; // SD not available — silently skip rather than spamming Serial

    m_logFile.flush();
    m_lastFlushTime = millis();
}

uint32_t SdLogger::readAndUpdateNvsBootCounter()
{
    Preferences preferences{};
    preferences.begin("sdlogger", false);
    uint32_t bootCounter = preferences.getUInt("bootCounter", 0);
    preferences.putUInt("bootCounter", bootCounter + 1);
    preferences.end();
    return bootCounter;
}

void SdLogger::setupLogFileName(uint32_t bootCounter)
{
    if(m_logFileName[0] != '/')
        m_logFileName = "/" + m_logFileName;

    m_logFileName += "_" + std::to_string(bootCounter) + ".txt";
}

#endif // SD_CARD
