#include <M5Unified.h>

void setup() {
    // M5Stack Hardware initialisieren
    auto cfg = M5.config();
    M5.begin(cfg);
    
    // Max Helligkeit
    M5.Display.setBrightness(255);

    // Bildschirm rot füllen
    M5.Display.fillScreen(TFT_RED);
    
    // Text Ausgabe
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(4); // Großer Text für 720p Auflösung
    
    M5.Display.setCursor(50, 100);
    M5.Display.println("M5GFX Raw Test Erfolgreich!");
    
    M5.Display.setCursor(50, 200);
    M5.Display.printf("Aufloesung: %d x %d", M5.Display.width(), M5.Display.height());
    
    M5.Display.setCursor(50, 300);
    M5.Display.println("Tippe auf den Screen um ihn gruen zu faerben!");
}

void loop() {
    M5.update();
    
    // Wenn der Touchscreen berührt wird, färbe den Bildschirm grün
    if (M5.Touch.getCount() > 0) {
        auto touch = M5.Touch.getDetail();
        if (touch.isPressed()) {
            M5.Display.fillScreen(TFT_GREEN);
            // Kleiner Debounce
            delay(100);
        }
    }
    
    delay(10);
}
