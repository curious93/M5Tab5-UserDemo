# Hardware & Projekt-Gedächtnis für KI

**WICHTIG: Vermeide Projekt-Verwechslungen!**

## Projekt 1: M5StickC Plus2 ("Camaro Counter")
- **Plattform:** Läuft erfolgreich unter `PlatformIO` und `Arduino` (via `M5Unified`).
- **Probleme:** Wir hatten dort bei LVGL Screen-Tearing (Flimmern) beim Zeichnen der UI. Dies ist ein reines Software/Buffer-Problem unter PlatformIO.

## Projekt 2: M5Tab5 ("Robo-Avatar")
- **Hardware:** Nutzt den brandneuen ESP32-P4 Chip. Dieses physische Gerät ist ein frühes Engineering-Sample mit der **Chip Revision 1.3**.
- **PlatformIO-Falle:** PlatformIO / Arduino (via `M5Unified`) unterstützt den PI4IOE IO-Expander des Tab5 aktuell *nicht korrekt*. **Das Display bleibt unter PlatformIO IMMER SCHWARZ.** Es ist KEIN LVGL-Flimmern-Problem, sondern das Panel bekommt gar keinen Strom. Gehe mit dem Tab5 NIEMALS zurück zu PlatformIO.
- **ESP-IDF v5.5 Falle:** Ein Kompilieren unter ESP-IDF v5.5 (oder neuer) übersetzt den Code mit Maschinenbefehlen für ESP32-P4 ab Revision 3.1. Wenn diese zu neue Firmware auf den Rev 1.3 Chip geflasht wird, crasht das Gerät beim Start im Bootloader mit `Illegal Instruction` und das Display bleibt komplett schwarz.
- **Einzige Lösung:** Das Gerät *muss zwingend* nativ unter **ESP-IDF Version v5.4.2** kompiliert und geflasht werden, damit es bei der Rev 1.3 Hardware funktioniert.
- **Hardware-Spezifika (Post-Okt 2025 "Frankenstein-Batch"):**
  - **Display-Treiber:** Das Display läuft **nicht** über ILI9881C, sondern zwingend über den **ST7123** Display/Touch-Kombi-IC (I2C Adresse: `0x55`).
  - **Power-Sequencing (WICHTIG):** Der **PI4IOE** IO-Expander (`0x43`) an I2C Bus 0 fungiert als Master-Schalter. Bevor Display, Audio oder WLAN Strom bekommen, muss er 5V (`EXT5V_EN`) freischalten.
  - **Boot-Timings:** Beim Flashen muss dem LVGL-Display-System ca. 10-12 Sekunden Bootzeit eingeräumt werden, bevor der SPI-Bus und Framebuffer visuelle Ergebnisse zeigen (kein unmittelbarer Startscreen).
