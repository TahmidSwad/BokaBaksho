#include "apps/lyrics/lyrics_app.h"

#include "core/system.h"
#include "services/ble_service.h"
#include "common/event_types.h"
#include "common/config.h"

LyricsApp lyrics_app;

// ==========================================================
// LIFECYCLE
// ==========================================================

bool LyricsApp::Begin() {
  ClearData();
  EnterWaitingSongs();
  return true;
}

bool LyricsApp::OnActivate() {
  if (total_songs_ == 0) {
    EnterWaitingSongs();
  } else {
    EnterFileList();
  }
  Serial.println("LYRICS_ON");
  return true;
}

void LyricsApp::OnDeactivate() {
  StopPlayback();
  ClearDisplay();
  Serial.println("LYRICS_OFF");
}

// ==========================================================
// BLE HANDLERS
// ==========================================================

void LyricsApp::OnAudioStarted() {
  if (state_ == State::WaitingHandshake) {
    Serial.println("BLE: AUDIO_STARTED received");
    audio_started_ = true;
  }
}

void LyricsApp::OnPlaybackEnded() {
  if (state_ == State::Playing) {
    Serial.println("BLE: END received");
    playback_ended_ = true;
  }
}

void LyricsApp::OnTotalSongs(uint8_t total) {
  total_songs_ = total;
  Serial.print("BLE: TOTAL_SONGS=");
  Serial.println(total);
}

void LyricsApp::OnSongListReceived(const char* names) {
  if (names == nullptr) return;
  const char* p = names;
  uint8_t added = 0;
  while (*p && added < kWindowSize) {
    const char* start = p;
    while (*p && *p != '|') p++;
    size_t len = p - start;
    if (len >= kSongNameLen) len = kSongNameLen - 1;
    if (len > 0) {
      memcpy(song_buffer_[added], start, len);
      song_buffer_[added][len] = '\0';
      ++added;
    }
    if (*p == '|') p++;
  }
  buffer_count_ = added;
  waiting_for_songs_ = false;

  if (added < kWindowSize) {
    uint8_t actual_total = buffer_offset_ + added;
    if (total_songs_ == 0 || actual_total < total_songs_) {
      total_songs_ = actual_total;
    }
  }

  Serial.print("BLE: SONGS received ");
  Serial.print(added);
  Serial.println(" songs");

  if (state_ == State::WaitingSongs && added > 0) {
    EnterFileList();
  } else if (state_ == State::FileList) {
    ShowFileList();
  }
}

void LyricsApp::OnLyricsData(const char* line) {
  if (state_ != State::Loading || line == nullptr) return;
  ParseLine(line);
}

void LyricsApp::OnLyricsEnd() {
  if (state_ == State::Loading && waiting_for_lyrics_) {
    Serial.print("BLE: LYRICS_END, ");
    Serial.print(word_count_);
    Serial.println(" words parsed");
    if (word_count_ > 0) {
      lyrics_received_ = true;
    } else {
      Serial.println("BLE: No lyrics received");
      ShowStatus("No lyrics", TextSize::Medium);
      state_ = State::LoadFailed;
      load_failed_start_ = millis();
    }
  }
}

// ==========================================================
// INPUT HANDLING
// ==========================================================

bool LyricsApp::HandleInput(const InputEvent& event) {
  switch (state_) {
    case State::WaitingSongs:
      if (event.type == InputType::Back) return false;
      return true;
    case State::FileList:
      return HandleFileListInput(event);
    case State::Loading:
    case State::WaitingHandshake:
      if (event.type == InputType::Back) {
        ReturnToFileList();
        return true;
      }
      return false;
    case State::LoadFailed:
      if (event.type == InputType::Back) {
        ReturnToFileList();
        return true;
      }
      return true;
    case State::Playing:
    case State::Paused:
      return HandlePlaybackInput(event);
    default:
      return false;
  }
}

