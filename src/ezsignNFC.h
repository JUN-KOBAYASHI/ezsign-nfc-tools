// ezsignNFC.h
//
// Arduino library for writing images to NFC-rewritable e-paper name badges.
//
// Supported hardware:
//   - 4.2-inch 4-color (Black/White/Yellow/Red), 400 x 300, 2 bpp
//   - 2.9-inch 4-color (Black/White/Yellow/Red), 296 x 128, 2 bpp
//   - AID D2 76 00 00 85 01 01
//   - Tested with PN5180 NFC reader on M5AtomS3 (ESP32-S3)
//
// Image-orientation note:
//   The card's native pixel layout depends on the model, and the on-device
//   firmware can vary between batches. Always call getDeviceInfo() and
//   try `flipH=true` first; if the image comes out mirrored, switch to
//   `flipH=false`.
//
// Required libraries:
//   - PN5180 (ATrappmann/PN5180-Library, install from GitHub ZIP:
//             https://github.com/ATrappmann/PN5180-Library )
//   - miniLZO 2.10 (place minilzo.c, minilzo.h, lzoconf.h, lzodefs.h
//                   next to your .ino file -- see README.md)
//
// Basic usage (auto-detects size):
//   #include <ezsignNFC.h>
//   PN5180ISO14443 nfc(NSS_PIN, BUSY_PIN, RST_PIN);
//   EzsignDevice ez(nfc);
//
//   void setup() {
//     SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, NSS_PIN);
//     ez.begin();
//   }
//
//   void writeImage() {
//     if (!ez.detect() || !ez.authenticate()) return;
//     EzsignDeviceInfo info;
//     ez.getDeviceInfo(info);  // info.width, info.height, info.numColors
//
//     // Allocate and fill an indices buffer with palette indices 0..3
//     uint8_t* indices = (uint8_t*) malloc(info.width * info.height);
//     // ... fill indices ...
//
//     ez.sendImageIndices(indices, info.width, info.height, /*flipH=*/true);
//     ez.refreshDisplay(/*blocking=*/false);
//     ez.waitForRefresh();
//     free(indices);
//   }
#pragma once
#include <Arduino.h>
#include <PN5180.h>
#include <PN5180ISO14443.h>

// ===== Public types =====

enum EzsignError : uint8_t {
  EZSIGN_OK                = 0,
  EZSIGN_NOT_INITIALIZED   = 1,
  EZSIGN_NO_CARD           = 2,
  EZSIGN_NOT_14443_4       = 3,
  EZSIGN_RATS_FAILED       = 4,
  EZSIGN_AUTH_FAILED       = 5,
  EZSIGN_INFO_FAILED       = 6,
  EZSIGN_SEND_FAILED       = 7,
  EZSIGN_REFRESH_FAILED    = 8,
  EZSIGN_OVERSIZED_BLOCK   = 9,
  EZSIGN_OUT_OF_MEMORY     = 10,
  EZSIGN_LZO_FAILED        = 11,
  EZSIGN_TIMEOUT           = 12,
};

// Behavior when a compressed block exceeds the single-fragment limit.
// Multi-fragment transmission IS now supported via ISO 14443-4 I-block
// chaining (see PROTOCOL.md §5.1), so blocks much larger than 250 bytes
// just take more fragments but still succeed. The choices below are kept
// for backward compatibility and as a defensive fallback if a card
// rejects multi-fragment blocks for some reason:
enum EzsignOversizedBehavior : uint8_t {
  EZSIGN_BLOCK_FAIL            = 0, // Stop with EZSIGN_OVERSIZED_BLOCK error
  EZSIGN_BLOCK_SUBSTITUTE_WHITE= 1, // Send a white block in place of oversized one
};

// 4-color palette indices (used by sendImageIndices).
enum EzsignColor : uint8_t {
  COLOR_BLACK  = 0,
  COLOR_WHITE  = 1,
  COLOR_YELLOW = 2,
  COLOR_RED    = 3,
};

// Device information (decoded from the 0x00D1 GET INFO command response).
struct EzsignDeviceInfo {
  uint16_t width;            // pixels
  uint16_t height;           // pixels
  uint8_t  bitsPerPixel;     // 1 or 2
  uint8_t  numColors;        // 2 or 4
  uint8_t  imageOrientation; // 0x00 = rotate 90, 0x01 = as-is (estimated)
  char     name[11];         // null-terminated device name
  uint8_t  uid[4];           // device UID (big-endian)
  bool     valid;            // false if response was malformed
};

// Logger callback. Set with setLogger() to capture progress / errors.
typedef void (*EzsignLogger)(const char* msg);

// Default pin assignment for M5AtomS3 + PN5180.
// Override any of these by passing your own EzsignPins to EzsignDevice.
struct EzsignPins {
  int8_t rst  = 38;
  int8_t nss  = 5;
  int8_t mosi = 8;
  int8_t miso = 6;
  int8_t sck  = 7;
  int8_t busy = 2;
};

// Convenience: pin set for common M5Stack boards. Use these instead of
// hardcoding numbers in your sketch.
namespace EzsignPinPresets {
  // M5AtomS3 (ESP32-S3, default in the library).
  static inline EzsignPins atomS3() {
    EzsignPins p;
    p.rst = 38; p.nss = 5; p.mosi = 8; p.miso = 6; p.sck = 7; p.busy = 2;
    return p;
  }
}

// ===== Main class =====

