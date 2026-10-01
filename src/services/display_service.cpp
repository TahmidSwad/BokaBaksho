#include "services/display_service.h"

#include "drivers/oled/oled.h"
#include "core/system.h"

DisplayService display_service;

bool DisplayService::Begin() {
  if (oled.Begin()) {
    initialized_ = true;
    return true;
  }
  return false;
}

void DisplayService::OnEvent(const Event& event) {
  if (!initialized_ || event.type != EventType::DisplayRequest) {
    return;
  }

  // Only the foreground app may draw: exclusive display
  // ownership is enforced here (replaces the old
  // ResourceManager).
  const IApp* owner = app_manager.GetForeground();
  if (owner == nullptr || owner->GetAppId() != event.sender) {
    return;
  }

  const DisplayRequest& req = event.display_request;
  switch (req.type) {
    case DisplayRequestType::ShowWord:
      ShowWord(req);
      break;
    case DisplayRequestType::ShowLine:
      ShowLine(req);
      break;
    case DisplayRequestType::ShowLines:
      ShowLines(req);
      break;
    case DisplayRequestType::ShowSongList:
      ShowSongList(req);
      break;
    case DisplayRequestType::ShowAppMenu:
      ShowAppMenu(req);
      break;
    case DisplayRequestType::ShowBigTime:
      ShowBigTime(req);
      break;
    case DisplayRequestType::ShowScreensaver:
      ShowScreensaver(req);
      break;
    case DisplayRequestType::ClearDisplay:
      ClearDisplay();
      break;
    default:
      break;
  }
}

// ==========================================================
// SHOW WORD
// ==========================================================

void DisplayService::ShowWord(const DisplayRequest& req) {
  if (req.text == nullptr) return;
  oled.Clear();
  SetFont(req.text_size);
  int16_t text_width = oled.GetTextWidth(req.text);
  int16_t text_height = oled.GetTextHeight();
  int16_t x = (oled.Width() - text_width) / 2;
  int16_t y = (oled.Height() + text_height) / 2;
  oled.DrawText(x, y, req.text);
  oled.Update();
}

// ==========================================================
// SHOW LINE
// ==========================================================

void DisplayService::ShowLine(const DisplayRequest& req) {
  if (req.text == nullptr) return;
  oled.Clear();
  SetFont(req.text_size);
  int16_t x = CalculateX(req.text, req.alignment);
  int16_t y = CalculateCenteredY();
  oled.DrawText(x, y, req.text);
  oled.Update();
}

// ==========================================================
// SHOW MULTIPLE LINES
// ==========================================================

void DisplayService::ShowLines(const DisplayRequest& req) {
  if (req.lines == nullptr || req.line_count == 0) return;
  oled.Clear();
  SetFont(req.text_size);
  int16_t line_height = oled.GetTextHeight();
  int16_t total_height = req.line_count * line_height;
  int16_t start_y = (oled.Height() - total_height) / 2 + line_height;
  bool has_selection = req.selected < req.line_count;
  for (uint8_t i = 0; i < req.line_count; ++i) {
    int16_t y = start_y + i * line_height;
    if (has_selection && i == req.selected) {
      oled.DrawText(0, y, ">");
    }
    int16_t x = has_selection ? 10 : CalculateX(req.lines[i], req.alignment);
    oled.DrawText(x, y, req.lines[i]);
  }
  oled.Update();
}

// ==========================================================
// SHOW SONG LIST
// ==========================================================

void DisplayService::ShowSongList(const DisplayRequest& req) {
  if (req.lines == nullptr || req.line_count == 0) return;
  oled.Clear();

  oled.SetFont(Oled::Font::Medium);
  int16_t line_h = oled.GetTextHeight();
  int16_t accent = oled.GetAscent();
  int16_t pad_x = 8;
  int16_t pad_top = 2;
  int16_t start_y = pad_top + accent;

  bool has_sel = req.selected < req.line_count;
  int16_t sel_abs = has_sel ? (int16_t)req.selected : -1;

  for (uint8_t i = 0; i < req.line_count; ++i) {
    int16_t y = start_y + i * line_h;
    if ((int16_t)i == sel_abs) {
      oled.DrawText(0, y, ">");
      oled.DrawText(pad_x, y, req.lines[i]);
    } else {
      oled.DrawText(pad_x, y, req.lines[i]);
    }
  }

  if (req.has_more_above) {
    oled.SetFont(Oled::Font::Small);
    const char* arrow = "^";
    int16_t aw = oled.GetTextWidth(arrow);
    oled.DrawText((oled.Width() - aw) / 2, oled.GetAscent(), arrow);
  }

  if (req.has_more_below) {
    oled.SetFont(Oled::Font::Small);
    const char* arrow = "v";
    int16_t aw = oled.GetTextWidth(arrow);
    int16_t bottom_y = oled.Height() - 1;
    oled.DrawText((oled.Width() - aw) / 2, bottom_y, arrow);
  }

  static char counter_buf[8];
  int16_t abs_idx = has_sel ? (req.selected + 1) : 0;
  snprintf(counter_buf, sizeof(counter_buf), "%d / %d", abs_idx, req.total_count);
  oled.SetFont(Oled::Font::Small);
  int16_t cw = oled.GetTextWidth(counter_buf);
  int16_t counter_y = oled.Height() - 1;
  oled.DrawText(oled.Width() - cw - 2, counter_y, counter_buf);

  oled.Update();
}

