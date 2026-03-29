#pragma once

// Mock ESP-IDF NVS API for native (desktop) unit tests.
// All operations are backed by in-memory maps; no flash is touched.

#include <cstdint>
#include <string>
#include <unordered_map>

typedef uint32_t  nvs_handle_t;
typedef int32_t   esp_err_t;
typedef int       nvs_open_mode_t;

constexpr esp_err_t ESP_OK                        =  0;
constexpr esp_err_t ESP_FAIL                      = -1;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND         = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_NO_FREE_PAGES     = 0x1105;
constexpr esp_err_t ESP_ERR_NVS_NEW_VERSION_FOUND = 0x1106;

constexpr nvs_open_mode_t NVS_READONLY  = 0;
constexpr nvs_open_mode_t NVS_READWRITE = 1;

// In-memory key-value stores — reset between tests via MockNvs::reset()
namespace MockNvs
{
    extern std::unordered_map<std::string, uint8_t>  u8_store;
    extern std::unordered_map<std::string, int32_t>  i32_store;

    void reset();
}

inline esp_err_t nvs_flash_init()  { return ESP_OK; }
inline esp_err_t nvs_flash_erase() { return ESP_OK; }

inline esp_err_t nvs_open(const char* /*ns*/, nvs_open_mode_t /*mode*/, nvs_handle_t* out_handle)
{
    *out_handle = 1;
    return ESP_OK;
}

inline esp_err_t nvs_close(nvs_handle_t)  { return ESP_OK; }
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }

inline esp_err_t nvs_erase_key(nvs_handle_t, const char* key)
{
    MockNvs::u8_store.erase(key);
    MockNvs::i32_store.erase(key);
    return ESP_OK;
}

// nvs_get_blob returns NOT_FOUND so the legacy-blob migration path in
// BatteryProfileSelector::init() is never triggered.
inline esp_err_t nvs_get_blob(nvs_handle_t, const char* /*key*/, void* /*out*/, size_t* /*size*/)
{
    return ESP_ERR_NVS_NOT_FOUND;
}

inline esp_err_t nvs_set_u8(nvs_handle_t, const char* key, uint8_t val)
{
    MockNvs::u8_store[key] = val;
    return ESP_OK;
}

inline esp_err_t nvs_get_u8(nvs_handle_t, const char* key, uint8_t* out)
{
    auto it = MockNvs::u8_store.find(key);
    if (it == MockNvs::u8_store.end()) return ESP_ERR_NVS_NOT_FOUND;
    *out = it->second;
    return ESP_OK;
}

inline esp_err_t nvs_set_i32(nvs_handle_t, const char* key, int32_t val)
{
    MockNvs::i32_store[key] = val;
    return ESP_OK;
}

inline esp_err_t nvs_get_i32(nvs_handle_t, const char* key, int32_t* out)
{
    auto it = MockNvs::i32_store.find(key);
    if (it == MockNvs::i32_store.end()) return ESP_ERR_NVS_NOT_FOUND;
    *out = it->second;
    return ESP_OK;
}
