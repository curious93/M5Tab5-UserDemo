# Tab5 Network-Diagnose (2026-05-03, Update 2)

## Befund: WLAN "Ingrid" hat KEIN Internet

**Verifiziert mit Internet-Sanity-Check (TCP-Connect-Tests aus dem Tab5):**

```
[INET_CHECK 1.1.1.1:80   dns_took=0ms DNS_OK]
[INET_CHECK 1.1.1.1:80   tcp_took=18249ms FAIL errno=113]   ← EHOSTUNREACH
[INET_CHECK 1.1.1.1:443  dns_took=0ms DNS_OK]
[INET_CHECK 1.1.1.1:443  tcp_took=18500ms FAIL errno=113]   ← EHOSTUNREACH
```

`errno=113` = `EHOSTUNREACH` = "No route to host"

Das ist **nicht Spotify-spezifisch** — Tab5 erreicht nicht mal Cloudflare 1.1.1.1. Das WLAN hat entweder:
- Keinen Internet-Uplink (LAN-only)
- Eine Firewall die alle ausgehenden TCP-Connections blockt
- Captive Portal das ohne Browser-Login alles filtert

## Aktueller Code-Stand (alles gefixt was code-seitig fixbar war)

- ✅ DNS auf Cloudflare 1.1.1.1/1.0.0.1 (ESP-Hosted DHCP-Bug umgangen)
- ✅ TLS-Connect-Sentinel `[TLS_CONNECT host=X took=Yms FAILED rc=Z]`
- ✅ Internet-Sanity-Check beim Boot mit `[INET_CHECK ...]` Sentinels
- ✅ WiFi-Credentials in `wifi_credentials.h` zentralisiert (kein Source-Suchen mehr nötig)
- ✅ esp_hosted Component nach `components/` verschoben

## Wie WLAN wechseln (1 Datei, 2 Zeilen)

`/Users/nicolasvonbruck/Desktop/M5_3/M5Tab5-UserDemo-Reference/platforms/tab5/main/wifi_credentials.h`:

```c
#define WIFI_PRIMARY_SSID     "DeinNeuesWLAN"
#define WIFI_PRIMARY_PASSWORD "DeinPasswort"
```

Dann:
```bash
cd /Users/nicolasvonbruck/Desktop/M5_3
./build.sh                  # build + flash + monitor in einem
./wifi_check.sh             # automatische Network-Diagnose
```

## Smartphone-Test (10 Sekunden, klärt alles)

1. Smartphone in WLAN "Ingrid" einbuchen
2. Browser: `https://1.1.1.1` öffnen
   - **Kommt eine Seite?** → Ingrid hat Internet → Tab5-spezifisches Problem
   - **Timeout/Fehler?** → Ingrid hat KEIN Internet → anderes WLAN nutzen

## Wenn Ingrid wirklich kein Internet hat

Tipp: Smartphone-Hotspot anmachen (z.B. "iPhone-vonbrueck"), Passwort in `wifi_credentials.h` eintragen, build.sh, fertig. Skip-Test geht dann.

## Status: Was kann jetzt sofort weitergehen

- **Phase 4** (Skip-Baseline) sobald Tab5 Spotify erreicht
- **Phase 5** (autonome Hypothesen) läuft danach komplett selbstständig

Solange WLAN blockt: alle Code-Änderungen unbestreitbar. Fix ist beim User (Router/WLAN), nicht im Code.
