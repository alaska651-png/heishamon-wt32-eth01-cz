#include "board_config.h"
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <DNSServer.h>

#include <ArduinoJson.h>

#include "webfunctions.h"
#include "decode.h"
#include "commands.h"
#include "version.h"
#include "cz_protocol.h"
#include "storage.h"
#include <esp_ota_ops.h>

DNSServer dnsServer;

const byte DNS_PORT = 53;

#define SERIALTIMEOUT 2000 // wait until all 203 bytes are read, must not be too long to avoid blocking the code

WebServer httpServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);
HTTPUpdateServer httpUpdater;

settingsStruct heishamonSettings;

bool sending = false; // mutex for sending data
bool mqttcallbackinprogress = false; // mutex for processing mqtt callback
bool firmwareUpdating = false;
bool inFlightProxyReply = false;
cz::Framer heatpumpFramer(false);
cz::Framer czFramer;
std::array<uint8_t,203> cachedBasic{};
bool cachedBasicValid = false;
uint32_t cachedBasicAt = 0;
unsigned long czFrames = 0;
unsigned long czErrors = 0;
unsigned long czCacheReplies = 0;
unsigned long czForwarded = 0;

#define MQTTRECONNECTTIMER 30000 //it takes 30 secs for each mqtt server reconnect attempt
unsigned long lastMqttReconnectAttempt = 0;

#define WIFIRETRYTIMER 10000 // switch between hotspot and configured SSID each 10 secs if SSID is lost
unsigned long lastWifiRetryTimer = 0;

unsigned long lastRunTime = 0;

unsigned long sendCommandReadTime = 0; //set to millis value during send, allow to wait millis for answer
unsigned long goodreads = 0;
unsigned long totalreads = 0;
unsigned long badcrcread = 0;
unsigned long badheaderread = 0;
unsigned long tooshortread = 0;
unsigned long toolongread = 0;
unsigned long timeoutread = 0;
float readpercentage = 0;

// instead of passing array pointers between functions we just define this in the global scope
#define MAXDATASIZE 255
char data[MAXDATASIZE];
byte  data_length = 0;

// store actual data in an String array
String actData[NUMBER_OF_TOPICS];
String actOptData[NUMBER_OF_OPT_TOPICS];

// log message to sprintf to
char log_msg[256];

// mqtt topic to sprintf and then publish to
char mqtt_topic[256];

static int mqttReconnects = 0;

// can't have too much in buffer due to memory shortage
#define MAXCOMMANDSINBUFFER 10

// buffer for commands to send
struct cmdbuffer_t {
  uint8_t length;
  byte data[128];
  bool proxyReply = false;
} cmdbuffer[MAXCOMMANDSINBUFFER];

static uint8_t cmdstart = 0;
static uint8_t cmdend = 0;
static uint8_t cmdnrel = 0;

// mqtt
WiFiClient mqtt_wifi_client;
PubSubClient mqtt_client(mqtt_wifi_client);

bool firstConnectSinceBoot = true; //if this is true there is no first connection made yet

/*
 *  check_wifi will process wifi reconnecting managing
 */
