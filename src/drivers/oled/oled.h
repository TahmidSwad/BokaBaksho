#ifndef OLED_H
#define OLED_H

#include <Arduino.h>

// ==========================================================
// OLED DRIVER
// ==========================================================
// Hardware abstraction for the SSD1306 OLED display using the
// U8g2 library over I2C. SDA/SCL pins and the preferred I2C
// address are configured in common/config.h. On startup the
// driver probes 0x3C, then 0x3D, reports the result on serial
// and returns false if no display is found.
// ==========================================================

class Oled {
public:
  enum class Font {
    Small,
    Medium,
    Large,
    LargeBold
  };

  bool Begin();

  // Screen information
  uint16_t Width() const;
  uint16_t Height() const;

  // Framebuffer control
  void Clear();
  void Update();

  // Font
  void SetFont(Font font);

  // Text
  void DrawText(int16_t x, int16_t y, const char* text);
  int16_t GetTextWidth(const char* text);
  int16_t GetTextHeight();
  int16_t GetAscent();

  // Graphics
  void DrawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2);
  void DrawRect(int16_t x, int16_t y, int16_t width, int16_t height);
  void FillRect(int16_t x, int16_t y, int16_t width, int16_t height);
  void SetDrawColor(uint8_t color);

private:
  // Returns true if an I2C device acknowledges at `address`.
  bool ProbeAddress(uint8_t address);
};

extern Oled oled;

#endif