class EzsignDevice {
public:
  // -------- Convenience constructors (own the PN5180 instance) --------
  //
  // Use these if you don't already have a PN5180ISO14443 in your sketch.
  // Pass an EzsignPins to override any pin; defaults are for M5AtomS3.

  // Default-pin constructor: uses EzsignPins{} (M5AtomS3 layout).
  EzsignDevice();

  // Pin-override constructor.
  explicit EzsignDevice(const EzsignPins& pins);

  // -------- Advanced constructor (bring your own PN5180 instance) -----
  //
  // Pass a PN5180ISO14443 you already constructed. You're responsible for
  // calling SPI.begin() before begin() in this mode.
  explicit EzsignDevice(PN5180ISO14443& nfc);

  ~EzsignDevice();

  // Allocate working buffers and initialize the PN5180. Call once from setup().
  // - In convenience-constructor mode this also calls SPI.begin() for you
  //   using the pin set you passed.
  // - In bring-your-own-NFC mode you must call SPI.begin() yourself first.
  // Returns false if memory allocation or PN5180 init fails.
  bool begin();

  // Free working buffers. Call from end-of-life cleanup if needed.
  void end();

  // Activate any e-paper badge present on the antenna (REQA -> RATS).
  // Must be called before authenticate / sendImage / refreshDisplay.
  // Returns true on success. Updates lastError() on failure.
  bool detect();

  // Send the AUTH APDU to the active card. Required before image transfer.
  bool authenticate();

  // Read device information (size, color depth, name, UID).
  bool getDeviceInfo(EzsignDeviceInfo& out);

  // Send an image given as a width*height array of palette indices (0..3).
  // The buffer must hold exactly width*height bytes; each byte is a color index.
  // `flipH` flips horizontally before transmission (set true for 4.2-inch
  // 4-color devices that mirror).
  // Performs LZO compression, splits into 2000-byte raw blocks, and sends
  // each as one or more 250-byte fragments using ISO 14443-4 I-block chaining.
  // Returns false on any failure.
  bool sendImageIndices(const uint8_t* indices, int width, int height,
                        bool flipH);

  // Send an image given as RGB bytes (3 bytes per pixel). The library nearest-
  // neighbor quantizes to the 4-color palette WITHOUT dithering. If you want
  // dithering, do it yourself and call sendImageIndices() instead.
  bool sendImageRGB(const uint8_t* rgb, int width, int height, bool flipH);

  // Send the REFRESH command.
  //   blocking=true  : the card blocks the response until the refresh
  //                    completes (can take 30+ s, may exceed NFC timeouts)
  //   blocking=false : returns immediately; use waitForRefresh() to poll
  bool refreshDisplay(bool blocking);

  // Poll the refresh-status command until the card reports completion or
  // `maxSeconds` elapses. Resilient: if the NFC link drops mid-poll, this
  // automatically re-activates the session and continues.
  // Returns true when the refresh completed; false on timeout (image was
  // still sent successfully, the card may finish on its own).
  bool waitForRefresh(uint32_t intervalMs = 500, uint32_t maxSeconds = 90);

  // Optional knobs / accessors -----------------------------------------------

  // What to do if compression of a 2000-byte block somehow fails completely.
  // With ISO 14443-4 I-block chaining now in use, blocks larger than 250
  // bytes are sent as multiple fragments (no longer "oversized") -- so this
  // setting is rarely meaningful. Kept for backward compatibility.
  // Default: EZSIGN_BLOCK_SUBSTITUTE_WHITE.
  void setOversizedBlockBehavior(EzsignOversizedBehavior b) { _oversized = b; }

  // Set a logger callback; pass nullptr to disable.
  void setLogger(EzsignLogger l) { _log = l; }

  // Most-recent error code (cleared on successful operation).
  EzsignError lastError() const { return _lastErr; }

  // Human-readable string for an error code.
  static const char* errorString(EzsignError e);

  // Direct access to the underlying PN5180ISO14443 instance.
  PN5180ISO14443& nfc() { return *_nfc; }

private:
  PN5180ISO14443* _nfc       = nullptr;
  bool            _ownsNfc   = false;     // true if we new'd _nfc ourselves
  EzsignPins      _pins;                  // valid when _ownsNfc is true
  EzsignError     _lastErr   = EZSIGN_OK;
  EzsignLogger    _log       = nullptr;
  bool            _started   = false;
  uint8_t         _blockNum  = 0;         // ISO 14443-4 I-block number toggle
  EzsignOversizedBehavior _oversized = EZSIGN_BLOCK_SUBSTITUTE_WHITE;

  // Heap-allocated buffers (only allocated in begin()).
  uint8_t* _packed     = nullptr;         // packed 2bpp/1bpp image
  uint8_t* _cmpBuf     = nullptr;         // LZO output scratch
  uint8_t* _lzoWork    = nullptr;         // LZO compress workspace (~64 KiB)
  size_t   _packedSize = 0;

  // Internal helpers --------------------------------------------------------
  void log(const char* msg);
  void logf(const char* fmt, ...);

  bool sendAPDU(const uint8_t* apdu, uint16_t len,
                uint8_t* resp, uint16_t respCap, uint16_t* respLen,
                uint16_t* sw);

  bool transceiveISO14443_4(const uint8_t* apdu, uint16_t apduLen,
                            uint8_t* resp, uint16_t respCap, uint16_t* respLen);

