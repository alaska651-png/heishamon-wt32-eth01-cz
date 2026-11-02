# Origine del codice e dipendenze

Questo progetto deriva dal firmware HeishaMon v2.0 del progetto
[HeishaMon/HeishaMon](https://github.com/HeishaMon/HeishaMon), tag `v2.0`,
commit `b8c0e5843c079edef657af7ff5c23b1596a824c1`.
I nomi e gli avvisi originali restano conservati nei sorgenti.
Non viene rivendicata la paternita del codice HeishaMon originale.

L'archivio sorgente di quel tag non contiene un file LICENSE di progetto.
Questo repository non aggiunge una licenza generale che riassegni i diritti
sul codice originale. Fare riferimento al progetto e agli autori originali
per i termini del codice HeishaMon. Le licenze delle singole dipendenze
rimangono applicabili ai rispettivi componenti.

Il proxy e un adattamento sperimentale alla v2.0, con comportamento preso
come riferimento da `readProxy()` di HeishaMon v4.2.3. Il motore Rules
della v4.2.3 non e incluso in questa build.

Il caricatore PowerShell ESPHome implementa il protocollo documentato dal
[client ufficiale ESPHome 2026.2.4](https://github.com/esphome/esphome/blob/2026.2.4/esphome/espota2.py).
ESPHome non e incluso come firmware o libreria nell'immagine HeishaMon.

## Dipendenze della build

| Componente | Versione | Sorgenti e licenze originali |
| --- | --- | --- |
| Arduino core ESP32 | 3.3.11 | https://github.com/espressif/arduino-esp32/tree/3.3.11 |
| ESP-IDF e componenti del core | v5.5.5 nella toolchain verificata | https://github.com/espressif/esp-idf/tree/v5.5.5 |
| ArduinoJson | 6.21.5 | https://github.com/bblanchon/ArduinoJson/tree/v6.21.5 |
| WebSockets | 2.7.1 | https://github.com/Links2004/arduinoWebSockets/tree/2.7.1 |
| PubSubClient | 2.8 | https://github.com/knolleary/pubsubclient/tree/v2.8 |
| DallasTemperature | 4.0.6 | https://github.com/milesburton/Arduino-Temperature-Control-Library |
| OneWire | 2.3.8 | https://github.com/PaulStoffregen/OneWire/tree/v2.3.8 |

I pacchetti della toolchain e delle librerie non sono vendorizzati in questo
repository. Lo script di build usa versioni fissate e sorgenti modificabili
per ricompilare l'applicazione. I testi di licenza distribuiti dai pacchetti
delle librerie sono conservati nella cartella `licenses/`.
