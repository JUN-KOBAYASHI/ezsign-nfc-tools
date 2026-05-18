// SimpleImage.ino
//
// Example: send a procedurally-generated RGB image to the e-paper using
// ezsignNFC's sendImageRGB() (no dithering -- nearest-neighbor quantize).
// For dithered images, use sendImageIndices() with your own dither.
//
// REMINDER: place minilzo.c, minilzo.h, lzoconf.h, lzodefs.h (from
// https://github.com/yuhaoth/minilzo ) into this sketch folder.
//
// See HelloEzsign.ino for full wiring/library prerequisites.

#include <M5Unified.h>
#include <ezsignNFC.h>

static const int W = 400;
static const int H = 300;

// Default M5AtomS3 pins. To override, pass an EzsignPins{...} to the
// constructor.
EzsignDevice ez;

static void logger(const char* m) { Serial.println(m); }

// Generate an RGB image: red / yellow / black tricolor flag with white
// margin and a small black solid square.
static void makeRGB(uint8_t* rgb) {
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      uint8_t r = 255, g = 255, b = 255;   // default white
      // 8 px white margin around everything
      if (y >= 8 && y < H - 8 && x >= 8 && x < W - 8) {
        // Three horizontal bands inside the margin
        int yi = y - 8;
        int bandH = (H - 16) / 3;
        int band = yi / bandH;
        if      (band == 0) { r = 255; g =   0; b =   0; }   // red
        else if (band == 1) { r = 255; g = 255; b =   0; }   // yellow
        else                { r =   0; g =   0; b =   0; }   // black
      }
      // Small black square in the center
      if (x >= W/2 - 30 && x < W/2 + 30 && y >= H/2 - 30 && y < H/2 + 30) {
        r = 0; g = 0; b = 0;
      }
      size_t i = (size_t)y * W + x;
      rgb[i*3+0] = r;
      rgb[i*3+1] = g;
      rgb[i*3+2] = b;
    }
  }
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== SimpleImage ===");

  ez.setLogger(logger);
  if (!ez.begin()) {
    Serial.println("begin failed");
    while (1) delay(1000);
  }
}

void loop() {
  M5.update();
  if (!M5.BtnA.wasPressed()) { delay(20); return; }

  size_t bytes = (size_t)W * H * 3;
  uint8_t* rgb = (uint8_t*) malloc(bytes);
  if (!rgb) { Serial.println("OOM"); return; }
  makeRGB(rgb);

  if (ez.detect() && ez.authenticate()) {
    if (ez.sendImageRGB(rgb, W, H, /*flipH=*/true)) {
      ez.refreshDisplay(false);
      ez.waitForRefresh();
      Serial.println("Done.");
    } else {
      Serial.printf("send fail: %s\n",
                    EzsignDevice::errorString(ez.lastError()));
    }
  } else {
    Serial.printf("detect/auth fail: %s\n",
                  EzsignDevice::errorString(ez.lastError()));
  }
  free(rgb);
}