  // Single-frame I-block / S-block exchange used by transceiveISO14443_4.
  // Handles S(WTX) automatically; returns when the next I-block or R-block
  // arrives.
  bool iso14443_4_exchange(uint8_t* txBuf, uint16_t txLen,
                           uint8_t* rx, uint16_t rxCap, uint16_t* rxLen,
                           uint32_t timeoutMs);

  bool sendRATS();
  bool reactivate();          // REQA + RATS only (no AUTH) for polling recovery

  bool epdSendFragment(uint8_t blockIdx, uint8_t fragIdx,
                       const uint8_t* data, uint8_t len, bool last);
  bool pollStatus(bool* done);

  // Pack 4-color rows: out gets ceil(w*2/8)=w/4 bytes per row, packed
  // right-to-left within a byte (per protocol).
  void packRow4Color(const uint8_t* rowPixels, uint8_t* dst, int width);

  // RGB -> 4-color palette index (nearest neighbor)
  static uint8_t quantizeRGB(uint8_t r, uint8_t g, uint8_t b);

  // PN5180 register-level helpers
  bool waitForRxIrq(uint32_t timeoutMs);
  uint16_t rxBytesAvailable();
  bool rfSendAndReceive(const uint8_t* tx, uint16_t txLen,
                        uint8_t validBitsLastByte,
                        uint8_t* out, uint16_t outCap, uint16_t* outLen,
                        uint32_t timeoutMs);
  void setCRC(bool on);
};

// =====================================================================
// INLINE IMPLEMENTATION
//
// All implementation lives in this header so that user sketches can
// place minilzo.c/.h alongside the .ino file. Arduino's compile model
// doesn't let a library .cpp see headers in the sketch folder, but
// when the implementation lives in the header the user sketch is
// the translation unit doing the compilation -- so it does see them.
//
// Place these four files next to your .ino:
//   minilzo.c, minilzo.h, lzoconf.h, lzodefs.h
// =====================================================================

#include <SPI.h>
extern "C" {
  // miniLZO is placed in the sketch folder by the user. We use quoted
  // include so the sketch directory is searched first.
  #include "minilzo.h"
}

#include <stdarg.h>
#include <string.h>

// PN5180 register addresses (subset from PN5180-Library)
static const uint8_t REG_IRQ_STATUS    = 0x02;
static const uint8_t REG_RX_STATUS     = 0x13;
static const uint8_t REG_CRC_RX_CONFIG = 0x12;
static const uint8_t REG_CRC_TX_CONFIG = 0x19;

// IRQ bits
static const uint32_t IRQ_RX           = (1UL << 0);
static const uint32_t IRQ_RX_TIMEOUT   = (1UL << 8);
static const uint32_t IRQ_ERR          = (1UL << 10);

// Block / fragment sizes
//
// Per the NFC e-paper protocol spec, each LZO-compressed 2000-byte block is
// split into "fragments" of up to 250 bytes (the application-layer fragment
// size). We honour that; the resulting 257-byte APDU (5 header + 2 idx + 250
// payload) does not fit in a single ISO 14443-4 frame at FSC=256, but the
// long-APDU path in transceiveISO14443_4() splits it into multiple I-blocks
// using ISO-DEP I-block chaining, which works on these tags.
//
// The card itself uses blockIndex * 2000 as the implicit byte offset into the
// image, so we must always send fixed 2000-byte blocks (last block may be
// smaller).
static const size_t EZ_BLOCK_SIZE    = 2000;
static const size_t EZ_FRAGMENT_SIZE = 250;
// LZO worst-case buffer: in + in/16 + 64 + 3
static const size_t EZ_CMP_BUF_SIZE  = EZ_BLOCK_SIZE + EZ_BLOCK_SIZE / 16 + 64 + 3;

// =========================================================================

// =========================================================================
// Construction / destruction
// =========================================================================

// Default-pin (M5AtomS3 layout) convenience constructor
inline EzsignDevice::EzsignDevice() {
  _pins = EzsignPins();  // defaults
  _nfc = new PN5180ISO14443(_pins.nss, _pins.busy, _pins.rst);
  _ownsNfc = true;
}

// Custom-pin convenience constructor
inline EzsignDevice::EzsignDevice(const EzsignPins& pins) : _pins(pins) {
  _nfc = new PN5180ISO14443(_pins.nss, _pins.busy, _pins.rst);
  _ownsNfc = true;
}

// Bring-your-own-PN5180 constructor (advanced)
inline EzsignDevice::EzsignDevice(PN5180ISO14443& nfc) {
  _nfc = &nfc;
  _ownsNfc = false;
}

inline EzsignDevice::~EzsignDevice() {
  end();
  if (_ownsNfc && _nfc) {
    delete _nfc;
    _nfc = nullptr;
  }
}

inline bool EzsignDevice::begin() {
  if (_started) return true;
  _lastErr = EZSIGN_OK;
  if (!_nfc) { _lastErr = EZSIGN_NOT_INITIALIZED; return false; }

  // Allocate working buffers from heap (LZO_WORK is ~64 KiB on 32-bit)
  _lzoWork = (uint8_t*) malloc(LZO1X_1_MEM_COMPRESS);
  _cmpBuf  = (uint8_t*) malloc(EZ_CMP_BUF_SIZE);
  if (!_lzoWork || !_cmpBuf) {
    free(_lzoWork); _lzoWork = nullptr;
    free(_cmpBuf);  _cmpBuf  = nullptr;
    _lastErr = EZSIGN_OUT_OF_MEMORY;
    return false;
  }

  if (lzo_init() != LZO_E_OK) {
    free(_lzoWork); _lzoWork = nullptr;
    free(_cmpBuf);  _cmpBuf  = nullptr;
    _lastErr = EZSIGN_LZO_FAILED;
    return false;
  }

  // Convenience-constructor mode: bring up SPI ourselves with the saved pins.
  // (Advanced mode: the user already called SPI.begin() before this.)
  if (_ownsNfc) {
    SPI.begin(_pins.sck, _pins.miso, _pins.mosi, _pins.nss);
  }

  _nfc->begin();
  _nfc->reset();
  _started = true;
  return true;
}

