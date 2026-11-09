<img width="3072" height="4096" alt="MVIMG_20261108_234809" src="https://github.com/user-attachments/assets/9b4f6282-c32d-4974-a4c7-9e394125bf6a" />
# HeishaMon per WT32-ETH01 con Ethernet e CZ-TAW1

Porting sperimentale di **HeishaMon v2.0** per la WT32-ETH01 classica
(ESP32, LAN8720, flash 4 MB), con una seconda seriale per il CZ-TAW1.
Versione: `2.0-WT32-ETH01-CZ-test1`.

La build conserva il decoder e i comandi della v2.0. Aggiunge Ethernet,
proxy CZ, parser con controllo dei pacchetti e salvataggio impostazioni
LittleFS/NVS per consentire anche la migrazione OTA da ESPHome.

## Stato delle verifiche

Verificati su una WT32 reale: caricamento OTA da ESPHome, avvio Ethernet,
interfaccia web, JSON, inizializzazione delle due UART e conservazione
delle impostazioni NVS dopo un riavvio. Superate 20 asserzioni del protocollo.

**La comunicazione seriale completa con PDC e CZ-TAW1 resta da collaudare.**
Il proxy aggiunto alla v2.0 e sperimentale. La prima configurazione mantiene
**Listen Only attivo** e l'abilitazione del buffer TX verso la PDC bassa.
In tale modalita non vengono inoltrati comandi e il proxy non risponde al CZ.

Questo non e un firmware ufficiale HeishaMon. Non usare questi binari
su ESP8266, ESP32-S3, ETH01-EVO o schede con chip/pin differenti.

## Collegamenti

| Porta sulla WT32 | RX | TX |
| --- | --- | --- |
| Panasonic PDC | GPIO35 | GPIO17 |
| CZ-TAW1 separato | GPIO36 | GPIO14 |

Entrambe le UART: **9600 baud, 8E1**. Enable buffer TX PDC: GPIO32,
attivo alto. I due segnali dati del CZ devono essere separati da quelli
della PDC, con topologia `PDC <-> WT32 <-> CZ-TAW1`.

Serve adattamento UART TTL **5 V / 3,3 V** su entrambe le porte, con GND
comune per circuiti non isolati. GPIO35/36 sono ingressi senza pull-up
interni. Un convertitore senza enable non viene fisicamente disconnesso
dalla sola impostazione Listen Only. Leggere il [cablaggio](docs/WIRING.md).

## Funzioni

- Ethernet LAN8720 con DHCP, Wi-Fi opzionale e hotspot di configurazione.
- Pagina web, JSON HTTP, MQTT e aggiornamento firmware dal browser.
- Comandi della v2.0, incluso `SetHeatpump`: `1` accende, `0` spegne.
- Potenze elettriche assorbite e termiche prodotte per riscaldamento,
  raffrescamento e ACS: valori in **W, a passi di 200 W**, non kWh cumulati.
- Proxy CZ: risposte base dalla cache, inoltro dei comandi e delle risposte;
  le risposte in cache scadono dopo 30 secondi.
- DS18B20 e due ingressi S0 opzionali, disabilitati inizialmente.

Le funzioni di controllo richiedono interfaccia elettrica e cablaggio
corretti e Listen Only disattivato. Il motore Rules non appartiene alla v2.0.

## Firmware pronto

I file si trovano in [dist](dist/):

- `HeishaMon-v2.0-WT32-ETH01-CZ-test1-factory.bin`: USB-UART, immagine completa
  da 4 MB da scrivere a **0x0**; sostituisce firmware e impostazioni.
- `HeishaMon-v2.0-WT32-ETH01-CZ-test1-ota.bin`: sola applicazione per OTA.
- `checksums-wt32-v2-cz.json`: SHA256, MD5 e dimensioni.

**Non caricare il file factory nella pagina OTA.**
L'OTA conserva bootloader e tabella partizioni; una migrazione da ESPHome
puo quindi usare NVS al posto di LittleFS. Le istruzioni distinguono
[prima installazione e aggiornamenti](docs/INSTALLATION.md).

## Build e controlli

Con Arduino CLI disponibile nel PATH e core ESP32 3.3.11 installato:

```powershell
.\scripts\build.ps1 -InstallDependencies
.\scripts\verify.ps1
```

Dipendenze: ArduinoJson 6.21.5, WebSockets 2.7.1, PubSubClient 2.8,
DallasTemperature 4.0.6, OneWire 2.3.8. Il flag `-funsigned-char` e obbligatorio
per conservare l'interpretazione dei byte del decoder v2.0.

I test del protocollo sono indipendenti da Arduino:

```sh
g++ -std=c++20 -fsyntax-only firmware/HeishaMon/cz_protocol_tests.cpp
```

La CI esegue questi test e verifica l'integrita di sorgenti e binari.
I sorgenti conservano i byte originali tramite `.gitattributes` per rendere
gli hash verificabili anche dopo un clone su un altro sistema.
[Risultati e limiti](docs/VALIDATION.md).

## Origine del codice

Base: [HeishaMon v2.0](https://github.com/HeishaMon/HeishaMon/tree/b8c0e5843c079edef657af7ff5c23b1596a824c1).
Il comportamento del proxy e stato adattato prendendo come riferimento
il [proxy HeishaMon v4.2.3](https://github.com/HeishaMon/HeishaMon/blob/v4.2.3/HeishaMon/HeishaMon.ino).
Riferimenti e avvisi delle dipendenze in [NOTICE.md](NOTICE.md).