bool LyricsApp::HandleFileListInput(const InputEvent& event) {
  if (buffer_count_ == 0) {
    if (event.type == InputType::Back) return false;
    return true;
  }
  switch (event.type) {
    case InputType::ButtonDecrement:
    case InputType::RotateLeft:
      if (selected_index_ > 0) {
        --selected_index_;
      } else {
        selected_index_ = total_songs_ - 1;
      }
      if (selected_index_ < buffer_offset_ ||
          selected_index_ >= buffer_offset_ + buffer_count_) {
        RequestSongs(selected_index_ < kWindowSize / 2 ? 0 : selected_index_ - kWindowSize / 2);
      }
      ShowFileList();
      return true;
    case InputType::ButtonIncrement:
    case InputType::RotateRight:
      if (selected_index_ < total_songs_ - 1) {
        ++selected_index_;
      } else {
        selected_index_ = 0;
      }
      if (selected_index_ < buffer_offset_ ||
          selected_index_ >= buffer_offset_ + buffer_count_) {
        RequestSongs(selected_index_ < kWindowSize / 2 ? 0 : selected_index_ - kWindowSize / 2);
      }
      ShowFileList();
      return true;
    case InputType::Enter:
      LoadSelectedLyric();
      return true;
    case InputType::Back:
      return false;
  }
  return false;
}

bool LyricsApp::HandlePlaybackInput(const InputEvent& event) {
  switch (event.type) {
    case InputType::Enter:
      if (state_ == State::Playing) PausePlayback();
      else if (state_ == State::Paused) ResumePlayback();
      return true;
    case InputType::Back:
      ReturnToFileList();
      return true;
  }
  return false;
}

// ==========================================================
// BLE REQUESTS
// ==========================================================

void LyricsApp::RequestSongs(uint8_t offset) {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "REQUEST_SONGS|%d|5", offset);
  if (ble_service.IsConnected()) {
    ble_service.SendCommand(cmd);
    Serial.print("BLE TX: ");
    Serial.println(cmd);
    waiting_for_songs_ = true;
    song_request_start_ = millis();
  } else {
    Serial.println("BLE: not connected");
  }
}

void LyricsApp::LoadSelectedLyric() {
  if (buffer_count_ == 0) return;
  uint8_t buf_idx = selected_index_ - buffer_offset_;
  if (buf_idx >= buffer_count_) return;

  const char* song_name = song_buffer_[buf_idx];
  ClearData();
  waiting_for_lyrics_ = true;
  lyrics_received_ = false;

  char cmd[128];
  snprintf(cmd, sizeof(cmd), "LYRICS|%s", song_name);
  if (ble_service.IsConnected()) {
    ble_service.SendCommand(cmd);
    Serial.print("BLE TX: ");
    Serial.println(cmd);
  } else {
    Serial.println("BLE: not connected");
    waiting_for_lyrics_ = false;
    ShowStatus("No device", TextSize::Medium);
    state_ = State::LoadFailed;
    load_failed_start_ = millis();
    return;
  }

  EnterLoading();
}

// ==========================================================
// STATE TRANSITIONS
// ==========================================================

void LyricsApp::EnterWaitingSongs() {
  state_ = State::WaitingSongs;
  buffer_offset_ = 0;
  buffer_count_ = 0;
  total_songs_ = 0;
  selected_index_ = 0;
  waiting_for_songs_ = true;
  waiting_for_lyrics_ = false;
  ShowStatus("Connect to PC", TextSize::Medium);
  if (ble_service.IsConnected()) {
    RequestSongs(0);
  }
}

void LyricsApp::EnterFileList() {
  state_ = State::FileList;
  loaded_ = false;
  playing_ = false;
  paused_ = false;
  new_word_ = false;
  display_active_ = false;
  waiting_for_songs_ = false;
  waiting_for_lyrics_ = false;
  if (selected_index_ >= total_songs_) selected_index_ = 0;
  if (buffer_count_ == 0 && total_songs_ > 0) {
    RequestSongs(0);
    return;
  }
  ShowFileList();
}

void LyricsApp::EnterLoading() {
  state_ = State::Loading;
  ShowStatus("Loading...", TextSize::Medium);
}

