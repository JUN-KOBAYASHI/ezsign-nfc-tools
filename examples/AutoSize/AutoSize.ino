// AutoSize.ino
//
// Auto-detect the e-paper size via getDeviceInfo() and write a matching
// test image. Works for both 4.2-inch (400x300) and 2.9-inch (296x128)
// 4-color devices.
//
// Required libraries: M5Unified, PN5180-Library, ezsignNFC, miniLZO
// (See HelloEzsign.ino for installation details.)
//
// REMINDER: place minilzo.c, minilzo.h, lzoconf.h, lzodefs.h (from
// https://github.com/yuhaoth/minilzo ) into this sketch folder.

#include <M5Unified.h>
#include <ezsignNFC.h>

// Default M5AtomS3 pins. Override by passing an EzsignPins{...}.
EzsignDevice ez;

static void logger(const char* msg) {
  Serial.println(msg);
}

// Build a simple test image at the device's actual resolution.
// Layout:
//   - Black border, 4-pixel thick
//   - Inside: top half white with a centered "+" cross (red),
//     bottom half: 4 vertical bars (Black/White/Yellow/Red palette test)
static void buildImage(uint8_t* indices, int W, int H) {
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      uint8_t c = COLOR_WHITE;

      // Border
      if (x < 4 || x >= W - 4 || y < 4 || y >= H - 4) {
        c = COLOR_BLACK;
      }
      // Bottom half: 4 color bars
      else if (y >= H/2) {
        int barW = W / 4;
        int idx = (x) / barW;
        if (idx > 3) idx = 3;
        switch (idx) {
          case 0: c = COLOR_BLACK;  break;
          case 1: c = COLOR_WHITE;  break;
          case 2: c = COLOR_YELLOW; break;
          case 3: c = COLOR_RED;    break;
        }
      }
      // Top half: red "+" cross at center
      else {
        int cx = W / 2, cy = H / 4;     // center of upper half
        int armLen = (H/4 < W/2 ? H/4 : W/2) - 8;
        if (armLen < 4) armLen = 4;
        bool inHorz = (abs(x - cx) <= armLen) && (abs(y - cy) <= 3);
        bool inVert = (abs(y - cy) <= armLen) && (abs(x - cx) <= 3);
        if (inHorz || inVert) {
          c = COLOR_RED;
        }
      }
      indices[(size_t)y * W + x] = c;
    }
  }
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== AutoSize ===");

  ez.setLogger(logger);
  if (!ez.begin()) {
    Serial.printf("begin failed: %s\n",
                  EzsignDevice::errorString(ez.lastError()));
    while (1) delay(1000);
  }

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.println("Place card");
  M5.Display.println("then press");
}

void writeImage() {
  // Step 1: detect + auth
  if (!ez.detect()) {
    Serial.printf("detect: %s\n", EzsignDevice::errorString(ez.lastError()));
    return;
  }
  if (!ez.authenticate()) {
    Serial.printf("auth: %s\n", EzsignDevice::errorString(ez.lastError()));
    return;
  }

  // Step 2: read device info to learn the resolution
  EzsignDeviceInfo info;
  if (!ez.getDeviceInfo(info) || !info.valid) {
    Serial.println("getDeviceInfo failed; skipping");
    return;
  }
  Serial.printf("Device: name='%s', %d x %d, %d colors, orient=0x%02X\n",
                info.name, info.width, info.height, info.numColors,
                info.imageOrientation);
  Serial.printf("UID: %02X %02X %02X %02X\n",
                info.uid[0], info.uid[1], info.uid[2], info.uid[3]);

  // Step 3: allocate an indices buffer at the device's native size
  size_t pixels = (size_t)info.width * info.height;
  uint8_t* indices = (uint8_t*) malloc(pixels);
  if (!indices) {
    Serial.println("OOM");
    return;
  }
  buildImage(indices, info.width, info.height);

  // Step 4: send. Try flipH=true first (works for most batches).
  if (!ez.sendImageIndices(indices, info.width, info.height, /*flipH=*/true)) {
    Serial.printf("send: %s\n", EzsignDevice::errorString(ez.lastError()));
    free(indices);
    return;
  }
  free(indices);

  // Step 5: refresh + wait
  if (!ez.refreshDisplay(false)) {
    Serial.printf("refresh: %s\n", EzsignDevice::errorString(ez.lastError()));
    return;
  }
  Serial.println("Refreshing display ...");
  if (!ez.waitForRefresh()) {
    Serial.printf("wait: %s (image may still complete)\n",
                  EzsignDevice::errorString(ez.lastError()));
  } else {
    Serial.println("DONE");
  }
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    M5.Display.fillScreen(TFT_NAVY);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.println("Writing...");
    M5.Display.println("Hold still!");
    writeImage();
    bool ok = (ez.lastError() == EZSIGN_OK ||
               ez.lastError() == EZSIGN_TIMEOUT);
    M5.Display.fillScreen(ok ? TFT_DARKGREEN : TFT_RED);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.println(ok ? "OK" : "FAIL");
  }
  delay(20);
}
