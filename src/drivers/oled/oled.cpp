#include "drivers/oled/oled.h"

#include <Wire.h>
#include <U8g2lib.h>

#include "common/config.h"

// ============================================================
// HARDWARE CONFIGURATION
// ============================================================

namespace {
constexpr uint16_t kOledWidth = 128;
constexpr uint16_t kOledHeight = 64;
}  // namespace

// ============================================================
// U8G2 OBJECT
// ============================================================

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE
);

// ============================================================
// GLOBAL OBJECT
// ============================================================

Oled oled;

// ============================================================
// INITIALIZATION
// ============================================================

bool Oled::Begin() {
  Wire.begin(config::pins::OledSda, config::pins::OledScl);

  // Real presence check: U8g2's begin() always reports success,
  // so probe the bus ourselves. Try the preferred address, then
  // the common fallback used by some clones.
  uint8_t addr = 0;
  if (ProbeAddress(config::display::I2cAddr)) {
    addr = config::display::I2cAddr;
  } else if (ProbeAddress(config::display::I2cAddrFallback)) {
    addr = config::display::I2cAddrFallback;
  }

  if (addr == 0) {
    Serial.println("OLED I2C: FAILED (check wiring / address / power)");
    return false;
  }

  Serial.print("OLED I2C: device at 0x");
  Serial.println(addr, HEX);

  // U8g2 stores the address in 8-bit form (0x3C shl 1 = 0x78).
  u8g2.setI2CAddress((uint8_t)(addr << 1));
  u8g2.begin();
  Clear();
  Update();
  return true;
}

// ============================================================
// I2C PROBE
// ============================================================

bool Oled::ProbeAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

// ============================================================
// SCREEN INFORMATION
// ============================================================

uint16_t Oled::Width() const {
  return kOledWidth;
}

uint16_t Oled::Height() const {
  return kOledHeight;
}

// ============================================================
// FRAMEBUFFER
// ============================================================

void Oled::Clear() {
  u8g2.clearBuffer();
}

void Oled::Update() {
  u8g2.sendBuffer();
}

// ============================================================
// FONT
// ============================================================

void Oled::SetFont(Font font) {
  switch (font) {
    case Font::Small:
      u8g2.setFont(u8g2_font_courR08_tr);
      break;
    case Font::Medium:
      u8g2.setFont(u8g2_font_courR10_tr);
      break;
    case Font::Large:
      u8g2.setFont(u8g2_font_courR18_tr);
      break;
    case Font::LargeBold:
      u8g2.setFont(u8g2_font_bubble_tn);
      break;
  }
}

// ============================================================
// TEXT
// ============================================================

void Oled::DrawText(int16_t x, int16_t y, const char* text) {
  u8g2.drawStr(x, y, text);
}

int16_t Oled::GetTextWidth(const char* text) {
  return u8g2.getStrWidth(text);
}

int16_t Oled::GetTextHeight() {
  return u8g2.getAscent() - u8g2.getDescent();
}

int16_t Oled::GetAscent() {
  return u8g2.getAscent();
}

// ============================================================
// GRAPHICS
// ============================================================

void Oled::DrawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
  u8g2.drawLine(x1, y1, x2, y2);
}

void Oled::DrawRect(int16_t x, int16_t y, int16_t width, int16_t height) {
  u8g2.drawFrame(x, y, width, height);
}

void Oled::FillRect(int16_t x, int16_t y, int16_t width, int16_t height) {
  u8g2.drawBox(x, y, width, height);
}

void Oled::SetDrawColor(uint8_t color) {
  u8g2.setDrawColor(color);
}
