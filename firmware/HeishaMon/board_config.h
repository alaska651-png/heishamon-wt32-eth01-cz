#pragma once
#include <Arduino.h>
#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "This HeishaMon v2.0 port requires the original ESP32 WT32-ETH01 (not S3/C3/EVO)"
#endif
static_assert(char(0xC8) > 0, "Build with -funsigned-char: the v2.0 decoder requires unsigned packet bytes");
#include <WiFi.h>
#include <ETH.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <LittleFS.h>
#define heatpumpSerial Serial1
#define loggingSerial Serial
#define proxySerial Serial2

constexpr uint8_t HEATPUMP_RX = 35;
constexpr uint8_t HEATPUMP_TX = 17;
constexpr uint8_t TX_ENABLE = 32;
constexpr uint8_t CONFIG_RESET = 33;
constexpr uint8_t CZ_RX = 36;
constexpr uint8_t CZ_TX = 14;

constexpr bool pinsAreDistinct() {
  constexpr uint8_t pins[] = {35,17,32,33,36,14,4,5,15,39,1,3,0,16,18,19,21,22,23,25,26,27};
  for (size_t i = 0; i < sizeof(pins); ++i)
    for (size_t j = i + 1; j < sizeof(pins); ++j)
      if (pins[i] == pins[j]) return false;
  return true;
}
static_assert(pinsAreDistinct(), "UART, Ethernet and optional pins must not overlap");

inline bool ethernetConnected() {
  return ETH.linkUp() && ETH.hasIP();
}

inline bool networkConnected() {
  return ethernetConnected() || (WiFi.isConnected() && WiFi.localIP() != IPAddress());
}

inline IPAddress networkIP() {
  return ethernetConnected() ? ETH.localIP() : WiFi.localIP();
}

// No VCC ADC exists on this board; do not report a fabricated voltage.
inline const char *supplyVoltageJson() { return "null"; }