void LyricsApp::EnterWaitingHandshake() {
  state_ = State::WaitingHandshake;
  handshake_start_ = millis();
  waiting_for_lyrics_ = false;
  lyrics_received_ = false;
  ShowStatus("Loading...", TextSize::Medium);
}

void LyricsApp::EnterPlaying() {
  state_ = State::Playing;
  playing_ = true;
  paused_ = false;
  loaded_ = true;
  start_time_ = millis();
  current_word_ = 0;
  last_shown_word_ = UINT16_MAX;
  new_word_ = false;
  display_active_ = false;
  title_until_ = millis() + kTitleTimeoutMs;
}

void LyricsApp::EnterPaused() {
  state_ = State::Paused;
  paused_ = true;
  playing_ = false;
  pause_time_ = millis();
  ShowStatus("PAUSED", TextSize::Medium);
}

void LyricsApp::ReturnToFileList() {
  StopPlayback();
  waiting_for_lyrics_ = false;
  lyrics_received_ = false;
  if (total_songs_ == 0) {
    EnterWaitingSongs();
  } else {
    EnterFileList();
  }
}

// ==========================================================
// PLAYBACK CONTROL
// ==========================================================

void LyricsApp::StartPlayback() {
  playing_ = true;
  paused_ = false;
  start_time_ = millis();
  current_word_ = 0;
  new_word_ = false;
  display_active_ = false;
  title_until_ = millis() + kTitleTimeoutMs;
  state_ = State::Playing;
}

void LyricsApp::PausePlayback() {
  if (state_ != State::Playing) return;
  ble_service.SendCommand("PAUSE");
  Serial.println("PAUSE");
  EnterPaused();
}

void LyricsApp::ResumePlayback() {
  if (state_ != State::Paused) return;
  ble_service.SendCommand("RESUME");
  Serial.println("RESUME");
  start_time_ += millis() - pause_time_;
  state_ = State::Playing;
  playing_ = true;
  paused_ = false;
  new_word_ = true;
}

void LyricsApp::StopPlayback() {
  ble_service.SendCommand("STOP");
  Serial.println("STOP");
  playing_ = false;
  paused_ = false;
  loaded_ = false;
  new_word_ = false;
  display_active_ = false;
  ClearDisplay();
}

// ==========================================================
// DISPLAY
// ==========================================================

void LyricsApp::ShowFileList() {
  if (buffer_count_ == 0) {
    if (waiting_for_songs_) {
      ShowStatus("Loading...", TextSize::Medium);
    } else {
      ShowStatus("Connect to PC", TextSize::Medium);
    }
    return;
  }
  uint8_t visible = 0;
  for (uint8_t i = 0; i < kWindowSize; ++i) {
    uint8_t buf_idx = i;
    if (buf_idx >= buffer_count_) break;
    visible_lines_[i] = song_buffer_[buf_idx];
    ++visible;
  }
  Event event;
  event.type = EventType::DisplayRequest;
  event.sender = GetAppId();
  event.display_request.type = DisplayRequestType::ShowSongList;
  event.display_request.lines = visible_lines_;
  event.display_request.line_count = visible;
  event.display_request.selected = selected_index_ - buffer_offset_;
  event.display_request.text_size = TextSize::Medium;
  event.display_request.alignment = TextAlign::Left;
  event.display_request.total_count = total_songs_;
  event.display_request.has_more_above = buffer_offset_ > 0;
  event.display_request.has_more_below = buffer_offset_ + buffer_count_ < total_songs_;
  display_active_ = true;
  (void)event_bus.Post(event);
}

void LyricsApp::ShowStatus(const char* text, TextSize size) {
  Event event;
  event.type = EventType::DisplayRequest;
  event.sender = GetAppId();
  event.display_request.type = DisplayRequestType::ShowWord;
  event.display_request.text = text;
  event.display_request.text_size = size;
  event.display_request.alignment = TextAlign::Center;
  display_active_ = true;
  (void)event_bus.Post(event);
}