void check_wifi()
{
  static IPAddress previousIP;
  const int scanState = WiFi.scanComplete();
  if (scanState >= 0) getWifiScanResults(scanState);

  if (!networkConnected()) {
    dnsServer.processNextRequest();
    if ((unsigned long)(millis() - lastWifiRetryTimer) > WIFIRETRYTIMER) {
      lastWifiRetryTimer = millis();
      if (WiFi.softAPSSID() == "") {
        log_message((char *)"No Ethernet/WiFi address, starting setup hotspot...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP("HeishaMon-Setup");
      }
      if (heishamonSettings.wifi_ssid[0] && WiFi.softAPgetStationNum() == 0 && scanState != WIFI_SCAN_RUNNING) {
        WiFi.disconnect();
        if (heishamonSettings.wifi_password[0] == '\0') {
          WiFi.begin(heishamonSettings.wifi_ssid);
        } else {
          WiFi.begin(heishamonSettings.wifi_ssid, heishamonSettings.wifi_password);
        }
      }
    }
    return;
  }

  // Prefer wired routing; the client used by MQTT works on either interface.
  if (ethernetConnected()) {
    if (!ETH.isDefault()) ETH.setDefault();
  } else if (!WiFi.STA.isDefault()) {
    WiFi.STA.setDefault();
  }
  if (networkIP() != previousIP) {
    previousIP = networkIP();
    mqtt_client.disconnect();
    lastMqttReconnectAttempt = 0;
    Serial.print(ethernetConnected() ? "Ethernet IP: " : "WiFi IP: ");
    Serial.println(previousIP);
  }
  if (WiFi.softAPSSID() != "") WiFi.softAPdisconnect(true);
  if (firstConnectSinceBoot) {
    firstConnectSinceBoot = false;
    setupOTA();
    MDNS.addService("http", "tcp", 80);
  }
  lastWifiRetryTimer = millis();
}

void mqtt_reconnect()
{
  unsigned long now = millis();
  if (!networkConnected() || !heishamonSettings.mqtt_server[0]) return;
  if (lastMqttReconnectAttempt == 0 || (unsigned long)(now - lastMqttReconnectAttempt) > MQTTRECONNECTTIMER) {
    lastMqttReconnectAttempt = now;
    log_message((char*)"Reconnecting to mqtt server ...");
    char topic[256];
    sprintf(topic, "%s/%s", heishamonSettings.mqtt_topic_base, mqtt_willtopic);
    if (mqtt_client.connect(heishamonSettings.wifi_hostname, heishamonSettings.mqtt_username, heishamonSettings.mqtt_password, topic, 1, true, "Offline"))
    {
      mqttReconnects++;

      sprintf(topic, "%s/%s/#", heishamonSettings.mqtt_topic_base, mqtt_topic_commands);
      mqtt_client.subscribe(topic);
      sprintf(topic, "%s/%s", heishamonSettings.mqtt_topic_base, mqtt_send_raw_value_topic);
      mqtt_client.subscribe(topic);
      sprintf(topic, "%s/%s", heishamonSettings.mqtt_topic_base, mqtt_willtopic);
      mqtt_client.publish(topic, "Online");
      sprintf(topic, "%s/%s", heishamonSettings.mqtt_topic_base, mqtt_iptopic);
      mqtt_client.publish(topic, networkIP().toString().c_str(), true);

      if (heishamonSettings.use_s0) { // connect to s0 topic to retrieve older watttotal from mqtt
        sprintf_P(mqtt_topic, PSTR("%s/%s/WatthourTotal/1"), heishamonSettings.mqtt_topic_base, mqtt_topic_s0);
        mqtt_client.subscribe(mqtt_topic);
        sprintf_P(mqtt_topic, PSTR("%s/%s/WatthourTotal/2"), heishamonSettings.mqtt_topic_base, mqtt_topic_s0);
        mqtt_client.subscribe(mqtt_topic);
      }
    }



  }
}

void log_message(char* string)
{
  if (heishamonSettings.logSerial1) {
    loggingSerial.print(millis());
    loggingSerial.print(": ");
    loggingSerial.println(string);
  }
  if (heishamonSettings.logMqtt && mqtt_client.connected())
  {
    char log_topic[256];
    sprintf(log_topic, "%s/%s", heishamonSettings.mqtt_topic_base, mqtt_logtopic);

    if (!mqtt_client.publish(log_topic, string)) {
      loggingSerial.print(millis());
      loggingSerial.print(F(": "));
      loggingSerial.println(F("MQTT publish log message failed!"));
      mqtt_client.disconnect();
    }
  }
  if (webSocket.connectedClients() > 0) {
    webSocket.broadcastTXT(string, strlen(string));
  }
}

void logHex(char *hex, byte hex_len) {
#define LOGHEXBYTESPERLINE 32  // please be aware of max mqtt message size
  for (int i = 0; i < hex_len; i += LOGHEXBYTESPERLINE) {
    char buffer [(LOGHEXBYTESPERLINE * 3) + 1];
    buffer[LOGHEXBYTESPERLINE * 3] = '\0';
    for (int j = 0; ((j < LOGHEXBYTESPERLINE) && ((i + j) < hex_len)); j++) {
      sprintf(&buffer[3 * j], "%02X ", hex[i + j]);
    }
    sprintf(log_msg, "data: %s", buffer ); log_message(log_msg);
  }
}

byte calcChecksum(byte* command, int length) {
  byte chk = 0;
  for ( int i = 0; i < length; i++)  {
    chk += command[i];
  }
  chk = (chk ^ 0xFF) + 01;
  return chk;
}

bool isValidReceiveChecksum() {
  byte chk = 0;
  for ( int i = 0; i < data_length; i++)  {
    chk += data[i];
  }
  return (chk == 0); //all received bytes + checksum should result in 0
}

bool readSerial() {
  while (heatpumpSerial.available()) {
    if (!heatpumpFramer.used) totalreads++;
    const auto result = heatpumpFramer.push(uint8_t(heatpumpSerial.read()),millis());
    data_length = uint8_t(heatpumpFramer.used);
    if (result == cz::Feed::BadChecksum) { badcrcread++; continue; }
    if (result == cz::Feed::BadLength) { toolongread++; continue; }
    if (result == cz::Feed::BadHeader) { badheaderread++; continue; }
    if (result != cz::Feed::Complete) continue;
    const size_t size = heatpumpFramer.used;
    memcpy(data,heatpumpFramer.bytes.data(),size);
    const bool basic = cz::basicData(heatpumpFramer.bytes.data(),size);
    const bool forwardReply = inFlightProxyReply;
    sending = inFlightProxyReply = false;
    goodreads++;
    if (heishamonSettings.proxy && !heishamonSettings.listenonly && !firmwareUpdating &&
        (forwardReply || (!basic && size != 20)))
      proxySerial.write(reinterpret_cast<uint8_t *>(data),size);
    bool decoded = false;
    if (basic) {
      memcpy(cachedBasic.data(),data,size);
      cachedBasicValid = true;
      cachedBasicAt = millis();
      decode_heatpump_data(data,actData,mqtt_client,log_message,heishamonSettings.mqtt_topic_base,heishamonSettings.updateAllTime);
      decoded = true;
    } else if (size == 20) {
      decode_optional_heatpump_data(data,actOptData,mqtt_client,log_message,heishamonSettings.mqtt_topic_base,heishamonSettings.updateAllTime);
      decoded = true;
    }
    heatpumpFramer.reset();
    data_length = 0;
    return decoded;
  }
  return false;
}

void readProxy() {
  if (cachedBasicValid && !cz::freshCache(true,millis(),cachedBasicAt)) cachedBasicValid = false;
  while (proxySerial.available()) {
    const auto result = czFramer.push(uint8_t(proxySerial.read()),millis());
    if (result != cz::Feed::Waiting && result != cz::Feed::Complete) { czErrors++; continue; }
    if (result != cz::Feed::Complete) continue;
    czFrames++;
    const bool fresh = cz::freshCache(cachedBasicValid,millis(),cachedBasicAt);
    const auto action = cz::requestAction(czFramer.bytes.data(),czFramer.used,
      heishamonSettings.proxy && !firmwareUpdating,heishamonSettings.listenonly,fresh);
    if (action == cz::Action::Forward || action == cz::Action::ForwardAndReplyCache) {
      sendCommandRouted(czFramer.bytes.data(),int(czFramer.used)-1,action == cz::Action::Forward);
      czForwarded++;
    }
    if (action == cz::Action::ReplyCache || action == cz::Action::ForwardAndReplyCache) {
      proxySerial.write(cachedBasic.data(),cachedBasic.size());
      czCacheReplies++;
    }
    czFramer.reset();
  }
}
void popCommandBuffer() {
  // to make sure we can pop a command from the buffer
  if ((!sending) && cmdnrel > 0) {
    sendCommandRouted(cmdbuffer[cmdstart].data, cmdbuffer[cmdstart].length, cmdbuffer[cmdstart].proxyReply);
    cmdstart = (cmdstart + 1) % (MAXCOMMANDSINBUFFER);
    cmdnrel--;
  }
}

void pushCommandBuffer(byte* command, int length, bool proxyReply) {
  if (length <= 0 || length > (int)sizeof(cmdbuffer[0].data)) return;
  if (cmdnrel + 1 > MAXCOMMANDSINBUFFER) {
    log_message((char *)"Too much commands already in buffer. Ignoring this commands.\n");
    return;
  }
  cmdbuffer[cmdend].length = length;
  cmdbuffer[cmdend].proxyReply = proxyReply;
  memcpy(&cmdbuffer[cmdend].data, command, length);
  cmdend = (cmdend + 1) % (MAXCOMMANDSINBUFFER);
  cmdnrel++;
}

bool send_command(byte* command, int length) {
  return sendCommandRouted(command,length,false);
}

bool sendCommandRouted(byte* command, int length, bool proxyReply) {
  if (!command || length <= 0 || length > (int)sizeof(cmdbuffer[0].data)) return false;
  if (heishamonSettings.listenonly || firmwareUpdating) {
    log_message((char*)"Not sending this command. Heishamon in listen only mode!");
    return false;
  }
  if ( sending ) {
    log_message((char*)"Already sending data. Buffering this send request");
    pushCommandBuffer(command, length, proxyReply);
    return false;
  }
  sending = true; //simple semaphore to only allow one send command at a time, semaphore ends when answered data is received
  inFlightProxyReply = proxyReply;

  byte chk = calcChecksum(command, length);
  int bytesSent = heatpumpSerial.write(command, length); //first send command
  bytesSent += heatpumpSerial.write(chk); //then calculcated checksum byte afterwards
  sprintf_P(log_msg, PSTR("sent bytes: %d including checksum value: %d "), bytesSent, int(chk));
  log_message(log_msg);

  if (heishamonSettings.logHexdump) logHex((char*)command, length);
  sendCommandReadTime = millis(); //set sendCommandReadTime when to timeout the answer of this command
  return true;
}

// Callback function that is called when a message has been pushed to one of your topics.
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  if (mqttcallbackinprogress) {
    log_message((char*)"Already processing another mqtt callback. Ignoring this one");
  }
  else {
    mqttcallbackinprogress = true; //simple semaphore to make sure we don't have two callbacks at the same time
    char msg[length + 1];
    for (unsigned int i = 0; i < length; i++) {
      msg[i] = (char)payload[i];
    }
    msg[length] = '\0';
    const size_t baseLength = strlen(heishamonSettings.mqtt_topic_base);
    if (strlen(topic) <= baseLength || strncmp(topic, heishamonSettings.mqtt_topic_base, baseLength) || topic[baseLength] != '/') {
      mqttcallbackinprogress = false;
      return;
    }
    char* topic_command = topic + baseLength + 1;
    if (strcmp(topic_command, mqtt_send_raw_value_topic) == 0)
    { // send a raw hex string
      sprintf_P(log_msg, PSTR("sending raw value"));
      log_message(log_msg);
      send_command(reinterpret_cast<byte *>(msg), length);
    } else if (strncmp(topic_command, "s0/WatthourTotal/", 17) == 0)
    {
      char* topic_s0_watthour_port = topic_command + 17; //strip the first 17 "s0/WatthourTotal/" from the topic to get the s0 port
      int s0Port = String(topic_s0_watthour_port).toInt();
      float watthour = String(msg).toFloat();
      restore_s0_Watthour(s0Port, watthour);
      //unsubscribe after restoring the watthour values
      char mqtt_topic[256];
      sprintf(mqtt_topic, "%s", topic);
      if (mqtt_client.unsubscribe(mqtt_topic)) {
        log_message((char*)"Unsubscribed from S0 watthour restore topic");
      }
    } else if (strncmp(topic_command, mqtt_topic_commands, 8) == 0)  // check for optional pcb commands
    {
      char* topic_sendcommand = topic_command + 9; //strip the first 9 "commands/" from the topic to get what we need
      send_heatpump_command(topic_sendcommand, msg, send_command, log_message, heishamonSettings.optionalPCB);
    }
    mqttcallbackinprogress = false;
  }
}