inline void EzsignDevice::end() {
  free(_lzoWork);    _lzoWork = nullptr;
  free(_cmpBuf);     _cmpBuf  = nullptr;
  free(_packed);     _packed  = nullptr;
  _packedSize = 0;
  _started = false;
}

// =========================================================================
// Logging
// =========================================================================

inline void EzsignDevice::log(const char* msg) {
  if (_log) _log(msg);
}

inline void EzsignDevice::logf(const char* fmt, ...) {
  if (!_log) return;
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  _log(buf);
}

// =========================================================================
// Error strings
// =========================================================================

inline const char* EzsignDevice::errorString(EzsignError e) {
  switch (e) {
    case EZSIGN_OK:               return "OK";
    case EZSIGN_NOT_INITIALIZED:  return "begin() not called";
    case EZSIGN_NO_CARD:          return "no card detected";
    case EZSIGN_NOT_14443_4:      return "card is not ISO14443-4 capable";
    case EZSIGN_RATS_FAILED:      return "RATS failed";
    case EZSIGN_AUTH_FAILED:      return "authentication APDU failed";
    case EZSIGN_INFO_FAILED:      return "GET INFO APDU failed";
    case EZSIGN_SEND_FAILED:      return "image fragment send failed";
    case EZSIGN_REFRESH_FAILED:   return "REFRESH command failed";
    case EZSIGN_OVERSIZED_BLOCK:  return "compressed block exceeds fragment limit";
    case EZSIGN_OUT_OF_MEMORY:    return "heap allocation failed";
    case EZSIGN_LZO_FAILED:       return "LZO init/compression failed";
    case EZSIGN_TIMEOUT:          return "timed out waiting for response";
  }
  return "unknown error";
}

// =========================================================================
// PN5180 register helpers
// =========================================================================

inline bool EzsignDevice::waitForRxIrq(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    uint32_t s = _nfc->getIRQStatus();
    if (s & IRQ_RX) return true;
    if (s & IRQ_RX_TIMEOUT) {
      logf("[ezsign] RX_TIMEOUT IRQ after %lu ms", (unsigned long)(millis() - t0));
      return false;
    }
    if (s & IRQ_ERR) {
      logf("[ezsign] ERR IRQ after %lu ms", (unsigned long)(millis() - t0));
      return false;
    }
    delay(1);
  }
  logf("[ezsign] software RX timeout %lu ms", (unsigned long)timeoutMs);
  return false;
}

inline uint16_t EzsignDevice::rxBytesAvailable() {
  uint32_t v = 0;
  _nfc->readRegister(REG_RX_STATUS, &v);
  return (uint16_t)(v & 0x1FF);
}

inline bool EzsignDevice::rfSendAndReceive(const uint8_t* tx, uint16_t txLen,
                                    uint8_t validBitsLastByte,
                                    uint8_t* out, uint16_t outCap,
                                    uint16_t* outLen, uint32_t timeoutMs) {
  _nfc->clearIRQStatus(0xFFFFFFFF);
  if (!_nfc->sendData((uint8_t*)tx, (int)txLen, validBitsLastByte)) {
    log("[ezsign] sendData failed");
    return false;
  }
  if (!waitForRxIrq(timeoutMs)) return false;
  uint16_t n = rxBytesAvailable();
  if (n == 0) return false;
  if (n > outCap) n = outCap;
  if (!_nfc->readData(n, out)) return false;
  if (outLen) *outLen = n;
  return true;
}

inline void EzsignDevice::setCRC(bool on) {
  if (on) {
    _nfc->writeRegisterWithOrMask(REG_CRC_TX_CONFIG, 0x00000001);
    _nfc->writeRegisterWithOrMask(REG_CRC_RX_CONFIG, 0x00000001);
  } else {
    _nfc->writeRegisterWithAndMask(REG_CRC_TX_CONFIG, 0xFFFFFFFE);
    _nfc->writeRegisterWithAndMask(REG_CRC_RX_CONFIG, 0xFFFFFFFE);
  }
}

// =========================================================================
// ISO 14443-4 I-block transceive (with I-block chaining for long APDUs)
// =========================================================================
//
// ATrappmann's PN5180::sendData() can only send one RF frame at a time. With
// FSC=256 reported by these e-paper tags, that means the APDU + ISO 14443-4
// PCB+CRC overhead must fit in 256 bytes per frame.
//
// Spec-compliant 250-byte fragment APDUs are 5 (CLA INS P1 P2 Lc) + 2
// (block/fragment idx) + 250 = 257 bytes long, which would overflow a single
// frame. The fix is to use ISO 14443-4 *I-block chaining*: split the APDU
// into multiple I-blocks, set the chaining bit (PCB bit 4 = 0x10) on every
// non-final block, expect an R(ACK) for each chained block, then send the
// final non-chained I-block, and read the actual APDU response from the
// I-block that comes back.
//
// Earlier versions of this library tried to send 250-byte fragments as a
// single I-block, which silently failed at the PN5180 level (overflowing the
// frame buffer) and surfaced as SW=6992 from the card. With chaining, the
// official 250-byte fragment size from the protocol spec works as intended.

