# Cablaggio WT32-ETH01 / Panasonic / CZ-TAW1

I numeri indicano GPIO ESP32, non posizioni fisiche del connettore.
Verificare serigrafia della propria WT32 e pinout del cavo Panasonic.

| Segnale | GPIO WT32 | Adattamento richiesto |
| --- | --- | --- |
| TX Panasonic -> RX HeishaMon | 35 | 5 V -> 3,3 V |
| TX HeishaMon -> RX Panasonic | 17 | 3,3 V -> 5 V |
| TX CZ-TAW1 -> RX proxy | 36 | 5 V -> 3,3 V |
| TX proxy -> RX CZ-TAW1 | 14 | 3,3 V -> 5 V |
| Enable del buffer TX PDC | 32 | Alto abilita, basso disabilita |

La topologia e `PDC <-> WT32 <-> CZ-TAW1`. Il CZ si collega alla UART
dedicata: i suoi due fili dati devono essere separati dal bus PDC.
Le due porte sono UART TTL a 9600 baud, 8 bit, parita pari, 1 stop.
RS232 e RS485 non sono equivalenti a questa interfaccia TTL.

I GPIO ESP32 non tollerano 5 V. GPIO35 e GPIO36 sono solo ingressi e non
hanno pull-up interni: l'interfaccia deve mantenere il lato RX a livello
alto di 3,3 V a riposo. Eventuali pull-up vanno sul lato LV del convertitore.
Un RX lasciato flottante puo generare byte non validi.

Per circuiti non isolati serve GND comune. Alimentare la WT32 con una
sorgente regolata adeguata, con almeno 500 mA disponibili. Non unire il
positivo 5 V esterno a quello Panasonic e non applicare i 12 V CN-CNT
ai pin 5 V o 3V3 della WT32. L'alimentazione originale del CZ puo essere
conservata separando i due fili dati.

Il primo avvio mantiene Listen Only attivo. GPIO32 e basso ma un
convertitore blu senza enable non viene fisicamente disconnesso dal
firmware: tenere inizialmente scollegato il TX GPIO17 finche non sono
verificati interfaccia e cablaggio. Un buffer con enable deve avere un
pull-down hardware che lo mantenga disabilitato durante reset/avvio.

Dopo la verifica elettrica, il CZ puo usare il proxy con `CZ-TAW1 proxy`
selezionato e Listen Only disattivato. In Listen Only il proxy non inoltra
comandi e non risponde al CZ.

| Altra funzione | GPIO |
| --- | --- |
| Pulsante reset impostazioni verso GND, oltre 10 secondi | 33 |
| UART0 debug/programmazione | TX0 1, RX0 3 |
| DS18B20 opzionale | 4 |
| S0 opzionali | 5 / 15 |
| Ingresso aggiuntivo senza pull-up interno | 39 |

Le periferiche opzionali sono inizialmente disabilitate. GPIO5 e GPIO15
sono anche pin di bootstrap e gli eventuali circuiti devono rispettare
i livelli di avvio.

Ethernet riserva GPIO0, 16, 18, 19, 21, 22, 23, 25, 26 e 27.
PHY LAN8720, indirizzo 1, MDC 23, MDIO 18, power/oscillatore 16,
clock esterno su GPIO0. Togliere il ponte IO0-GND dopo la programmazione USB.
