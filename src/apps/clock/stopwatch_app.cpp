#include "apps/clock/stopwatch_app.h"

#include "core/system.h"
#include "common/event_types.h"

StopwatchApp stopwatch_app;

// ==========================================================
// LIFECYCLE
// ==========================================================

bool StopwatchApp::Begin() {
  ResetAll();
  return true;
}

bool StopwatchApp::OnActivate() {
  ResetAll();
  Draw();
  return true;
}

void StopwatchApp::OnDeactivate() {
}

// ==========================================================
// INPUT
// ==========================================================

bool StopwatchApp::HandleInput(const InputEvent& event) {
  switch (event.type) {
    case InputType::RotateRight:
    case InputType::ButtonIncrement:
      if (running_ || finished_) return true;   // lock while active
      if (timer_set_min_ < 65535U) {
        ++timer_set_min_;
      }
      remaining_ms_ = (uint32_t)timer_set_min_ * kMinuteMs;
      mode_ = (timer_set_min_ == 0) ? Mode::STOPWATCH : Mode::TIMER;
      Draw();
      return true;

    case InputType::RotateLeft:
    case InputType::ButtonDecrement:
      if (running_ || finished_) return true;   // lock while active
      if (timer_set_min_ > 0) {
        --timer_set_min_;
      }
      remaining_ms_ = (uint32_t)timer_set_min_ * kMinuteMs;
      mode_ = (timer_set_min_ == 0) ? Mode::STOPWATCH : Mode::TIMER;
      Draw();
      return true;

    case InputType::Enter:
      if (finished_) {
        // Acknowledge the finished timer: re-arm it at the preset.
        finished_ = false;
        blink_on_ = true;
        remaining_ms_ = (uint32_t)timer_set_min_ * kMinuteMs;
        running_ = false;
      } else if (running_) {
        running_ = false;                        // pause
      } else {
        Start();                               // (re)start
      }
      Draw();
      return true;

    case InputType::Back:
      if (IsReset()) {
        return false;                            // exit to launcher
      }
      ResetAll();
      Draw();
      return true;
  }
  return true;
}

// ==========================================================
// UPDATE
// ==========================================================

void StopwatchApp::Update() {
  uint32_t now = millis();

  if (finished_) {
    // Flash the display until acknowledged.
    if (now - last_blink_ms_ >= kBlinkMs) {
      last_blink_ms_ = now;
      blink_on_ = !blink_on_;
      Draw();
    }
    return;
  }

  if (!running_) return;

  uint32_t delta = now - last_tick_ms_;
  last_tick_ms_ = now;

  if (mode_ == Mode::STOPWATCH) {
    elapsed_ms_ += delta;
  } else if (delta >= remaining_ms_) {
    remaining_ms_ = 0;
    finished_ = true;
    running_ = false;
    last_blink_ms_ = now;
    blink_on_ = true;
  } else {
    remaining_ms_ -= delta;
  }

  if (now - last_draw_ms_ >= kDrawIntervalMs) {
    last_draw_ms_ = now;
    Draw();
  }
}

// ==========================================================
// INTERNAL
// ==========================================================

void StopwatchApp::Start() {
  if (mode_ == Mode::TIMER && timer_set_min_ > 0 && remaining_ms_ == 0) {
    remaining_ms_ = (uint32_t)timer_set_min_ * kMinuteMs;
  }
  last_tick_ms_ = millis();
  last_draw_ms_ = last_tick_ms_;
  running_ = true;
}

void StopwatchApp::ResetAll() {
  mode_ = Mode::STOPWATCH;
  running_ = false;
  finished_ = false;
  blink_on_ = true;
  timer_set_min_ = 0;
  elapsed_ms_ = 0;
  remaining_ms_ = 0;
}

bool StopwatchApp::IsReset() const {
  return timer_set_min_ == 0 && elapsed_ms_ == 0
         && !running_ && !finished_;
}

// ==========================================================
// DRAWING
// ==========================================================

void StopwatchApp::Draw() {
  const char* top = "";
  const char* time_str = "";
  const char* bottom = "";

  if (finished_) {
    top = "TIME'S UP!";
    FormatTime(time_buf_, sizeof(time_buf_), 0);
    time_str = time_buf_;
    bottom = "ENTER: restart";
  } else if (mode_ == Mode::STOPWATCH) {
    top = "STOPWATCH";
    FormatTime(time_buf_, sizeof(time_buf_), elapsed_ms_);
    time_str = time_buf_;
    bottom = running_ ? "RUNNING" : "READY";
  } else {  // TIMER
    top = "TIMER";
    FormatTime(time_buf_, sizeof(time_buf_), remaining_ms_);
    time_str = time_buf_;
    bottom = running_ ? "RUNNING" : "READY";
  }

  Event event;
  event.type = EventType::DisplayRequest;
  event.sender = GetAppId();
  event.display_request.type = DisplayRequestType::ShowBigTime;
  event.display_request.top_label = top;
  event.display_request.big_time = time_str;
  event.display_request.bottom_status = bottom;
  event.display_request.blink = finished_ && !blink_on_;
  event.display_request.alignment = TextAlign::Center;
  event_bus.Post(event);
}

// ==========================================================
// FORMAT mm:ss (or h:mm:ss past an hour)
// ==========================================================

void StopwatchApp::FormatTime(char* buf, size_t len, uint32_t ms) const {
  uint32_t total_s = ms / 1000U;
  uint32_t hh = total_s / 3600U;
  uint32_t mm = (total_s % 3600U) / 60U;
  uint32_t ss = total_s % 60U;

  if (hh > 0) {
    snprintf(buf, len, "%lu:%02lu:%02lu",
             (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
  } else {
    snprintf(buf, len, "%lu:%02lu",
             (unsigned long)mm, (unsigned long)ss);
  }
}