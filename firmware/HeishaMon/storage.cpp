#include "storage.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include <cstring>

namespace {
bool initialized = false;
bool filesystem = false;
constexpr char StorageNamespace[] = "heisha_v2cz";
const char *pathFor(const char *key) {
  if (!strcmp(key,"config")) return "/config.json";
  if (!strcmp(key,"heatcurve")) return "/heatcurve.json";
  return "/optionalpcb.raw";
}
}
bool initStorage() {
  if (initialized) return true;
  if (esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs"))
    filesystem = LittleFS.begin(true);
  if (filesystem) { initialized = true; return true; }
  Preferences preferences;
  if (!preferences.begin(StorageNamespace,false)) return false;
  preferences.end();
  initialized = true;
  return true;
}
const char *storageName() { initStorage(); return filesystem ? "LittleFS" : "NVS"; }
bool readStoredJson(const char *key, DynamicJsonDocument &document) {
  if (!initStorage()) return false;
  if (filesystem) {
    File file = LittleFS.open(pathFor(key),"r");
    if (!file) return false;
    const bool ok = !deserializeJson(document,file);
    file.close();
    return ok;
  }
  Preferences preferences;
  if (!preferences.begin(StorageNamespace,true)) return false;
  const String json = preferences.getString(key,"");
  preferences.end();
  return json.length() && !deserializeJson(document,json);
}
bool writeStoredJson(const char *key, const DynamicJsonDocument &document) {
  if (!initStorage() || document.overflowed()) return false;
  if (filesystem) {
    File file = LittleFS.open(pathFor(key),"w");
    if (!file) return false;
    const bool ok = serializeJson(document,file) > 0;
    file.close();
    return ok;
  }
  String json;
  serializeJson(document,json);
  Preferences preferences;
  if (!preferences.begin(StorageNamespace,false)) return false;
  const bool ok = preferences.putString(key,json) == json.length();
  preferences.end();
  return ok;
}
bool readStoredBytes(const char *key, uint8_t *bytes, size_t size) {
  if (!initStorage()) return false;
  if (filesystem) {
    File file = LittleFS.open(pathFor(key),"r");
    if (!file || file.size() != size) return false;
    return file.read(bytes,size) == size;
  }
  Preferences preferences;
  if (!preferences.begin(StorageNamespace,true)) return false;
  const bool ok = preferences.getBytesLength(key) == size && preferences.getBytes(key,bytes,size) == size;
  preferences.end();
  return ok;
}
bool writeStoredBytes(const char *key, const uint8_t *bytes, size_t size) {
  if (!initStorage()) return false;
  if (filesystem) {
    File file = LittleFS.open(pathFor(key),"w");
    return file && file.write(bytes,size) == size;
  }
  Preferences preferences;
  if (!preferences.begin(StorageNamespace,false)) return false;
  const bool ok = preferences.putBytes(key,bytes,size) == size;
  preferences.end();
  return ok;
}
void resetStorage() {
  initStorage();
  if (filesystem) LittleFS.format();
  Preferences preferences;
  if (preferences.begin(StorageNamespace,false)) { preferences.clear(); preferences.end(); }
}
