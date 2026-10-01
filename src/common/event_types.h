#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <Arduino.h>

#include "common/ui_types.h"
#include "common/input_types.h"

// ==========================================================
// APPLICATION IDENTIFIER
// ==========================================================
// SystemUi is the launcher; the other ids are user apps.
// Apps that are not implemented yet simply stay unregistered
// and therefore do not appear in the launcher menu.
// ==========================================================

enum class AppId : uint8_t {
  SystemUi,
  Lyrics,
  Music,
  Alarm,
  Settings,
  Clock,
  Count
};

// ==========================================================
// EVENT TYPE
// ==========================================================

enum class EventType : uint8_t {
  InputEvent,
  DisplayRequest,
  AudioEvent,
  System,
  Count
};

// ==========================================================
// DISPLAY REQUEST
// ==========================================================
// Payload of a DisplayRequest event.
//   ShowAppMenu: lines = item names, line_count = item count,
//                selected = highlighted menu item.
// ==========================================================

struct DisplayRequest {
  DisplayRequestType type = DisplayRequestType::None;
  const char* text = nullptr;
  const char** lines = nullptr;
  uint8_t line_count = 0;
  uint8_t selected = 0;
  // ShowBigTime: a large central time with small labels above/below.
  const char* top_label = nullptr;
  const char* big_time = nullptr;
  const char* bottom_status = nullptr;
  bool blink = false;
  TextSize text_size = TextSize::Medium;
  TextAlign alignment = TextAlign::Center;
  uint8_t total_count = 0;
  bool has_more_above = false;
  bool has_more_below = false;
  // ShowScreensaver: cat animation state.
  uint8_t cat_state = 0;
  uint8_t cat_base_state = 0;
  int8_t cat_y_offset = 0;
  int8_t paw_offset = 0;
};

// ==========================================================
// EVENT
// ==========================================================
// Generic event container; only one payload field is valid
// for a given event type (lightweight alternative to a
// variant). Input events are routed to the foreground app by
// the AppManager; display events are rendered by the Display
// Service, but only when the sender is the foreground app.
// ==========================================================

struct Event {
  EventType type = EventType::System;
  AppId sender = AppId::Count;
  InputEvent input;
  DisplayRequest display_request;
};

#endif