// ==========================================================
// SHOW APP MENU
// ==========================================================

void DisplayService::ShowAppMenu(const DisplayRequest& req) {
  if (req.lines == nullptr || req.line_count == 0) return;

  oled.Clear();

  uint8_t count = req.line_count;
  uint8_t sel = (req.selected < count) ? req.selected : (uint8_t)(count - 1);
  const char* current_name = req.lines[sel];

  // Current app (centered both axes).
  oled.SetFont(Oled::Font::Large);
  int16_t x = CalculateX(current_name, TextAlign::Center);
  int16_t y = CalculateCenteredY();
  oled.DrawText(x, y, current_name);

  // Pagination dots.
  oled.SetFont(Oled::Font::Medium);
  int16_t dot_y = oled.Height() - 10;
  int16_t dot_spacing = 8;
  int16_t dots_width = count * dot_spacing;
  int16_t dot_start_x = (oled.Width() - dots_width) / 2;
  for (uint8_t i = 0; i < count; ++i) {
    int16_t dot_x = dot_start_x + i * dot_spacing + 2;
    if (i == sel) {
      oled.FillRect(dot_x, dot_y, 4, 4);
    } else {
      oled.DrawRect(dot_x, dot_y, 4, 4);
    }
  }

  oled.Update();
}

// ==========================================================
// SHOW SCREENSAVER
// ==========================================================
// Draws the animated cat face.
// cat_state: 0=happy, 1=blink, 2=alert, 3=neutral,
//            4=sad, 5=sleepy, 6=angry
// ==========================================================

void DisplayService::ShowScreensaver(const DisplayRequest& req) {
  oled.Clear();

  int8_t yo = req.cat_y_offset;

  // Head — wider sides, same top/risers.
  oled.DrawLine(44, 41 + yo, 44, 17 + yo);
  oled.DrawLine(44, 17 + yo, 52, 17 + yo);
  oled.DrawLine(52, 17 + yo, 52, 11 + yo);
  oled.DrawLine(52, 11 + yo,  76, 11 + yo);
  oled.DrawLine(76, 11 + yo,  76, 17 + yo);
  oled.DrawLine(76, 17 + yo, 84, 17 + yo);
  oled.DrawLine(84, 17 + yo, 84, 41 + yo);

  // Paws — larger squares.
  oled.DrawRect(38, 41, 14, 14);
  oled.DrawRect(76, 41, 14, 14);

  // Connecting line between paws.
  oled.DrawLine(52, 46, 76, 46);

  // Face — eyes and mouth inside head.
  int8_t ey = 24 + yo;
  int8_t lo = req.paw_offset * 2;  // look offset: -2, 0, or +2

  switch (req.cat_state) {
    case 0:  // Happy — chevron ^ ^
      oled.DrawLine(54 + lo, ey + 4, 57 + lo, ey);
      oled.DrawLine(57 + lo, ey, 61 + lo, ey + 4);
      oled.DrawLine(66 + lo, ey + 4, 69 + lo, ey);
      oled.DrawLine(69 + lo, ey, 73 + lo, ey + 4);
      break;
    case 1:  // Blink — flat - -
      oled.DrawLine(54 + lo, ey + 2, 61 + lo, ey + 2);
      oled.DrawLine(66 + lo, ey + 2, 73 + lo, ey + 2);
      break;
    case 2:  // Alert — box eyes [] [] (same as neutral)
      oled.DrawRect(54 + lo, ey, 7, 7);
      oled.DrawRect(66 + lo, ey, 7, 7);
      break;
    case 3:  // Neutral — box eyes [] [] (default)
      oled.DrawRect(54 + lo, ey, 7, 7);
      oled.DrawRect(66 + lo, ey, 7, 7);
      break;
    case 4:  // Sad — droopy eyes
      oled.DrawLine(54 + lo, ey, 57 + lo, ey + 4);
      oled.DrawLine(57 + lo, ey + 4, 61 + lo, ey);
      oled.DrawLine(66 + lo, ey, 69 + lo, ey + 4);
      oled.DrawLine(69 + lo, ey + 4, 73 + lo, ey);
      break;
    case 5:  // Sleepy — same as blink
      oled.DrawLine(54 + lo, ey + 2, 61 + lo, ey + 2);
      oled.DrawLine(66 + lo, ey + 2, 73 + lo, ey + 2);
      break;
    case 6:  // Angry — x x eyes
      oled.DrawLine(54 + lo, ey, 61 + lo, ey + 6);
      oled.DrawLine(61 + lo, ey, 54 + lo, ey + 6);
      oled.DrawLine(66 + lo, ey, 73 + lo, ey + 6);
      oled.DrawLine(73 + lo, ey, 66 + lo, ey + 6);
      break;
  }

  // Mouth — uses base_state so it doesn't change during blink.
  int8_t mouth_y = 35 + yo;
  switch (req.cat_base_state) {
    case 0: {  // Happy — right, bottom, left (no top) — 1px up
      int8_t my = mouth_y - 1;
      oled.DrawLine(68, my, 68, my + 2);
      oled.DrawLine(68, my + 2, 60, my + 2);
      oled.DrawLine(60, my + 2, 60, my);
      break;
    }
    case 2: {  // Alert — small open mouth — 2px down
      int8_t my = mouth_y + 2;
      oled.DrawRect(61, my - 1, 6, 4);
      break;
    }
    case 3:  // Neutral — straight line (default)
      oled.DrawLine(60, mouth_y, 68, mouth_y);
      break;
    case 4: {  // Sad — right, top, left (no bottom)
      int8_t my = mouth_y;
      oled.DrawLine(68, my + 2, 68, my);
      oled.DrawLine(68, my, 60, my);
      oled.DrawLine(60, my, 60, my + 2);
      break;
    }
    case 6: {  // Angry — right, top, left (no bottom) — 1px down
      int8_t my = mouth_y + 1;
      oled.DrawLine(68, my + 2, 68, my);
      oled.DrawLine(68, my, 60, my);
      oled.DrawLine(60, my, 60, my + 2);
      break;
    }
    default:  // Neutral fallback
      oled.DrawLine(60, mouth_y, 68, mouth_y);
      break;
  }

  oled.Update();
}