void setupOTA() {
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(heishamonSettings.wifi_hostname);

  // Set authentication
  ArduinoOTA.setPassword(heishamonSettings.ota_password);

  ArduinoOTA.onStart([]() {
    firmwareUpdating = true;
    digitalWrite(TX_ENABLE, LOW);
  });
  ArduinoOTA.onEnd([]() {
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {

  });
  ArduinoOTA.onError([](ota_error_t error) {
    firmwareUpdating = false;
    digitalWrite(TX_ENABLE, heishamonSettings.listenonly ? LOW : HIGH);
  });
  ArduinoOTA.begin();
}

void setupHttp() {
  httpUpdater.setup(&httpServer, heishamonSettings.update_path, heishamonSettings.update_username, heishamonSettings.ota_password);
  httpServer.on("/", [] {
    handleRoot(&httpServer, readpercentage, mqttReconnects, &heishamonSettings);
  });
  httpServer.on("/command", [] {
    handleREST(&httpServer, heishamonSettings.optionalPCB);
  });
  httpServer.on("/tablerefresh", [] {
    handleTableRefresh(&httpServer, actData);
  });
  httpServer.on("/json", [] {
    handleJsonOutput(&httpServer, actData);
  });
  httpServer.on("/status", [] {
    DynamicJsonDocument status(4096);
    status["firmware"] = heishamon_version;
    status["network"] = ethernetConnected() ? "ethernet" : "wifi";
    status["ip"] = networkIP().toString();
    status["mac"] = ethernetConnected() ? ETH.macAddress() : WiFi.macAddress();
    status["storage"] = storageName();
    status["listen_only"] = heishamonSettings.listenonly;
    status["tx_enabled"] = digitalRead(TX_ENABLE) == HIGH;
    status["proxy_configured"] = heishamonSettings.proxy;
    status["proxy_active"] = heishamonSettings.proxy && !heishamonSettings.listenonly && !firmwareUpdating;
    status["poll_seconds"] = heishamonSettings.waitTime;
    status["pdc_rx"] = HEATPUMP_RX; status["pdc_tx"] = HEATPUMP_TX;
    status["cz_rx"] = CZ_RX; status["cz_tx"] = CZ_TX;
    status["pdc_uart_ready"] = bool(heatpumpSerial);
    status["cz_uart_ready"] = bool(proxySerial);
    status["pdc_baud"] = heatpumpSerial.baudRate();
    status["cz_baud"] = proxySerial.baudRate();
    status["cz_frames"] = czFrames; status["cz_errors"] = czErrors;
    status["cz_cache_replies"] = czCacheReplies; status["cz_forwarded"] = czForwarded;
    status["pdc_good_reads"] = goodreads;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) status["running_partition"] = running->label;
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (next) status["ota_slot_bytes"] = next->size;
    JsonArray partitions = status.createNestedArray("partitions");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,nullptr);
    while (it) {
      const esp_partition_t *part = esp_partition_get(it);
      JsonObject row = partitions.createNestedObject();
      row["name"] = part->label; row["offset"] = part->address; row["bytes"] = part->size;
      it = esp_partition_next(it);
    }
    String output;
    serializeJson(status,output);
    httpServer.send(200,"application/json",output);
  });
  httpServer.on("/factoryreset", [] {
    handleFactoryReset(&httpServer);
  });
  httpServer.on("/reboot", [] {
    handleReboot(&httpServer);
  });
  httpServer.on("/debug", [] {
    handleDebug(&httpServer, data, 203);
  });
  httpServer.on("/settings", [] {
    if (handleSettings(&httpServer, &heishamonSettings)) {
      mqtt_client.disconnect();
      setupMqtt();
      lastMqttReconnectAttempt = 0;
      ArduinoOTA.setPassword(heishamonSettings.ota_password);
      httpUpdater.updateCredentials(heishamonSettings.update_username, heishamonSettings.ota_password);
      setupConditionals();
    }
  });
  httpServer.on("/wifiscan", [] {
    handleWifiScan(&httpServer);
  });

  httpServer.on("/smartcontrol", [] {
    handleSmartcontrol(&httpServer, &heishamonSettings, actData);
  });
  httpServer.on("/togglelog", [] {
    log_message((char*)"Toggled mqtt log flag");
    heishamonSettings.logMqtt ^= true;
    httpServer.sendHeader("Location", String("/"), true);
    httpServer.send ( 302, "text/plain", "");
    httpServer.client().stop();
  });
  httpServer.on("/togglehexdump", [] {
    log_message((char*)"Toggled hexdump log flag");
    heishamonSettings.logHexdump ^= true;
    httpServer.sendHeader("Location", String("/"), true);
    httpServer.send ( 302, "text/plain", "");
    httpServer.client().stop();
  });
  httpServer.onNotFound([]() {
    httpServer.sendHeader("Location", String("/"), true);
    httpServer.send(302, "text/plain", "");
    httpServer.client().stop();
  });
  /*
     Captive portal url's
     for now, the android one sometimes gets the heishamon in a wait loop during wifi reconfig

    httpServer.on("/generate_204", [] {
    handleSettings(&httpServer, &heishamonSettings);
    });  */
  httpServer.on("/hotspot-detect.html", [] {
    handleSettings(&httpServer, &heishamonSettings);
  });
  httpServer.on("/fwlink", [] {
    handleSettings(&httpServer, &heishamonSettings);
  });
  httpServer.on("/popup", [] {
    handleSettings(&httpServer, &heishamonSettings);
  });

  httpServer.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  webSocket.enableHeartbeat(3000, 3000, 2);
}

