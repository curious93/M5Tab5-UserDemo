// wifi_credentials.h — zentrale WiFi-Config für M5_3
//
// Wird von app_main.cpp und app_spotify.cpp included.
// Ändere SSID/PASSWORD hier, dann ./build.sh — kein Code-Suchen nötig.
//
// Mehrere Netze möglich: WIFI_NETWORKS Array, Tab5 versucht in Reihenfolge.
// Aktuell wird nur das erste genutzt (Multi-Network-Logik kommt wenn nötig).

#pragma once

#define WIFI_PRIMARY_SSID     "Ingo"
#define WIFI_PRIMARY_PASSWORD "Loggo03!"

// Backup-WLAN (z.B. Smartphone-Hotspot) — nur dokumentarisch, nicht aktiv genutzt
// #define WIFI_BACKUP_SSID     "MyHotspot"
// #define WIFI_BACKUP_PASSWORD "..."
