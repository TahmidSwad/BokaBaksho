#include "system_ui/system_ui.h"

#include "core/system.h"
#include "common/event_types.h"

SystemUi system_ui;

bool SystemUi::Begin() {
  selection_ = 0;
  screensaver_active_ = true;
  base_state_ = CatState::Neutral;
  draw_state_ = CatState::Neutral;
  cat_y_offset_ = 0;
  is_blinking_ = false;
  sleepy_mode_ = false;
  look_dir_ = 0;
  is_looking_ = false;
  next_look_time_ = millis() + random(6000, 10000);
  next_neutral_blink_ = millis() + random(2500, 6000);
  in_expression_ = true;
  hold_start_ = millis();
  hold_duration_ = random(6000, 12000);
  next_blink_time_ = millis() + random(1000, 2000);
  return ShowScreensaver();
}

bool SystemUi::OnActivate() {
  screensaver_active_ = false;
  return ShowMenu();
}

void SystemUi::OnDeactivate() {
}

// ==========================================================
// EXPRESSION PICKING
// ==========================================================

void SystemUi::PickNextExpression() {
  unsigned long now = millis();
  hold_start_ = now;
  look_dir_ = 0;
  is_looking_ = false;

  if (!in_expression_) {
    // Currently neutral → pick an expression.
    in_expression_ = true;
    uint8_t expr = random(6);
    switch (expr) {
      case 0:
        base_state_ = CatState::Happy;
        break;
      case 1:
        base_state_ = CatState::Alert;
        break;
      case 2:
        base_state_ = CatState::Sad;
        break;
      case 3:
  base_state_ = CatState::Neutral;
        break;
      case 4:
        base_state_ = CatState::Angry;
        break;
      case 5:
        base_state_ = CatState::Blink;
        break;
    }
    hold_duration_ = random(6000, 12000);
    cat_y_offset_ = (base_state_ != CatState::Sleepy) ? -2 : 0;
    sleepy_mode_ = (base_state_ == CatState::Sleepy);
    next_blink_time_ = now + random(1000, 2500);
  } else {
    // Currently expression → go back to neutral.
    in_expression_ = false;
    base_state_ = CatState::Neutral;
    hold_duration_ = random(15000, 25000);
    cat_y_offset_ = 0;
    sleepy_mode_ = false;
    next_neutral_blink_ = now + random(2500, 6000);
  }

  draw_state_ = base_state_;
  is_blinking_ = false;
}

// ==========================================================
// UPDATE — drives cat animation while screensaver is active
// ==========================================================

void SystemUi::Update() {
  if (!screensaver_active_) return;

  unsigned long now = millis();
  bool redraw = false;

  // Neutral mode blinking — independent of expression blinks.
  if (base_state_ == CatState::Neutral && !is_blinking_ && now >= next_neutral_blink_) {
    draw_state_ = CatState::Blink;
    is_blinking_ = true;
    blink_end_time_ = now + 180;
    uint16_t interval = is_looking_ ? random(2500, 6000) : random(2500, 6000);
    next_neutral_blink_ = now + interval;
    redraw = true;
  }

  // Handle ongoing blink.
  if (is_blinking_) {
    if (now >= blink_end_time_) {
      if (sleepy_mode_) {
        draw_state_ = CatState::Blink;
      } else {
        draw_state_ = base_state_;
      }
      is_blinking_ = false;
      redraw = true;
    }
  }

  // Check if it's time for the next blink.
  if (!is_blinking_ && now >= next_blink_time_) {
    draw_state_ = CatState::Blink;
    is_blinking_ = true;
    blink_end_time_ = now + 180;

    // In normal mode: blink every 3-6s. While looking: blink more often.
    if (base_state_ == CatState::Neutral) {
      uint16_t blink_interval = is_looking_ ? random(1500, 3000) : random(3000, 6000);
      next_blink_time_ = now + blink_interval;
    } else {
      next_blink_time_ = now + random(1000, 5000);
    }
    redraw = true;
  }

  // Look left/right — only in normal mode.
  if (base_state_ == CatState::Neutral) {
    if (is_looking_) {
      if (now >= look_end_time_) {
        look_dir_ = 0;
        is_looking_ = false;
        cat_y_offset_ = 0;
        next_look_time_ = now + random(6000, 10000);
        redraw = true;
      }
    } else if (now >= next_look_time_) {
      // Randomly decide to look left or right.
      if (random(3) == 0) {
        look_dir_ = random(2) == 0 ? -1 : 1;
        is_looking_ = true;
        cat_y_offset_ = -2;
        look_end_time_ = now + random(1000, 3000);
        redraw = true;
      }
    }
  }

  // Check if the entire hold period has expired.
  if (now - hold_start_ >= hold_duration_) {
    PickNextExpression();
    redraw = true;
  }

  if (redraw) ShowScreensaver();
}

// ==========================================================
// INPUT
// ==========================================================

bool SystemUi::HandleInput(const InputEvent& event) {
  if (screensaver_active_) {
    screensaver_active_ = false;
    return ShowMenu();
  }

  uint8_t count = app_manager.GetAppCount();
  if (count == 0) return false;

  switch (event.type) {
    case InputType::ButtonDecrement:
    case InputType::RotateLeft:
      selection_ = (selection_ == 0) ? (uint8_t)(count - 1)
                                      : (uint8_t)(selection_ - 1);
      return ShowMenu();

    case InputType::ButtonIncrement:
    case InputType::RotateRight:
      selection_ = (uint8_t)((selection_ + 1) % count);
      return ShowMenu();

    case InputType::Enter: {
      IApp* target = app_manager.GetAppAt(selection_);
      if (target != nullptr) app_manager.LaunchApp(target->GetAppId());
      return true;
    }

    case InputType::Back:
      screensaver_active_ = true;
      base_state_ = CatState::Sleepy;
      draw_state_ = CatState::Sleepy;
      cat_y_offset_ = 0;
      is_blinking_ = false;
      sleepy_mode_ = true;
      look_dir_ = 0;
      is_looking_ = false;
      next_look_time_ = millis() + random(6000, 10000);
      next_neutral_blink_ = millis() + random(2500, 6000);
      in_expression_ = true;
      hold_start_ = millis();
      hold_duration_ = random(6000, 12000);
      next_blink_time_ = millis() + random(1000, 2000);
      return ShowScreensaver();
  }
  return true;
}

// ==========================================================
// SCREENSAVER
// ==========================================================

bool SystemUi::ShowScreensaver() {
  Event event;
  event.type = EventType::DisplayRequest;
  event.sender = GetAppId();
  event.display_request.type = DisplayRequestType::ShowScreensaver;
  event.display_request.cat_state = static_cast<uint8_t>(draw_state_);
  event.display_request.cat_base_state = static_cast<uint8_t>(base_state_);
  event.display_request.cat_y_offset = cat_y_offset_;
  event.display_request.paw_offset = look_dir_;
  return event_bus.Post(event);
}

// ==========================================================
// MENU
// ==========================================================

bool SystemUi::ShowMenu() {
  uint8_t count = app_manager.GetAppCount();
  if (count == 0 || count > kMaxMenuItems) return false;

  for (uint8_t i = 0; i < count; ++i) {
    IApp* app = app_manager.GetAppAt(i);
    menu_items_[i] = (app != nullptr) ? app->GetName() : "";
  }

  Event event;
  event.type = EventType::DisplayRequest;
  event.sender = GetAppId();
  event.display_request.type = DisplayRequestType::ShowAppMenu;
  event.display_request.lines = menu_items_;
  event.display_request.line_count = count;
  event.display_request.selected = selection_;
  return event_bus.Post(event);
}