void serviceResetButton() {
  static unsigned long releasedAt = 0;
  if (digitalRead(CONFIG_RESET) != LOW) {
    releasedAt = millis();
  } else if ((unsigned long)(millis() - releasedAt) > 10000) {
    digitalWrite(TX_ENABLE, LOW);
    Serial.println("GPIO33 held for 10 seconds: clearing configuration");
    resetStorage();
    WiFi.disconnect(true, true);
    delay(100);
    ESP.restart();
  }
}

void setupSerial() {
  //boot issue's first on normal serial
  Serial.begin(115200);
  Serial.flush();
}

void setupSerial1() {
  if (heishamonSettings.logSerial1) Serial.println(F("Debugging on UART0 TX0/GPIO1"));
}

void switchSerial() {
  heatpumpSerial.setRxBufferSize(1024);
  heatpumpSerial.begin(9600, SERIAL_8E1, HEATPUMP_RX, HEATPUMP_TX);
  proxySerial.setRxBufferSize(1024);
  proxySerial.begin(9600,SERIAL_8E1,CZ_RX,CZ_TX);
  setupGPIO(heishamonSettings.gpioSettings);
  digitalWrite(TX_ENABLE, heishamonSettings.listenonly ? LOW : HIGH);
}

void setupMqtt() {
  mqtt_client.setBufferSize(1024);
  mqtt_client.setSocketTimeout(10); mqtt_client.setKeepAlive(5); //fast timeout, any slower will block the main loop too long
  mqtt_client.setServer(heishamonSettings.mqtt_server, atoi(heishamonSettings.mqtt_port));
  mqtt_client.setCallback(mqtt_callback);
}

