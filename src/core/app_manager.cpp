#include "core/app_manager.h"

#include "common/event_types.h"

AppManager app_manager;

// ==========================================================
// REGISTRY
// ==========================================================

bool AppManager::RegisterApp(IApp* app) {
  if (app == nullptr || app_count_ >= kMaxApps) return false;
  if (IsRegistered(app->GetAppId())) return false;

  apps_[app_count_++] = app;
  return true;
}

uint8_t AppManager::GetAppCount() const {
  return app_count_;
}

IApp* AppManager::GetAppAt(uint8_t index) const {
  return (index < app_count_) ? apps_[index] : nullptr;
}

// ==========================================================
// NAVIGATION
// ==========================================================

bool AppManager::Boot(IApp* system_ui) {
  if (system_ui == nullptr || depth_ != 0) return false;

  stack_[0] = system_ui;
  depth_ = 1;
  return true;
}

bool AppManager::LaunchApp(AppId id) {
  IApp* target = nullptr;
  for (uint8_t i = 0; i < app_count_; ++i) {
    if (apps_[i]->GetAppId() == id) {
      target = apps_[i];
      break;
    }
  }
  if (target == nullptr) return false;
  if (depth_ > 0 && stack_[depth_ - 1] == target) return true;  // already top
  if (depth_ >= kMaxDepth) return false;

  IApp* old_top = (depth_ > 0) ? stack_[depth_ - 1] : nullptr;
  if (old_top != nullptr) old_top->OnDeactivate();

  if (!target->OnActivate()) {
    // Roll back: restore the previous top of the stack.
    if (old_top != nullptr) old_top->OnActivate();
    return false;
  }

  stack_[depth_++] = target;
  return true;
}

void AppManager::GoBack() {
  if (depth_ <= 1) return;  // never pop the launcher root

  IApp* top = stack_[depth_ - 1];
  --depth_;
  top->OnDeactivate();

  IApp* new_top = stack_[depth_ - 1];
  new_top->OnActivate();
}

// ==========================================================
// STATE QUERIES
// ==========================================================

bool AppManager::IsForeground(AppId id) const {
  return (depth_ > 0) && stack_[depth_ - 1]->GetAppId() == id;
}

IApp* AppManager::GetForeground() const {
  return (depth_ > 0) ? stack_[depth_ - 1] : nullptr;
}

// ==========================================================
// LOOP PUMP
// ==========================================================

void AppManager::Update() {
  IApp* top = GetForeground();
  if (top != nullptr) top->Update();
}

// ==========================================================
// INPUT ROUTING
// ==========================================================

bool AppManager::RouteInput(const InputEvent& event) {
  IApp* top = GetForeground();
  if (top == nullptr) return false;

  if (top->HandleInput(event)) return true;

  // Unconsumed Back pops the stack (returns to the launcher).
  if (event.type == InputType::Back) {
    GoBack();
    return true;
  }
  return false;
}

void AppManager::OnEvent(const Event& event) {
  if (event.type == EventType::InputEvent) {
    RouteInput(event.input);
  }
}

// ==========================================================
// HELPERS
// ==========================================================

bool AppManager::IsRegistered(AppId id) const {
  for (uint8_t i = 0; i < app_count_; ++i) {
    if (apps_[i]->GetAppId() == id) return true;
  }
  return false;
}