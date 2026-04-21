#pragma once

/**
 * @file Logger.h
 * @brief Portable logging shim.
 *
 * On ESP32: delegates to ESP-IDF esp_log — runtime per-module level control:
 *   esp_log_level_set("DPSxDcConverter", ESP_LOG_DEBUG)  // one module verbose
 *   esp_log_level_set("*", ESP_LOG_WARN)                 // silence everything
 *
 * On generic Arduino (non-ESP32): maps to Serial.printf, DEBUG suppressed.
 * On native test target: ERROR to stderr, rest suppressed.
 *
 * Usage in every module:
 *   #include "Logger.h"
 *   static const char* TAG = "MyModule";
 *
 *   ESP_LOGI(TAG, "Connected. IP: %s", ip);   // lifecycle events
 *   ESP_LOGW(TAG, "Measurement stale");        // degraded but operational
 *   ESP_LOGE(TAG, "NVS error: %d", err);       // failures
 *   ESP_LOGD(TAG, "Tx: %02X %02X", b0, b1);   // high-frequency detail, stripped in non-debug builds
 */

#if defined(ESP32) || defined(ESP_PLATFORM)
    // ESP32 / ESP-IDF: use native logging — runtime level control available
    #include <esp_log.h>

    // The Arduino-ESP32 precompiled SDK ships with CONFIG_LOG_MAXIMUM_LEVEL=1 (ERROR),
    // which causes the compiler to strip ESP_LOGI/W/D call sites entirely via the
    // outer '#if CONFIG_LOG_MAXIMUM_LEVEL >= N' guard in esp_log.h.
    // Override those macros to call esp_log_write() directly, bypassing the compile-time
    // gate while preserving runtime level filtering set by esp_log_level_set() in setup().
    #ifdef ESP_LOGE
        #undef ESP_LOGE
    #endif
    #ifdef ESP_LOGI
        #undef ESP_LOGI
    #endif
    #ifdef ESP_LOGW
        #undef ESP_LOGW
    #endif
    #ifdef ESP_LOGD
        #undef ESP_LOGD
    #endif

    // Format raw milliseconds as HH:MM:SS.mmm for readability
    inline const char* _log_fmt_time(char* buf, unsigned long ms) 
    {
        unsigned long s   = ms / 1000;
        unsigned long m   = s  / 60;
        unsigned long h   = m  / 60;
        snprintf(buf, 16, "%02lu:%02lu:%02lu.%03lu", h, m % 60, s % 60, ms % 1000);
        return buf;
    }
    #define _LOG_TS() ([]() -> const char* { static char _b[16]; return _log_fmt_time(_b, (unsigned long)esp_log_timestamp()); }())

    #define ESP_LOGE(tag, fmt, ...) esp_log_write(ESP_LOG_ERROR, tag, "[ERROR] [%s] %s: " fmt "\n", _LOG_TS(), tag, ##__VA_ARGS__)
    #define ESP_LOGI(tag, fmt, ...) esp_log_write(ESP_LOG_INFO,  tag, "[INFO]  [%s] %s: " fmt "\n", _LOG_TS(), tag, ##__VA_ARGS__)
    #define ESP_LOGW(tag, fmt, ...) esp_log_write(ESP_LOG_WARN,  tag, "[WARN]  [%s] %s: " fmt "\n", _LOG_TS(), tag, ##__VA_ARGS__)
    #define ESP_LOGD(tag, fmt, ...) esp_log_write(ESP_LOG_DEBUG, tag, "[DEBUG] [%s] %s: " fmt "\n", _LOG_TS(), tag, ##__VA_ARGS__)

#elif defined(ARDUINO)
    // Generic Arduino: Serial output, DEBUG suppressed
    #include <Arduino.h>
    #define ESP_LOGE(tag, fmt, ...) do { Serial.printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
    #define ESP_LOGW(tag, fmt, ...) do { Serial.printf("[WARNING][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
    #define ESP_LOGI(tag, fmt, ...) do { Serial.printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
    #define ESP_LOGD(tag, fmt, ...) do {} while(0)

#else
    // Native test target: errors to stderr, rest suppressed
    #include <cstdio>
    #define ESP_LOGE(tag, fmt, ...) do { fprintf(stderr, "[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
    #define ESP_LOGW(tag, fmt, ...) do {} while(0)
    #define ESP_LOGI(tag, fmt, ...) do {} while(0)
    #define ESP_LOGD(tag, fmt, ...) do {} while(0)

#endif