void setupConditionals() {
  digitalWrite(TX_ENABLE, heishamonSettings.listenonly ? LOW : HIGH);
  if (heishamonSettings.listenonly) {
    cmdnrel = cmdstart = cmdend = 0;
    sending = inFlightProxyReply = false;
    heatpumpFramer.reset();
    czFramer.reset();
  }
  //load optional PCB data from flash
  if (heishamonSettings.optionalPCB) {
    if (loadOptionalPCB(optionalPCBQuery, OPTIONALPCBQUERYSIZE)) {
      log_message((char*)"Succesfully loaded optional PCB data from saved flash!");
    }
    else {
      log_message((char*)"Failed to load optional PCB data from flash!");
    }
    delay(1500); //need 1.5 sec delay before sending first datagram
    send_optionalpcb_query(); //send one datagram already at start
  }

  //these two after optional pcb because it needs to send a datagram fast after boot
  if (heishamonSettings.use_1wire) initDallasSensors(log_message, heishamonSettings.updataAllDallasTime, heishamonSettings.waitDallasTime);
  if (heishamonSettings.use_s0) initS0Sensors(heishamonSettings.s0Settings);
}
void setup() {
  // Keep the external TX buffer disabled before UART or networking starts.
  digitalWrite(TX_ENABLE, LOW);
  pinMode(TX_ENABLE, OUTPUT);
  pinMode(CONFIG_RESET, INPUT_PULLUP);
  
  //first get total memory before we do anything
  getFreeMemory();

  //set boottime
  getUptime();
  
  
  setupSerial();
  
  
  Serial.println();
  Serial.println(F("--- HEISHAMON ---"));
  Serial.println(heishamon_version);
  Serial.println(F("starting..."));

  loadSettings(&heishamonSettings);
  setupSerial1();
  setupWifi(&heishamonSettings);
  Network.onEvent([](arduino_event_id_t event) {
    if (event == ARDUINO_EVENT_ETH_START) ETH.setHostname(heishamonSettings.wifi_hostname);
  });
  if (!ETH.begin(ETH_PHY_LAN8720, 1, 23, 18, 16, ETH_CLOCK_GPIO0_IN)) {
    Serial.println("Could not start LAN8720 Ethernet; WiFi setup remains available");
  }

  setupMqtt();
  setupHttp();

  switchSerial();

  setupConditionals(); //setup for routines based on settings

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

}

