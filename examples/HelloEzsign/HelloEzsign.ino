// HelloEzsign.ino
//
// Minimal example: press the button, write a solid-color full-screen image
// to a 4-color NFC e-paper badge. Each press cycles through colors:
//   WHITE -> BLACK -> YELLOW -> RED -> WHITE ...
//
// This is the simplest possible "did the wiring work?" sketch.
//
// Required libraries:
//   - M5Unified            (Arduino Library Manager)
//   - PN5180-Library       (GitHub ZIP download:
//                           https://github.com/ATrappmann/PN5180-Library )
//   - ezsignNFC            (this library)
//   - miniLZO              (place 4 files in THIS sketch folder; see below)
//
// miniLZO setup (REQUIRED for this sketch to compile):
//   Download these 4 files from
//       https://github.com/yuhaoth/minilzo
//   and copy them into the SAME folder as this .ino file:
//       minilzo.c
//       minilzo.h
//       lzoconf.h
//       lzodefs.h
//   Arduino IDE will automatically compile minilzo.c with the sketch.
//
// Wiring (M5AtomS3 -> PN5180):
//   PN5180         M5AtomS3
//   ----------     ---------
//   +5V         -> +5V
//   +3.3V       -> 3.3V    <-- REQUIRED. Most PN5180 modules need both rails.
//   RST         -> G38
//   NSS         -> G5
//   MOSI        -> G8
//   MISO        -> G6
//   SCK         -> G7
//   BUSY        -> G2
//   GND         -> GND
//   IRQ         -> (not used)
//
// If your wiring is different, pass a custom EzsignPins to the constructor:
//
//   EzsignPins pins;
//   pins.nss  = 10;
//   pins.rst  = 9;
//   // ... etc ...
//   EzsignDevice ez(pins);
//
// Usage on the device:
//   1. Power on the M5AtomS3. Display shows "Place card / Press button".
//   2. Hold an NFC e-paper badge against the PN5180 antenna.
//   3. Press the AtomS3 button. The e-paper refreshes (~30-60 sec).
//   4. Press again to cycle to the next color.

#include <M5Unified.h>
#include <ezsignNFC.h>

// Default pins (M5AtomS3). To override:
//   EzsignPins pins;
//   pins.nss = 10; pins.rst = 9; // etc.
//   EzsignDevice ez(pins);
EzsignDevice ez;

// Cycle state
static const uint8_t COLOR_CYCLE[] = {
  COLOR_WHITE, COLOR_BLACK, COLOR_YELLOW, COLOR_RED
};
static const char* COLOR_NAMES[] = {
  "WHITE", "BLACK", "YELLOW", "RED"
};
static int g_colorIndex = 0;

static void onLog(const char* msg) {
  Serial.println(msg);
}

static void showLocal(const char* l1, const char* l2 = "", const char* l3 = "") {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.println(l1);
  M5.Display.println(l2);
  M5.Display.println(l3);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(300);

  Serial.println("\n=== ezsignNFC HelloEzsign demo ===");

  ez.setLogger(onLog);
  if (!ez.begin()) {
    Serial.printf("begin failed: %s\n",
                  EzsignDevice::errorString(ez.lastError()));
    showLocal("FATAL", EzsignDevice::errorString(ez.lastError()));
    while (true) delay(1000);
  }

  showLocal("Ready", "Place card,", "press button");
}

static bool writeSolidColor(uint8_t color) {
  Serial.printf("Writing solid %s ...\n", COLOR_NAMES[g_colorIndex]);

  // Detect + auth
  if (!ez.detect()) {
    Serial.printf("detect: %s\n", EzsignDevice::errorString(ez.lastError()));
    return false;
  }
  if (!ez.authenticate()) {
    Serial.printf("auth: %s\n", EzsignDevice::errorString(ez.lastError()));
    return false;
  }

  // Find out the badge's resolution
  EzsignDeviceInfo info;
  if (!ez.getDeviceInfo(info) || !info.valid) {
    Serial.println("getDeviceInfo failed");
    return false;
  }
  Serial.printf("Badge: '%s' %dx%d, %d colors\n",
                info.name, info.width, info.height, info.numColors);

  // Build a solid-color image
  size_t n = (size_t)info.width * info.height;
  uint8_t* indices = (uint8_t*) malloc(n);
  if (!indices) { Serial.println("OOM"); return false; }
  memset(indices, color, n);

  // Send + refresh. flipH=true matches most 4.2-inch units.
  bool ok = ez.sendImageIndices(indices, info.width, info.height,
                                /*flipH=*/true);
  free(indices);
  if (!ok) {
    Serial.printf("send: %s\n", EzsignDevice::errorString(ez.lastError()));
    return false;
  }

  if (!ez.refreshDisplay(/*blocking=*/false)) {
    Serial.printf("refresh: %s\n", EzsignDevice::errorString(ez.lastError()));
    return false;
  }
  Serial.println("Refreshing display (~30-60 s) ...");
  ez.waitForRefresh();   // soft success: ok even if poll times out
  Serial.println("Done.");
  return true;
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    uint8_t color = COLOR_CYCLE[g_colorIndex];
    showLocal("Writing...", COLOR_NAMES[g_colorIndex], "Hold still");

    bool ok = writeSolidColor(color);

    showLocal(ok ? "OK" : "FAIL",
              COLOR_NAMES[g_colorIndex],
              "Press again");
    if (ok) {
      g_colorIndex = (g_colorIndex + 1) % 4;
    }
  }
  delay(20);
}