// Internal helper: send one already-formed I-block (or S-block reply) and
// receive the next inbound block. Handles WTX (S(WTX)) automatically.
inline bool EzsignDevice::iso14443_4_exchange(uint8_t* txBuf, uint16_t txLen,
                                       uint8_t* rx, uint16_t rxCap,
                                       uint16_t* rxLen, uint32_t timeoutMs) {
  for (int safety = 0; safety < 12; ++safety) {
    *rxLen = 0;
    if (!rfSendAndReceive(txBuf, txLen, 0, rx, rxCap, rxLen, timeoutMs)) {
      return false;
    }
    if (*rxLen < 1) return false;

    uint8_t pcb = rx[0];
    uint8_t blockType = pcb & 0xC0;

    if (blockType == 0xC0) {
      // S-block: WTX or DESELECT
      bool isWTX = ((pcb & 0x30) == 0x30);
      if (isWTX) {
        uint16_t wtxOff = 1;
        if (pcb & 0x08) wtxOff++;
        uint8_t wtx = (*rxLen > wtxOff) ? rx[wtxOff] : 0x01;
        txBuf[0] = pcb;
        uint16_t i = 1;
        if (pcb & 0x08) txBuf[i++] = rx[1];
        txBuf[i++] = wtx;
        txLen = i;
        continue;
      }
      logf("[ezsign] S-block (non-WTX) PCB %02X", pcb);
      return false;
    }

    // I-block or R-block: caller decides what to do.
    return true;
  }
  log("[ezsign] exchange safety counter exceeded");
  return false;
}

inline bool EzsignDevice::transceiveISO14443_4(const uint8_t* apdu, uint16_t apduLen,
                                        uint8_t* resp, uint16_t respCap,
                                        uint16_t* respLen) {
  setCRC(true);

  // Per-frame APDU payload limit. With FSC=256, a single I-block can carry
  // up to ~250 bytes of user data (1 PCB + 250 INF + 2 CRC + headroom).
  static const uint16_t INF_CHUNK = 250;

  uint8_t txBuf[300];
  uint8_t rx[300];
  uint16_t rxLen = 0;
  const uint32_t timeoutMs = 25000;  // REFRESH-blocking responses can be many sec.

  // ------- Short APDU (single I-block) -------
  if (apduLen <= INF_CHUNK) {
    uint16_t txLen = 0;
    txBuf[txLen++] = 0x02 | (_blockNum & 0x01);
    if (apduLen + txLen > sizeof(txBuf)) return false;
    memcpy(&txBuf[txLen], apdu, apduLen);
    txLen += apduLen;

    for (int safety = 0; safety < 8; ++safety) {
      if (!iso14443_4_exchange(txBuf, txLen, rx, sizeof(rx), &rxLen, timeoutMs)) {
        return false;
      }
      uint8_t pcb = rx[0];
      uint8_t blockType = pcb & 0xC0;

      if (blockType == 0x80) {
        // R-block (ACK/NAK) — resend our I-block
        txBuf[0] = 0x02 | (_blockNum & 0x01);
        memcpy(&txBuf[1], apdu, apduLen);
        txLen = apduLen + 1;
        continue;
      }
      if (blockType == 0x00) {
        // I-block response
        uint16_t off = 1;
        if (pcb & 0x08) off++;
        if (pcb & 0x04) off++;
        uint16_t dataLen = (rxLen >= off) ? (rxLen - off) : 0;
        if (resp && respCap >= dataLen) {
          memcpy(resp, &rx[off], dataLen);
          if (respLen) *respLen = dataLen;
        } else if (respLen) {
          *respLen = 0;
        }
        _blockNum ^= 0x01;
        if (pcb & 0x10) {
          log("[ezsign] response chaining not supported");
          return false;
        }
        return true;
      }
      logf("[ezsign] unexpected PCB %02X", pcb);
      return false;
    }
    return false;
  }

  // ------- Long APDU (I-block chaining required) -------
  logf("[ezsign] chained APDU len=%u chunk=%u", apduLen, INF_CHUNK);

  uint16_t off = 0;
  while (off < apduLen) {
    uint16_t remain = apduLen - off;
    uint16_t chunk = (remain > INF_CHUNK) ? INF_CHUNK : remain;
    bool more = (off + chunk < apduLen);

    uint16_t txLen = 0;
    txBuf[txLen++] = (uint8_t)(0x02 | (_blockNum & 0x01) | (more ? 0x10 : 0x00));
    if (txLen + chunk > sizeof(txBuf)) return false;
    memcpy(&txBuf[txLen], apdu + off, chunk);
    txLen += chunk;

    for (int safety = 0; safety < 8; ++safety) {
      if (!iso14443_4_exchange(txBuf, txLen, rx, sizeof(rx), &rxLen, timeoutMs)) {
        return false;
      }
      uint8_t pcb = rx[0];
      uint8_t blockType = pcb & 0xC0;

      if (more) {
        // For chained chunks we expect R(ACK) PCB=0xA2 / 0xA3
        if (blockType == 0x80) {
          _blockNum ^= 0x01;
          break;
        }
        logf("[ezsign] expected R-ACK during chain, got PCB=%02X", pcb);
        return false;
      }

      // Last chunk: card should reply with the final I-block (APDU response)
      if (blockType == 0x80) {
        // R-block instead of I-block: resend final chunk
        txLen = 0;
        txBuf[txLen++] = 0x02 | (_blockNum & 0x01);
        memcpy(&txBuf[txLen], apdu + off, chunk);
        txLen += chunk;
        continue;
      }
      if (blockType == 0x00) {
        uint16_t roff = 1;
        if (pcb & 0x08) roff++;
        if (pcb & 0x04) roff++;
        uint16_t dataLen = (rxLen >= roff) ? (rxLen - roff) : 0;
        if (resp && respCap >= dataLen) {
          memcpy(resp, &rx[roff], dataLen);
          if (respLen) *respLen = dataLen;
        } else if (respLen) {
          *respLen = 0;
        }
        _blockNum ^= 0x01;
        if (pcb & 0x10) {
          log("[ezsign] response chaining not supported");
          return false;
        }
        return true;
      }
      logf("[ezsign] unexpected final PCB %02X", pcb);
      return false;
    }

    off += chunk;
  }

  return false;
}

