#ifndef LYRICS_APP_H
#define LYRICS_APP_H

#include <Arduino.h>

#include "core/app_base.h"

// ==========================================================
// LYRICS APP
// ==========================================================
// 1. Receives song list from PC over BLE (streaming).
// 2. User selects one with ENTER; ESP32 requests lyrics via BLE.
// 3. PC sends LYRICS_DATA lines, then LYRICS_END.
// 4. ESP32 sends PLAY|song_name, waits for AUDIO_STARTED.
// 5. Lyric timing starts from zero.
// ==========================================================

class LyricsApp : public IApp {
public:
  bool Begin() override;
  bool OnActivate() override;
  void OnDeactivate() override;
  void Update() override;
  bool HandleInput(const InputEvent& event) override;
  AppId GetAppId() const override { return AppId::Lyrics; }
  const char* GetName() const override { return "Lyrics"; }

  void OnAudioStarted();
  void OnPlaybackEnded();
  void OnSongListReceived(const char* names);
  void OnTotalSongs(uint8_t total);
  void OnLyricsData(const char* line);
  void OnLyricsEnd();

private:
  enum class State {
    WaitingSongs,
    FileList,
    Loading,
    WaitingHandshake,
    LoadFailed,
    Playing,
    Paused
  };

  static constexpr uint16_t kMaxWords = 1000;
  static constexpr uint8_t  kMaxWordLength = 32;
  static constexpr uint32_t kTitleTimeoutMs = 1500;
  static constexpr uint32_t kWordDisplayTimeoutMs = 5000;
  static constexpr uint32_t kHandshakeTimeoutMs = 10000;
  static constexpr uint32_t kSongRequestTimeoutMs = 5000;
  static constexpr uint32_t kLoadFailedTimeoutMs = 1500;
  static constexpr uint8_t  kWindowSize = 5;
  static constexpr uint8_t  kSongNameLen = 32;

  struct LyricWord {
    uint32_t timestamp;
    char     word[kMaxWordLength];
  };

  LyricWord words_[kMaxWords];
  uint16_t word_count_ = 0;
  uint16_t current_word_ = 0;
  uint16_t last_shown_word_ = UINT16_MAX;

  uint32_t start_time_ = 0;
  uint32_t pause_time_ = 0;
  uint32_t word_display_start_ = 0;
  uint32_t title_until_ = 0;
  uint32_t handshake_start_ = 0;
  uint32_t load_failed_start_ = 0;
  uint32_t song_request_start_ = 0;

  char song_buffer_[kWindowSize][kSongNameLen];
  uint8_t buffer_offset_ = 0;
  uint8_t requested_offset_ = 0;
  uint8_t buffer_count_ = 0;
  uint8_t total_songs_ = 0;
  const char* visible_lines_[kWindowSize];
  uint8_t selected_index_ = 0;
  bool waiting_for_songs_ = false;
  bool waiting_for_lyrics_ = false;
  bool pending_load_ = false;

  State state_ = State::WaitingSongs;
  bool loaded_ = false;
  bool playing_ = false;
  bool paused_ = false;
  bool new_word_ = false;
  bool display_active_ = false;
  volatile bool audio_started_ = false;
  volatile bool playback_ended_ = false;
  volatile bool lyrics_received_ = false;

  bool ParseLine(const char* line);
  bool ParseWordTimestamp(const char* text, uint32_t& milliseconds, uint8_t& consumed);
  void UpdateCurrentWord(uint32_t elapsed);

  void RequestSongs(uint8_t offset);
  void LoadSelectedLyric();

  void EnterWaitingSongs();
  void EnterFileList();
  void EnterLoading();
  void EnterWaitingHandshake();
  void EnterPlaying();
  void EnterPaused();
  void ReturnToFileList();

  void StartPlayback();
  void PausePlayback();
  void ResumePlayback();
  void StopPlayback();

  void ShowFileList();
  void ShowStatus(const char* text, TextSize size);
  bool ShowCurrentWord();
  bool ClearDisplay();

  void SendPlayCommand(const char* song_name);

  void ClearData();
  void ClearLyricData();
  const char* GetCurrentWord() const;

  bool HandleFileListInput(const InputEvent& event);
  bool HandlePlaybackInput(const InputEvent& event);
};

extern LyricsApp lyrics_app;

#endif
