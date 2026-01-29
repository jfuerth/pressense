#pragma once

#ifdef PLATFORM_ESP32
    #include <esp_log.h>
    #define LOG_TAG "PRESSENCE"
    
    #define logInfo(fmt, ...) ESP_LOGI(LOG_TAG, fmt, ##__VA_ARGS__)
    #define logError(fmt, ...) ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
    #define logDebug(fmt, ...) ESP_LOGD(LOG_TAG, fmt, ##__VA_ARGS__)
    #define logWarn(fmt, ...) ESP_LOGW(LOG_TAG, fmt, ##__VA_ARGS__)
#else
    #include <cstdio>
    
    #define logInfo(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
    #define logError(fmt, ...) fprintf(stderr, "ERROR: " fmt "\n", ##__VA_ARGS__)
    #define logDebug(fmt, ...) printf("DEBUG: " fmt "\n", ##__VA_ARGS__)
    #define logWarn(fmt, ...) printf("WARN: " fmt "\n", ##__VA_ARGS__)
#endif
