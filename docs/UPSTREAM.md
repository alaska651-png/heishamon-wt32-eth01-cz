# Provenienza e build

Base: HeishaMon v2.0, commit `b8c0e5843c079edef657af7ff5c23b1596a824c1`.
Archivio originale:
https://codeload.github.com/HeishaMon/HeishaMon/zip/refs/tags/v2.0

SHA256 dell'archivio verificato:
`347d06deca40d9ef0a9e5016fa7d1dec1c8912f46f6b490e27980cf89a5d81c1`.

I 22 file del firmware consegnato sono elencati in `source-sha256.json`.
La build usa Arduino CLI 1.5.1, ESP32 3.3.11 e le versioni delle librerie
indicate nel README e in NOTICE.md. Il flag `-funsigned-char` preserva
l'interpretazione dei byte della v2.0.

Il decoder e i comandi della v2.0 sono conservati. Sono adattati hardware,
networking, persistenza e trasporto seriale. I nuovi file del parser CZ e
della persistenza sono separati per facilitare la revisione.

I binari in `dist/` corrispondono alla build provata su una WT32 reale per
avvio, Ethernet e impostazioni. La ricompilazione in un altro ambiente
puo produrre hash differenti per metadati e percorsi della toolchain.