inline bool EzsignDevice::sendAPDU(const uint8_t* apdu, uint16_t len,
                            uint8_t* resp, uint16_t respCap,
                            uint16_t* respLen, uint16_t* sw) {
  uint8_t buf[300];
  uint16_t bufLen = 0;
  if (!transceiveISO14443_4(apdu, len, buf, sizeof(buf), &bufLen)) {
    if (sw) *sw = 0;
    return false;
  }
  if (bufLen < 2) {
    if (sw) *sw = 0;
    return false;
  }
  uint16_t sw_v = ((uint16_t)buf[bufLen - 2] << 8) | buf[bufLen - 1];
  if (sw) *sw = sw_v;
  uint16_t dataLen = bufLen - 2;
  if (resp && respCap >= dataLen) {
    memcpy(resp, buf, dataLen);
    if (respLen) *respLen = dataLen;
  } else if (respLen) {
    *respLen = 0;
  }
  return (sw_v == 0x9000);
}

// =========================================================================
// RATS / detect / reactivate
// =========================================================================

inline bool EzsignDevice::sendRATS() {
  setCRC(true);
  // RATS: 0xE0 | (FSDI<<4 | CID).  FSDI=8 -> FSD=256.
  uint8_t rats[2] = {0xE0, 0x80};
  uint8_t ats[32];
  uint16_t atsLen = 0;
  if (!rfSendAndReceive(rats, sizeof(rats), 0, ats, sizeof(ats), &atsLen, 100)) {
    return false;
  }
  return atsLen >= 1;
}

inline bool EzsignDevice::detect() {
  if (!_started) { _lastErr = EZSIGN_NOT_INITIALIZED; return false; }
  _blockNum = 0;
  _nfc->reset();
  _nfc->setupRF();

  uint8_t respBuf[10] = {0};
  uint8_t uidLen = _nfc->activateTypeA(respBuf, /*kind=*/0);
  if (uidLen == 0 || (respBuf[0] == 0xFF && respBuf[1] == 0xFF)) {
    _lastErr = EZSIGN_NO_CARD;
    return false;
  }
  if (!(respBuf[2] & 0x20)) {
    _lastErr = EZSIGN_NOT_14443_4;
    return false;
  }
  if (!sendRATS()) {
    _lastErr = EZSIGN_RATS_FAILED;
    return false;
  }
  _lastErr = EZSIGN_OK;
  return true;
}

inline bool EzsignDevice::reactivate() {
  // Re-establish the ISO14443-4 channel without re-authenticating
  // (so an in-progress display refresh isn't disturbed).
  _blockNum = 0;
  _nfc->reset();
  _nfc->setupRF();
  uint8_t respBuf[10] = {0};
  uint8_t uidLen = _nfc->activateTypeA(respBuf, 0);
  if (uidLen == 0 || (respBuf[0] == 0xFF && respBuf[1] == 0xFF)) return false;
  return sendRATS();
}

// =========================================================================
// Authentication / device info
// =========================================================================

inline bool EzsignDevice::authenticate() {
  if (!_started) { _lastErr = EZSIGN_NOT_INITIALIZED; return false; }
  static const uint8_t apdu[] = {0x00,0x20,0x00,0x01,0x04,0x20,0x09,0x12,0x10};
  uint16_t sw = 0;
  bool ok = sendAPDU(apdu, sizeof(apdu), nullptr, 0, nullptr, &sw);
  if (!ok) {
    _lastErr = EZSIGN_AUTH_FAILED;
    logf("[ezsign] AUTH SW=%04X", sw);
    return false;
  }
  _lastErr = EZSIGN_OK;
  return true;
}