bool LyricsApp::ShowCurrentWord() {
  const char* word = GetCurrentWord();
  if (word == nullptr) return false;
  Event event;
  event.type = EventType::DisplayRequest;
  event.sender = GetAppId();
  event.display_request.type = DisplayRequestType::ShowWord;
  event.display_request.text = word;
  event.display_request.text_size = (strlen(word) > 8) ? TextSize::Medium : TextSize::Large;
  event.display_request.alignment = TextAlign::Center;
  display_active_ = true;
  return (bool)event_bus.Post(event);
}

bool LyricsApp::ClearDisplay() {
  Event event;
  event.type = EventType::DisplayRequest;
  event.sender = GetAppId();
  event.display_request.type = DisplayRequestType::ClearDisplay;
  display_active_ = false;
  return event_bus.Post(event);
}

// ==========================================================
// BLE
// ==========================================================

void LyricsApp::SendPlayCommand(const char* song_name) {
  if (song_name == nullptr) return;
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "PLAY|%s", song_name);
  if (ble_service.IsConnected()) {
    ble_service.SendCommand(cmd);
    Serial.print("BLE TX: ");
    Serial.println(cmd);
  } else {
    Serial.println("BLE: not connected");
  }
}

// ==========================================================
// HELPERS
// ==========================================================

void LyricsApp::ClearData() {
  word_count_ = 0;
  current_word_ = 0;
  last_shown_word_ = UINT16_MAX;
  start_time_ = 0;
  pause_time_ = 0;
  title_until_ = 0;
  handshake_start_ = 0;
  loaded_ = false;
  playing_ = false;
  paused_ = false;
  new_word_ = false;
  display_active_ = false;
}

void LyricsApp::ClearLyricData() {
  word_count_ = 0;
  current_word_ = 0;
  last_shown_word_ = UINT16_MAX;
}

const char* LyricsApp::GetCurrentWord() const {
  if (!loaded_ || current_word_ >= word_count_) return nullptr;
  return words_[current_word_].word;
}

// ==========================================================
// UPDATE
// ==========================================================

void LyricsApp::Update() {
  switch (state_) {
    case State::WaitingSongs: {
      if (waiting_for_songs_ && song_request_start_ > 0 &&
          millis() - song_request_start_ > kSongRequestTimeoutMs) {
        Serial.println("BLE: Song request timeout");
        ShowStatus("No device", TextSize::Medium);
        state_ = State::LoadFailed;
        load_failed_start_ = millis();
      }
      break;
    }
    case State::FileList: {
      if (waiting_for_songs_ && song_request_start_ > 0 &&
          millis() - song_request_start_ > kSongRequestTimeoutMs) {
        Serial.println("BLE: Song request timeout");
        ShowStatus("No device", TextSize::Medium);
        state_ = State::LoadFailed;
        load_failed_start_ = millis();
      }
      break;
    }
    case State::Loading: {
      if (lyrics_received_) {
        lyrics_received_ = false;
        EnterWaitingHandshake();
        SendPlayCommand(song_buffer_[selected_index_ - buffer_offset_]);
        break;
      }
      if (song_request_start_ > 0 &&
          millis() - song_request_start_ > kSongRequestTimeoutMs) {
        Serial.println("BLE: Lyrics data timeout");
        ShowStatus("No device", TextSize::Medium);
        state_ = State::LoadFailed;
        load_failed_start_ = millis();
      }
      break;
    }
    case State::WaitingHandshake: {
      if (audio_started_) {
        audio_started_ = false;
        ClearDisplay();
        EnterPlaying();
        break;
      }
      if (millis() - handshake_start_ > kHandshakeTimeoutMs) {
        Serial.println("BLE: Handshake timeout");
        static const char* timeout_lines[] = {"No device", "connected"};
        Event event;
        event.type = EventType::DisplayRequest;
        event.sender = GetAppId();
        event.display_request.type = DisplayRequestType::ShowLines;
        event.display_request.lines = timeout_lines;
        event.display_request.line_count = 2;
        event.display_request.selected = 255;
        event.display_request.text_size = TextSize::Medium;
        event.display_request.alignment = TextAlign::Center;
        display_active_ = true;
        (void)event_bus.Post(event);
        state_ = State::LoadFailed;
        load_failed_start_ = millis();
      }
      break;
    }
    case State::LoadFailed:
      break;
    case State::Playing: {
      if (!ble_service.IsConnected()) {
        StopPlayback();
        ReturnToFileList();
        break;
      }
      if (playback_ended_) {
        playback_ended_ = false;
        StopPlayback();
        uint8_t next = (selected_index_ + 1) % total_songs_;
        selected_index_ = next;
        if (next < buffer_offset_ || next >= buffer_offset_ + buffer_count_) {
          RequestSongs(next < kWindowSize / 2 ? 0 : next - kWindowSize / 2);
        }
        LoadSelectedLyric();
        break;
      }
      if (!playing_) return;
      if (millis() < title_until_) return;
      uint32_t elapsed = millis() - start_time_;
      UpdateCurrentWord(elapsed);
      if (new_word_) {
        if (ShowCurrentWord()) {
          new_word_ = false;
          last_shown_word_ = current_word_;
          word_display_start_ = millis();
        }
        return;
      }
      if (!display_active_) return;
      if (millis() - word_display_start_ < kWordDisplayTimeoutMs) return;
      ClearDisplay();
      break;
    }
    case State::Paused: {
      if (!ble_service.IsConnected()) {
        StopPlayback();
        ReturnToFileList();
        break;
      }
      break;
    }
    default:
      break;
  }
}

