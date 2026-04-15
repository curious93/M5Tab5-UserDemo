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
- **ESP-IDF Pfad:** `~/esp/esp-idf-v5.4.2-tab5/export.sh`
- **esptool Mindestversion: v4.11** — v4.7 kann den ESP32-P4 Stub nicht laden (`Invalid head of packet 0x45`). esptool meldet Chip fälschlicherweise als `v0.0` statt `v1.3` bei alter Version.

### Verifizierte Gerätedaten (physisches Gerät, ausgelesen 2026-04-15)
- **MAC-Adresse:** `30:ed:a0:ea:8e:2f`
- **Chip:** ESP32-P4, Revision v1.3, eFuse Block Revision v0.3, Chip ID: 18
- **CPU:** 360 MHz, Dual-Core
- **Crystal:** 40 MHz
- **USB-Modus:** USB-Serial/JTAG
- **Flash:** 16 MB, SPI Mode QIO, 80 MHz, Hersteller `0x46`, Device `0x4018`
- **Flash Status:** `0x0200`
- **PSRAM:** 32 MB HEX-PSRAM (AP, Generation 4, 256 Mbit), 200 MHz, X16 Mode
- **Secure Boot:** Disabled
- **Flash Encryption:** Disabled

### Display & Touch
- **Controller:** ST7123 (Display + Touch Kombi-IC), I2C Adresse `0x55`
- **Interface:** MIPI DSI
- **Auflösung:** 720 × 1280 px
- **LCD ID:** `80 A0 FB`
- **Touch Firmware:** Version 3 (1.71.1.3), Max. 10 Touchpunkte
- **Touch Panel Version:** 1.0.0
- **Reset GPIO:** GPIO 23
- **Backlight:** GPIO 22 (LEDC, Konflikt beachten)

### Audio
- **ADC (Mikrofon):** ES7210, 4× Mikrofone (MIC1–4), TDM Mode, 48 kHz, 16 Bit, I2C `0x40`
- **DAC (Lautsprecher):** ES8388, I2C `0x10`
- **I2S:** STD Mode, 32/16 Bit, 48 kHz

### IMU
- **Sensor:** BMI270 (Bosch), I2C `0x68`

### Power & Stromversorgung
- **Power Monitor:** INA226, I2C `0x41` (Bus Voltage beim Auslesen: 1.75V)
- **RTC:** RX8130, I2C `0x32`
- **IO-Expander:** PI4IOE, I2C `0x43` — Master-Schalter für EXT5V_EN

### I2C Bus Scan (vollständig, verifiziert)
| Adresse | Gerät |
|---------|-------|
| `0x10` | ES8388 (Audio DAC) |
| `0x28` | ? |
| `0x32` | RX8130 (RTC) |
| `0x36` | ? |
| `0x40` | ES7210 (Audio ADC) |
| `0x41` | INA226 (Power Monitor) |
| `0x43` | PI4IOE (IO-Expander) |
| `0x44` | ? |
| `0x55` | ST7123 (Display + Touch) |
| `0x68` | BMI270 (IMU) |

### WiFi / Connectivity
- **WiFi-Chip:** ESP32-C6 (Slave via SDIO), Board Type 13, Chip ID 12
- **SDIO:** 4-bit, 40 MHz, CLK[12] CMD[13] D0[11] D1[10] D2[9] D3[8], Slave_Reset[15]
- **WiFi AP SSID:** `M5Tab5-UserDemo-WiFi` (kein Passwort)
- **DHCP:** 192.168.4.1

### Partition Table (Factory Firmware)
| Label | Type | Offset | Größe |
|-------|------|--------|-------|
| nvs | WiFi data | 0x009000 | 24 KB |
| phy_init | RF data | 0x00F000 | 4 KB |
| factory | App | 0x010000 | 10 MB |
| human_face_det | Data | 0xA10000 | 400 KB |
| storage | Data | 0xA74000 | 2 MB |

### Binaries & Recovery
- **Factory RESCUE:** `/Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/M5Tab5-UserDemo-Reference/binaries/official_factory_v0.2_rev1.3.bin`
  - SHA256: `f3e0c860001c8eeda4fc4c3664077f408782ff213a3138e32c9f489f0d158827`
  - Größe: 5.5 MB, flashen ab `0x0000`
- **Golden Build (Robo-Avatar 12:39):** `M5Tab5-UserDemo-Reference/platforms/tab5/build/m5stack_tab5.bin`
  - SHA256: `ead0a6a1226fc90f0df04f2c8cfb028871d6dd64563ffe72f8c089798a767c1e`
  - Größe: 2.6 MB

### Flash-Befehl (verifiziert, funktioniert)
**WICHTIG ESP32-P4:** Bootloader sitzt bei `0x2000`, NICHT bei `0x0000` (anders als ESP32-S3). Falsche Adresse → Endlosloop mit `invalid header: 0x...`.

```bash
source ~/esp/esp-idf-v5.4.2-tab5/export.sh

# Factory Rescue (Single-Image Dump, beginnt bei 0x0000):
esptool.py --chip esp32p4 --port /dev/cu.usbmodem* --baud 115200 \
  write_flash --erase-all 0x0000 <factory.bin>

# Golden Build (alle 3 Komponenten, Bootloader bei 0x2000):
esptool.py --chip esp32p4 --port /dev/cu.usbmodem* --baud 921600 \
  --before=default_reset --after=hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x2000  build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/m5stack_tab5.bin
```

### Hardware-Spezifika (Post-Okt 2025 "Frankenstein-Batch")
  - **Display-Treiber:** Das Display läuft **nicht** über ILI9881C, sondern zwingend über den **ST7123** Display/Touch-Kombi-IC (I2C Adresse: `0x55`).
  - **Power-Sequencing (WICHTIG):** Der **PI4IOE** IO-Expander (`0x43`) an I2C Bus 0 fungiert als Master-Schalter. Bevor Display, Audio oder WLAN Strom bekommen, muss er 5V (`EXT5V_EN`) freischalten.
  - **Boot-Timings:** Beim Flashen muss dem LVGL-Display-System ca. 10-12 Sekunden Bootzeit eingeräumt werden, bevor der SPI-Bus und Framebuffer visuelle Ergebnisse zeigen (kein unmittelbarer Startscreen).
