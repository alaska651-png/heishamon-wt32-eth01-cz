#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ESPHome ESP-IDF installations can have NVS but no filesystem partition.
// OTA preserves that partition table: use NVS instead of assuming min_spiffs.
bool initStorage();
const char *storageName();
bool readStoredJson(const char *key, DynamicJsonDocument &document);
bool writeStoredJson(const char *key, const DynamicJsonDocument &document);
bool readStoredBytes(const char *key, uint8_t *bytes, size_t size);
bool writeStoredBytes(const char *key, const uint8_t *bytes, size_t size);
void resetStorage();