// ==========================================================
// SHOW BIG TIME
// ==========================================================
// A large central time with small labels above and below.
//   top_label:    small font, top of screen
//   big_time:     large font, vertically centered
//   bottom_status:small font, bottom of screen
//   blink:        when true, the time is hidden (for flashing)
// ==========================================================

void DisplayService::ShowBigTime(const DisplayRequest& req) {
  oled.Clear();

  // Top label (small font).
  if (req.top_label != nullptr) {
    oled.SetFont(Oled::Font::Medium);
    int16_t x = CalculateX(req.top_label, req.alignment);
    oled.DrawText(x, 12, req.top_label);
  }

  // Big time (large bold font, centered) - hidden while blinking.
  if (req.big_time != nullptr && !req.blink) {
    oled.SetFont(Oled::Font::LargeBold);
    int16_t x = CalculateX(req.big_time, req.alignment);
    int16_t y = CalculateCenteredY();
    oled.DrawText(x, y, req.big_time);
  }

  // Bottom status (small font).
  if (req.bottom_status != nullptr) {
    oled.SetFont(Oled::Font::Small);
    int16_t x = CalculateX(req.bottom_status, req.alignment);
    oled.DrawText(x, oled.Height() - 9, req.bottom_status);
  }

  oled.Update();
}

// ==========================================================
// CLEAR DISPLAY
// ==========================================================

void DisplayService::ClearDisplay() {
  oled.Clear();
  oled.Update();
}

// ==========================================================
// SET FONT
// ==========================================================

void DisplayService::SetFont(TextSize size) {
  switch (size) {
    case TextSize::Small:
      oled.SetFont(Oled::Font::Small);
      break;
    case TextSize::Medium:
      oled.SetFont(Oled::Font::Medium);
      break;
    case TextSize::Large:
      oled.SetFont(Oled::Font::Large);
      break;
  }
}

// ==========================================================
// CALCULATE X POSITION
// ==========================================================

int16_t DisplayService::CalculateX(const char* text, TextAlign align) {
  int16_t text_width = oled.GetTextWidth(text);
  int16_t screen_width = oled.Width();
  switch (align) {
    case TextAlign::Left:
      return 0;
    case TextAlign::Center:
      return (screen_width - text_width) / 2;
    case TextAlign::Right:
      return screen_width - text_width;
  }
  return 0;
}

// ==========================================================
// CALCULATE CENTERED Y
// ==========================================================

int16_t DisplayService::CalculateCenteredY() {
  int16_t screen_height = oled.Height();
  int16_t text_height = oled.GetTextHeight();
  return (screen_height - text_height) / 2 + oled.GetAscent();
}