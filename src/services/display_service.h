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

  // Internal flag to indicate that the OLED driver is ready.
  bool initialized_ = false;
};

extern DisplayService display_service;

#endif