#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "core/event_bus.h"
#include "common/event_types.h"

// ==========================================================
// BLE SERVICE
// ==========================================================
// A BLE GATT-based command/response protocol for the
// Boka_Baksho lyric synchronization system.
//
// GATT layout:
//   Service   UUID: 12345678-1234-5678-9abc-def012345678
//   TX Char   UUID: 12345678-1234-5678-9abc-def012345679  (NOTIFY)
//   RX Char   UUID: 12345678-1234-5678-9abc-def012345680  (WRITE)
//
// Protocol (plain text, newline-terminated). See docs/ble-protocol.md
// for the full specification.
//
//   ESP32 -> PC (via TX notify):
//     REQUEST_SONGS|<offset>|<count>
//     LYRICS|<song_name>
//     PLAY|<song_name>
//     PAUSE
//     RESUME
//     STOP
//
//   PC -> ESP32 (via RX write):
//     TOTAL_SONGS|<n>
//     SONGS|<name>|<name>|...
//     LYRICS_DATA|<lyric line>
//     LYRICS_END
//     AUDIO_STARTED
//     END
//     STOPPED / PAUSED / RESUMED        (received, currently ignored)
//
// ==========================================================

// ---- Custom 128-bit UUIDs ----
#define BLE_SERVICE_UUID        "12345678-1234-5678-9abc-def012345678"
#define BLE_TX_CHAR_UUID        "12345678-1234-5678-9abc-def012345679"  // ESP -> Python (notify)
#define BLE_RX_CHAR_UUID        "12345678-1234-5678-9abc-def012345680"  // Python -> ESP32 (write)

static constexpr const char* kBleDeviceName = "BokaBaksho";

// ==========================================================
// BLE COMMAND CALLBACK
// ==========================================================
// Apps register a callback to receive commands that arrive
// from the Python client (e.g. AUDIO_STARTED).
// ==========================================================

using BleCommandCallback = void (*)(const char* command);

// Forward declarations for friend access
class BleServerCallbacks;
class BleRxCallbacks;

class BleService : public IEventSubscriber {
public:
  bool Begin();
  void OnEvent(const Event& event) override;

  // Send a command string to the connected Python client.
  // Returns false if no client is connected.
  bool SendCommand(const char* command);
  bool SendCommand(const String& command);

  // Check connection state.
  bool IsConnected() const;

  // Register a callback invoked when a command arrives.
  void SetCommandCallback(BleCommandCallback callback);

  // Stop advertising and disconnect any client.
  void Stop();

private:
  friend class BleServerCallbacks;
  friend class BleRxCallbacks;

  bool initialized_ = false;
  bool connected_ = false;
  BleCommandCallback command_callback_ = nullptr;

  BLECharacteristic* tx_char_ = nullptr;
  BLECharacteristic* rx_char_ = nullptr;

  // Internal buffer for assembling received lines.
  static constexpr size_t kRxBufferSize = 256;
  char rx_buffer_[kRxBufferSize];
  size_t rx_len_ = 0;

  void HandleReceived(const uint8_t* data, size_t length);
  void ProcessLine(const char* line);
};

extern BleService ble_service;

#endif