void send_panasonic_query() {
  String message = F("Requesting new panasonic data");
  log_message((char*)message.c_str());
  send_command(panasonicQuery, PANASONICQUERYSIZE);
}

void send_optionalpcb_query() {
  String message = F("Sending optional PCB data");
  log_message((char*)message.c_str());
  send_command(optionalPCBQuery, OPTIONALPCBQUERYSIZE);
}


void read_panasonic_data() {
  if (sending && ((unsigned long)(millis() - sendCommandReadTime) > SERIALTIMEOUT)) {
    log_message((char*)"Previous read data attempt failed due to timeout!");
    sprintf_P(log_msg, PSTR("Received %d bytes data"), data_length);
    log_message(log_msg);
    if (heishamonSettings.logHexdump) logHex(data, data_length);
    if (data_length == 0) {
      timeoutread++;
      totalreads++; //at at timeout we didn't receive anything but did expect it so need to increase this for the stats
    } else {
      tooshortread++;
    }
    data_length = 0; //clear any data in array
    heatpumpFramer.reset();
    inFlightProxyReply = false;
    sending = false; //receiving the answer from the send command timed out, so we are allowed to send a new command
  }
  if (heatpumpSerial.available() > 0) readSerial();
}

void loop() {
  serviceResetButton();
  // check wifi
  check_wifi();
  // Handle OTA first.
  ArduinoOTA.handle();
  // then handle HTTP
  httpServer.handleClient();
  // handle Websockets
  webSocket.loop();

  mqtt_client.loop();

  read_panasonic_data();
  readProxy();

  if ((!sending) && (cmdnrel > 0)) { //check if there is a send command in the buffer
    log_message((char *)"Sending command from buffer");
    popCommandBuffer();
  }

  if (heishamonSettings.use_1wire) dallasLoop(mqtt_client, log_message, heishamonSettings.mqtt_topic_base);

  if (heishamonSettings.use_s0) s0Loop(mqtt_client, log_message, heishamonSettings.mqtt_topic_base, heishamonSettings.s0Settings);

  if (heishamonSettings.SmartControlSettings.enableHeatCurve) smartControlLoop(log_message, heishamonSettings.SmartControlSettings, actData, goodreads);

  if ((!sending) && (!heishamonSettings.listenonly) && (heishamonSettings.optionalPCB)) send_optionalpcb_query(); //send this as fast as possible or else we could get warnings on heatpump

  // run the data query only each WAITTIME
  if ((unsigned long)(millis() - lastRunTime) > (1000 * heishamonSettings.waitTime)) {
    lastRunTime = millis();
    //check mqtt
    if ( (networkConnected()) && (!mqtt_client.connected()) )
    {
      log_message((char *)"Lost MQTT connection!");
      mqtt_reconnect();
    }

    //log stats
    if (totalreads > 0 ) readpercentage = (((float)goodreads / (float)totalreads) * 100);
    String message = F("Heishamon stats: Uptime: ");
    message += getUptime();
    message += F(" ## Free memory: ");
    message += getFreeMemory();
    message += F("% ");
    message += ESP.getFreeHeap();
    message += F(" bytes ## Wifi: ");
    message += getWifiQuality();
    message += F("% ## Mqtt reconnects: ");
    message += mqttReconnects;
    message += F(" ## Correct data: ");
    message += readpercentage;
    message += F("%");
    log_message((char*)message.c_str());

    String stats = F("{\"uptime\":");
    stats += String(millis());
    stats += F(",\"voltage\":");
    stats += supplyVoltageJson();
    stats += F(",\"free memory\":");
    stats += getFreeMemory();
    stats += F(",\"wifi\":");
    stats += getWifiQuality();
    stats += F(",\"mqtt reconnects\":");
    stats += mqttReconnects;
    stats += F(",\"total reads\":");
    stats += totalreads;
    stats += F(",\"good reads\":");
    stats += goodreads;
    stats += F(",\"bad crc reads\":");
    stats += badcrcread;
    stats += F(",\"bad header reads\":");
    stats += badheaderread;
    stats += F(",\"too short reads\":");
    stats += tooshortread;
    stats += F(",\"too long reads\":");
    stats += toolongread;
    stats += F(",\"timeout reads\":");
    stats += timeoutread;
    stats += F("}");
    sprintf(mqtt_topic, "%s/stats", heishamonSettings.mqtt_topic_base);
    mqtt_client.publish(mqtt_topic, stats.c_str(), MQTT_RETAIN_VALUES);

    //get new data
    if (!heishamonSettings.listenonly) send_panasonic_query();

    //Make sure the LWT is set to Online, even if the broker have marked it dead.
    sprintf(mqtt_topic, "%s/%s", heishamonSettings.mqtt_topic_base, mqtt_willtopic);
    mqtt_client.publish(mqtt_topic, "Online");

    delay(1);
  }
  delay(1);
}
