# Installazione e aggiornamento

Prima delle prove collegare la sola WT32 sul banco, separata dalla PDC.
Usare un'interfaccia elettrica verificata prima di collegare le UART.

## USB-UART: prima installazione

Adattatore USB-UART con segnali logici a 3,3 V:

- TX adattatore -> RX0 / GPIO3 WT32.
- RX adattatore -> TX0 / GPIO1 WT32.
- GND comune e alimentazione regolata adeguata.
- IO0 a GND durante ingresso nel bootloader, poi riaccensione.

Con esptool installato, sostituire `COM_PORT` con la porta effettiva:

```powershell
esptool --chip esp32 --port COM_PORT --baud 115200 write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB 0x0 dist/HeishaMon-v2.0-WT32-ETH01-CZ-test1-factory.bin
```

La factory sostituisce tutti i 4 MB di flash, comprese le impostazioni.
Togliere IO0-GND e spegnere/riaccendere prima di usare Ethernet.

## Migrazione da ESPHome attraverso Ethernet

Serve ESPHome OTA TCP 3232 attivo e protocollo v2. Lo script attuale
gestisce il caso senza password, verificato durante lo sviluppo; se il
server richiede autenticazione lo script si ferma prima della scrittura.

In Windows PowerShell, sostituire `DEVICE_IP` e `DEVICE_MAC` con i dati
verificati della propria scheda:

```powershell
.\scripts\upload-esphome.ps1 -Address DEVICE_IP -ExpectedMac DEVICE_MAC
```

Lo script controlla MAC ARP e checksum prima di caricare il solo file
`-ota.bin`, poi verifica le conferme ESPHome per ogni blocco e per l'immagine.
Il supporto della porta API ESPHome 6053 non dimostra da solo che OTA sia attivo.

L'OTA conserva bootloader e partizioni. Questa build usa LittleFS quando
esiste una partizione `spiffs`, altrimenti NVS per configurazione, curva
termica e dati optional PCB. Non e una cancellazione completa della flash.
Il file factory non deve essere inviato attraverso OTA.

## Primo accesso HeishaMon

Collegare la LAN al router e cercare l'indirizzo DHCP della WT32. Puo
essere diverso dal precedente IP ESPHome. Aprire `http://DEVICE_IP/`.
Se non trova una rete disponibile, viene aperto l'hotspot `HeishaMon-Setup`
con pagina a `http://192.168.4.1/`.

La password amministrativa iniziale e `heisha`, utente aggiornamento `admin`.
Cambiarla nelle impostazioni. Il firmware parte in Listen Only; il proxy
e predisposto ma resta inattivo finche Listen Only e attivo.

`/status` mostra versione, MAC, interfaccia, partizioni, storage, stato
Listen Only, pin e inizializzazione UART, contatori PDC/CZ.
`/json` mostra i 106 topic della v2.0. Campi vuoti e contatori validi a zero
non sono una lettura reale dello stato della PDC.

## Aggiornamenti successivi

Dopo la migrazione usare la pagina `/firmware` di HeishaMon, campo
**Firmware**, con il file `-ota.bin` della stessa build. Lo script ESPHome
non e l'aggiornatore del nuovo HeishaMon. Non caricare il file factory o
un'immagine filesystem nel campo Firmware.

Per funzionare con PDC e CZ occorre il [cablaggio separato](WIRING.md).
La verifica Ethernet non sostituisce il collaudo delle comunicazioni seriali.
