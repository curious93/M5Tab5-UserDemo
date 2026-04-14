# M5Stack Tab5 Recovery: Erfahrene Lektionen (Rev 1.3)

## Status
Das Gerät wurde erfolgreich in den **Auslieferungszustand (V0.2)** zurückversetzt.

## Kritische Schritte der Wiederbelebung
1. **SHA256-Verifikation**: Der Abgleich des Hashes (`f3e0c860...`) bestätigte, dass die lokale Binary identisch mit dem offiziellen M5Stack-Release ist.
2. **Hard-Erase**: Ein kompletter Flash-Erase vor dem Schreiben war notwendig, um hängende Boot-Prozesse zu beenden.
3. **Optimierter Flash-Parameter**:
   - **Baudrate**: 115200 / 230400 (für maximale Stabilität).
   - **Adresse**: 0x0 (Full Image inkl. Bootloader).
   - **Mode**: DIO (Kompatibilität mit Rev 1.3 Flash).

## Hinweis zur Überwachung
Die Anker-Webcam war während des Prozesses nicht angeschlossen; vorherige Snapshots waren veraltet/echtzeit-unfähig. Die Bestätigung der Funktion erfolgt ausschließlich durch den Benutzer.

## Archivierte Binary
Die funktionierende Firmware liegt gesichert unter: `official_factory_v0.2_rev1.3.bin`
