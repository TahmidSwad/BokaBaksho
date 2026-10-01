#ifndef UI_TYPES_H
#define UI_TYPES_H

#include <Arduino.h>

// ==========================================================
// TEXT SIZE
// ==========================================================

enum class TextSize {
  Small,
  Medium,
  Large
};

// ==========================================================
// TEXT ALIGNMENT
// ==========================================================

enum class TextAlign {
  Left,
  Center,
  Right
};

// ==========================================================
// DISPLAY REQUEST TYPE
// ==========================================================
//
// These describe operations that the Display Service
// understands. They are shared between applications and
// the display service, so they live in this common header.
//
// ==========================================================

enum class DisplayRequestType {
  None,
  ShowWord,
  ShowLine,
  ShowLines,
  ShowSongList,
  ShowAppMenu,
  ShowBigTime,
  ShowScreensaver,
  ClearDisplay
};

#endif