inline bool EzsignDevice::getDeviceInfo(EzsignDeviceInfo& out) {
  if (!_started) { _lastErr = EZSIGN_NOT_INITIALIZED; return false; }
  out.valid = false;
  static const uint8_t apdu[] = {0x00,0xD1,0x00,0x00,0x00};
  uint8_t  resp[260];
  uint16_t respLen = 0, sw = 0;
  if (!sendAPDU(apdu, sizeof(apdu), resp, sizeof(resp), &respLen, &sw)) {
    _lastErr = EZSIGN_INFO_FAILED;
    return false;
  }

  // Parse TLV
  size_t i = 0;
  memset(&out, 0, sizeof(out));
  while (i + 2 <= respLen) {
    uint8_t tag = resp[i++];
    uint8_t len = resp[i++];
    if (i + len > respLen) break;
    const uint8_t* d = &resp[i];
    if (tag == 0xA0 && len >= 7) {
      // bytes: ?? numColors ?? heightInBits(BE) width(BE)
      uint8_t numColorsByte = d[1];
      out.bitsPerPixel = (numColorsByte == 0x07) ? 2 : 1;
      out.numColors = (numColorsByte == 0x07) ? 4 : 2;
      uint16_t heightInBits = ((uint16_t)d[3] << 8) | d[4];
      out.height = heightInBits / out.bitsPerPixel;
      out.width  = ((uint16_t)d[5] << 8) | d[6];
    } else if (tag == 0xA1 && len >= 1) {
      out.imageOrientation = d[0];
    } else if (tag == 0xC0 && len <= 10) {
      memcpy(out.name, d, len);
      out.name[len] = '\0';
    } else if (tag == 0xC1 && len >= 4) {
      memcpy(out.uid, d, 4);
    }
    i += len;
  }
  out.valid = (out.width > 0 && out.height > 0);
  _lastErr = EZSIGN_OK;
  return true;
}

// =========================================================================
// Image transfer
// =========================================================================

inline void EzsignDevice::packRow4Color(const uint8_t* rowPixels, uint8_t* dst,
                                 int width) {
  // 4-color: byte = p0 | (p1<<2) | (p2<<4) | (p3<<6)
  // Within each byte, p0 is the rightmost pixel of the 4-pixel group.
  int bytes = (width * 2 + 7) / 8;
  for (int xByte = 0; xByte < bytes; ++xByte) {
    uint8_t b = 0;
    for (int k = 0; k < 4; ++k) {
      int x = xByte * 4 + (3 - k);
      if (x < 0 || x >= width) continue;
      uint8_t p = rowPixels[x] & 0x03;
      b |= (uint8_t)(p << (k * 2));
    }
    dst[xByte] = b;
  }
}

inline uint8_t EzsignDevice::quantizeRGB(uint8_t r, uint8_t g, uint8_t b) {
  // 4-color palette: 0=black, 1=white, 2=yellow, 3=red
  static const uint8_t PAL[4][3] = {
    {  0,   0,   0},
    {255, 255, 255},
    {255, 255,   0},
    {255,   0,   0},
  };
  int best = 0;
  long bestD = 1L << 30;
  for (int i = 0; i < 4; ++i) {
    int dr = (int)r - (int)PAL[i][0];
    int dg = (int)g - (int)PAL[i][1];
    int db = (int)b - (int)PAL[i][2];
    long d = (long)dr*dr + (long)dg*dg + (long)db*db;
    if (d < bestD) { bestD = d; best = i; }
  }
  return (uint8_t)best;
}

inline bool EzsignDevice::epdSendFragment(uint8_t blockIdx, uint8_t fragIdx,
                                   const uint8_t* data, uint8_t len,
                                   bool last) {
  uint8_t apdu[5 + 2 + 250];
  uint8_t p2 = last ? 0x01 : 0x00;
  uint8_t lc = (uint8_t)(len + 2);
  apdu[0] = 0xF0;
  apdu[1] = 0xD3;
  apdu[2] = 0x00;
  apdu[3] = p2;
  apdu[4] = lc;
  apdu[5] = blockIdx;
  apdu[6] = fragIdx;
  memcpy(&apdu[7], data, len);
  uint16_t sw = 0;
  bool ok = sendAPDU(apdu, 5 + 2 + len, nullptr, 0, nullptr, &sw);
  if (!ok) {
    logf("[ezsign] FRAG %u-%u SW=%04X", blockIdx, fragIdx, sw);
  }
  return ok;
}

