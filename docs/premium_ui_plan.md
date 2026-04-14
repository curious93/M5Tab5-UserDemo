# Robo-Avatar Premium UI: Upgrade Plan

Der aktuelle Status zeigt eine funktionierende, aber optische einfache Grundstruktur ("Hello World" des Avatars). Jetzt transformieren wir das Projekt in ein **Premium-Produkt** mit hoher visueller Qualität und flüssigen Animationen.

## 1. Visuelle Aufwertung (Aesthetics)
- **Glowing Eyes**: Statt flacher Kreise verwenden wir LVGL-Styles mit `bg_grad` (radialer Verlauf) und `shadow_width`. Die Augen sollen pulsieren ("Atmen").
- **Glassmorphism Status-Bar**: Eine halbtransparente Leiste am unteren Rand zeigt Systemdaten (FPS, IMU, Vision-Status) in einem sauberen, technoiden Look.
- **Animated Mouth**: Der Mund wird flüssiger animiert (Sinus-Welle für "Energietransfer").

## 2. Animationen & Interaktion
- **Blinzeln (Blink Logic)**: Ein asynchroner Timer lässt den Avatar gelegentlich blinzeln, um ihn lebendig zu machen.
- **Smooth Pupil Motion**: Wir implementieren eine einfache Dämpfung (Lerp) für die Pupillenbewegung, damit sie nicht springen, wenn Vision-Ziele erkannt werden.
- **Startup Sequence**: Kurze Animation beim Start, bei der die Augen nacheinander "hochfahren".

## 3. Audio-Integration
- Das Projekt enthält bereits `canon_in_d.mp3` und `startup_sfx.mp3`. Wir werden eine einfache Audio-Wiedergabe beim Start integrieren (via `bsp_codec` und I2S).

## 4. Vision Refining
- Die aktuelle Bewegungs-Erkennung in `hal_camera.cpp` ist funktional. Wir optimieren die Empfindlichkeit und die Darstellung des Kamerafeeds im Hintergrund (evtl. leicht abgedunkelt oder mit Blaufilter für den "Robo-Look").

## Zeitplan
1. **Modifikation `app_main.cpp`**: Implementierung der neuen Styles und Animationen.
2. **Setup High-End Styles**: Erstellen von wiederverwendbaren LVGL-Style-Tokens.
3. **Audio-Trigger**: Hinzufügen des Startup-Sounds.
4. **Build & Flash**: Automatisierter Deploy-Zyklus mit Webcam-Verifikation.

> [!IMPORTANT]
> Sollen wir mit dem Premium-Redesign starten? Ich werde die `app_main.cpp` entsprechend umbauen.
