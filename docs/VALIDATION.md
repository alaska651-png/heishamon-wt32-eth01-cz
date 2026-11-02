# Verifiche e limiti: CZ-test1

Data delle prove: 2 novembre 2026.

## Software

- Base HeishaMon v2.0, commit b8c0e5843c079edef657af7ff5c23b1596a824c1.
- Core ESP32 3.3.11, Arduino CLI 1.5.1, flash 4 MB DIO/40 MHz, PSRAM off.
- Programma: 1.271.423 byte; variabili globali: 60.880 byte.
- Immagine OTA: 1.271.568 byte; factory: 4.194.304 byte.
- 20 `static_assert` del protocollo superate: CRC, lunghezze, frammenti,
  pacchetti consecutivi, timeout, rollover, cache, richieste e blocco TX.
- Controllo immagine esptool: chip ID ESP32, checksum e hash validi.
- SHA256 e MD5, limiti delle partizioni factory e coerenza factory/OTA verificati.
- I sorgenti della compilazione sono stati confrontati con quelli consegnati.
- Decoder e logica dei comandi Panasonic conservati dalla v2.0; per i comandi
  cambia soltanto la persistenza dei dati optional PCB.

La prima compilazione completa ha mostrato due warning della libreria
OneWire 2.3.8 nelle direttive `#undef`, senza errori di compilazione.
1-Wire e S0 rimangono disabilitati al primo avvio e non sono collaudati.

## Prove sulla WT32 reale

Verificati trasferimento ESPHome OTA v2, controllo MD5 e conferma finale,
avvio su Ethernet, pagina principale, JSON con 106 topic e pagina settings.
Entrambe le UART si inizializzano; baud nominale 9600, il driver legge 9603.
Listen Only resta attivo e l'enable TX GPIO32 e basso.

La scheda di prova conservava le partizioni ESPHome ESP-IDF: due slot
applicazione da 0x1C0000 byte e NVS da 0x6D000 byte, senza filesystem.
La build ha salvato le impostazioni in NVS e le ha conservate dopo un riavvio.

## Prove ancora necessarie

**Non e ancora verificato il dialogo completo tra PDC e CZ attraverso il proxy.**
Durante le prove non sono stati ricevuti pacchetti PDC o CZ validi e sul
RX CZ sono comparsi byte non validi, compatibili con ingresso flottante o
cablaggio da correggere. Verificare livelli, stato alto a riposo e cablaggio
prima di attivare comandi o automazioni.

Queste prove non attestano stabilita operativa del proxy, affidabilita dei
comandi alla PDC o precisione metrologica dei dati di potenza.
