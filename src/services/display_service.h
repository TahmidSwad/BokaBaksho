#ifndef DISPLAY_SERVICE_H
#define DISPLAY_SERVICE_H

#include <Arduino.h>

#include "core/event_bus.h"
#include "common/ui_types.h"

// ==========================================================
// DISPLAY SERVICE
// ==========================================================
// Subscribes to DisplayRequest events and renders them on the
// OLED screen. Display ownership is enforced here: only the
// foreground app (top of the AppManager stack) may draw.
// This replaces the old ResourceManager with an explicit,
// lightweight exclusive-ownership check.
// ==========================================================

class DisplayService : public IEventSubscriber {
public:
  bool Begin();
  void OnEvent(const Event& event) override;

private:
  void ShowWord(const DisplayRequest& request);
  void ShowLine(const DisplayRequest& request);
  void ShowLines(const DisplayRequest& request);
  void ShowSongList(const DisplayRequest& request);
  void ShowAppMenu(const DisplayRequest& request);
  void ShowBigTime(const DisplayRequest& request);
  void ShowScreensaver(const DisplayRequest& request);
  void ClearDisplay();

  void SetFont(TextSize size);
  int16_t CalculateX(const char* text, TextAlign align);
  int16_t CalculateCenteredY();

  // Word layout: one line if it fits, otherwise two balanced lines.
  // Returns false without drawing when neither fits at the current font.
  bool TryDrawWrapped(const char* text, int16_t max_width);
  void DrawCentered(const char* text);
  static TextSize Shrink(TextSize size);

  // Longest string ShowWord() will split across two lines. It has to hold
  // the whole text plus a terminator, because the split is measured by
  // NUL-terminating a copy rather than mutating the caller's string.
  static constexpr uint8_t kMaxWrapLen = 64;

  // Internal flag to indicate that the OLED driver is ready.
  bool initialized_ = false;
};

extern DisplayService display_service;

#endif