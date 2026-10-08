#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==========================================================
// GLOBAL CONFIGURATION
// ==========================================================

namespace config {

// ==========================================================
// HARDWARE PINS
// ==========================================================
namespace pins {
  // Navigation buttons:
  //   Increment button -> InputType::ButtonIncrement
  //   Decrement button -> InputType::ButtonDecrement
  //   (RotateLeft/RotateRight are serial debug keys only — see
  //    InputService::PollSerialDebug() — and are never posted by a pin.)
  constexpr uint8_t ButtonIncrement = 26;
  constexpr uint8_t ButtonDecrement = 25;

  // Menu buttons (INPUT_PULLUP, connect to GND).
  constexpr uint8_t ButtonEnter = 27;
  constexpr uint8_t ButtonBack = 14;


  // OLED display (I2C: SDA / SCL).
  constexpr uint8_t OledSda = 21;
  constexpr uint8_t OledScl = 22;
}

// ==========================================================
// OLED DISPLAY
// ==========================================================
namespace display {
  // Preferred I2C address of the SSD1306 (7-bit form).
  constexpr uint8_t I2cAddr = 0x3C;
  // Fallback for clones that use the alternate address.
  constexpr uint8_t I2cAddrFallback = 0x3D;
}

// ==========================================================
// LYRICS
// ==========================================================
constexpr const char* kLyricsPathA = "/lyrics/Tumi.txt";
constexpr const char* kLyricsPathB = "/lyrics/Closer.txt";

}  // namespace config

#endif