inline bool EzsignDevice::sendImageIndices(const uint8_t* indices, int width,
                                    int height, bool flipH) {
  if (!_started) { _lastErr = EZSIGN_NOT_INITIALIZED; return false; }
  if (!indices || width <= 0 || height <= 0) {
    _lastErr = EZSIGN_OUT_OF_MEMORY;
    return false;
  }
  // Allocate / reuse packedImage buffer
  size_t bytesPerRow = (width * 2 + 7) / 8;
  size_t total = bytesPerRow * height;
  if (_packed && _packedSize != total) {
    free(_packed); _packed = nullptr; _packedSize = 0;
  }
  if (!_packed) {
    _packed = (uint8_t*) malloc(total);
    if (!_packed) { _lastErr = EZSIGN_OUT_OF_MEMORY; return false; }
    _packedSize = total;
  }

  // Pack rows with optional horizontal flip
  uint8_t* row = (uint8_t*) malloc(width);
  if (!row) { _lastErr = EZSIGN_OUT_OF_MEMORY; return false; }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int srcX = flipH ? (width - 1 - x) : x;
      row[x] = indices[(size_t)y * width + srcX] & 0x03;
    }
    packRow4Color(row, _packed + (size_t)y * bytesPerRow, width);
  }
  free(row);

  // Helper: send a compressed block as N protocol fragments.
  // Each fragment is up to EZ_FRAGMENT_SIZE bytes; P2 is 0x00 for non-final
  // fragments and 0x01 for the final one. The resulting 257-byte APDU is
  // automatically split into multiple ISO 14443-4 I-blocks via the chaining
  // path inside transceiveISO14443_4().
  auto sendCompressedBlock = [&](uint8_t blockIdx, const uint8_t* cmp,
                                 size_t cmpLen) -> bool {
    if (cmpLen == 0) return false;
    size_t off = 0;
    uint8_t fragIdx = 0;
    while (off < cmpLen) {
      size_t remain = cmpLen - off;
      uint8_t fragLen = (remain > EZ_FRAGMENT_SIZE)
                          ? (uint8_t)EZ_FRAGMENT_SIZE : (uint8_t)remain;
      bool last = (off + fragLen >= cmpLen);
      bool ok = epdSendFragment(blockIdx, fragIdx, cmp + off, fragLen, last);
      if (!ok) {
        delay(120);
        ok = epdSendFragment(blockIdx, fragIdx, cmp + off, fragLen, last);
      }
      if (!ok) return false;
      off += fragLen;
      fragIdx++;
      delay(6);
    }
    return true;
  };

  // Send each 2000-byte block as one or more fragments of up to 250 B.
  // The protocol allows multi-fragment blocks; long APDUs are transmitted
  // using ISO-DEP I-block chaining (see transceiveISO14443_4()).
  uint8_t blockIdx = 0;
  for (size_t off = 0; off < total; off += EZ_BLOCK_SIZE) {
    size_t thisBlock = (EZ_BLOCK_SIZE < total - off)
                        ? EZ_BLOCK_SIZE : (total - off);

    lzo_uint outLen = 0;
    int r = lzo1x_1_compress(_packed + off, thisBlock,
                             _cmpBuf, &outLen, _lzoWork);
    if (r != LZO_E_OK) {
      logf("[ezsign] LZO err %d", r);
      _lastErr = EZSIGN_LZO_FAILED;
      return false;
    }

    size_t fragments = ((size_t)outLen + EZ_FRAGMENT_SIZE - 1) / EZ_FRAGMENT_SIZE;
    logf("[ezsign] BLOCK %u raw=%u cmp=%u frags=%u",
         blockIdx, (unsigned)thisBlock, (unsigned)outLen, (unsigned)fragments);

    if (!sendCompressedBlock(blockIdx, _cmpBuf, (size_t)outLen)) {
      logf("[ezsign] BLOCK %u send failed (cmp=%u, frags=%u)",
           blockIdx, (unsigned)outLen, (unsigned)fragments);
      _lastErr = EZSIGN_SEND_FAILED;
      return false;
    }

    blockIdx++;
    delay(10);
  }
  _lastErr = EZSIGN_OK;
  return true;
}

inline bool EzsignDevice::sendImageRGB(const uint8_t* rgb, int width, int height,
                                bool flipH) {
  if (!rgb || width <= 0 || height <= 0) {
    _lastErr = EZSIGN_OUT_OF_MEMORY;
    return false;
  }
  // Quantize into a temporary index buffer
  size_t n = (size_t)width * height;
  uint8_t* idx = (uint8_t*) malloc(n);
  if (!idx) { _lastErr = EZSIGN_OUT_OF_MEMORY; return false; }
  for (size_t i = 0; i < n; ++i) {
    uint8_t r = rgb[i*3+0], g = rgb[i*3+1], b = rgb[i*3+2];
    idx[i] = quantizeRGB(r, g, b);
  }
  bool ok = sendImageIndices(idx, width, height, flipH);
  free(idx);
  return ok;
}

// =========================================================================
// Refresh + polling
// =========================================================================

inline bool EzsignDevice::refreshDisplay(bool blocking) {
  if (!_started) { _lastErr = EZSIGN_NOT_INITIALIZED; return false; }
  uint8_t apdu[] = {0xF0, 0xD4, 0x85, (uint8_t)(blocking ? 0x00 : 0x80), 0x00};
  uint16_t sw = 0;
  bool ok = sendAPDU(apdu, sizeof(apdu), nullptr, 0, nullptr, &sw);
  if (!ok) {
    _lastErr = EZSIGN_REFRESH_FAILED;
    return false;
  }
  _lastErr = EZSIGN_OK;
  return true;
}

inline bool EzsignDevice::pollStatus(bool* done) {
  static const uint8_t apdu[] = {0xF0, 0xDE, 0x00, 0x00, 0x01};
  uint8_t  resp[8];
  uint16_t respLen = 0, sw = 0;
  if (!sendAPDU(apdu, sizeof(apdu), resp, sizeof(resp), &respLen, &sw)) {
    return false;
  }
  if (respLen >= 1) {
    *done = (resp[0] == 0x00);
  } else {
    *done = false;
  }
  return true;
}

inline bool EzsignDevice::waitForRefresh(uint32_t intervalMs, uint32_t maxSeconds) {
  if (!_started) { _lastErr = EZSIGN_NOT_INITIALIZED; return false; }
  uint32_t t0 = millis();
  int consecutiveFailures = 0;
  while ((millis() - t0) < maxSeconds * 1000UL) {
    delay(intervalMs);
    bool done = false;
    if (!pollStatus(&done)) {
      consecutiveFailures++;
      if (consecutiveFailures == 3) {
        log("[ezsign] 3 consecutive poll failures, attempting reactivation");
        if (reactivate()) consecutiveFailures = 0;
      } else if (consecutiveFailures >= 6) {
        _lastErr = EZSIGN_TIMEOUT;
        return false;
      }
      continue;
    }
    consecutiveFailures = 0;
    if (done) {
      _lastErr = EZSIGN_OK;
      return true;
    }
  }
  _lastErr = EZSIGN_TIMEOUT;
  return false;
}