// ==========================================================
// PARSING
// ==========================================================

bool LyricsApp::ParseLine(const char* line) {
  if (line == nullptr) return false;
  const char* p = line;
  if (p[0] == '[') {
    while (*p && *p != ']') p++;
    if (*p == ']') p++;
    while (*p == ' ') p++;
  }
  bool added_any = false;
  while (*p && word_count_ < kMaxWords) {
    if (*p == '<') {
      uint32_t ms = 0;
      uint8_t consumed = 0;
      if (!ParseWordTimestamp(p, ms, consumed)) { p++; continue; }
      const char* text = p + consumed;
      uint8_t i = 0;
      while (i < kMaxWordLength - 1 && text[i] != '\0' && text[i] != ' '
             && text[i] != '\n' && text[i] != '\r') {
        words_[word_count_].word[i] = text[i];
        ++i;
      }
      words_[word_count_].word[i] = '\0';
      words_[word_count_].timestamp = ms;
      ++word_count_;
      added_any = true;
      p += consumed;
      while (*p == ' ') p++;
      continue;
    }
    p++;
  }
  return added_any;
}

bool LyricsApp::ParseWordTimestamp(const char* text, uint32_t& milliseconds, uint8_t& consumed) {
  if (text == nullptr || strlen(text) < 10) return false;
  if (text[0] != '<' || text[3] != ':' || text[6] != '.' || text[9] != '>') return false;
  for (uint8_t i = 1; i <= 2; ++i) if (!isdigit(text[i])) return false;
  for (uint8_t i = 4; i <= 5; ++i) if (!isdigit(text[i])) return false;
  for (uint8_t i = 7; i <= 8; ++i) if (!isdigit(text[i])) return false;
  uint32_t minutes = (text[1] - '0') * 10 + (text[2] - '0');
  uint32_t seconds = (text[4] - '0') * 10 + (text[5] - '0');
  uint32_t hundredths = (text[7] - '0') * 10 + (text[8] - '0');
  if (seconds >= 60) return false;
  milliseconds = (minutes * 60UL * 1000UL) + (seconds * 1000UL) + (hundredths * 10UL);
  consumed = 10;
  return true;
}

void LyricsApp::UpdateCurrentWord(uint32_t elapsed) {
  while (current_word_ + 1 < word_count_ &&
         words_[current_word_ + 1].timestamp <= elapsed) {
    ++current_word_;
    new_word_ = true;
  }
  if (!new_word_ && !display_active_ && current_word_ < word_count_ &&
      elapsed >= words_[current_word_].timestamp &&
      current_word_ != last_shown_word_) {
    new_word_ = true;
    last_shown_word_ = current_word_;
  }
}
