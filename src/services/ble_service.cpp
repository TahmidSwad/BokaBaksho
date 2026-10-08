#include "services/ble_service.h"

// ==========================================================
// GLOBAL INSTANCE
// ==========================================================

BleService ble_service;

// ==========================================================
// INTERNAL CALLBACKS FOR BLE
// ==========================================================

class BleServerCallbacks : public BLEServerCallbacks {
public:
  void onConnect(BLEServer* server) override {
    ble_service.connected_ = true;
    Serial.println("BLE_CLIENT_CONNECTED");
  }
  void onDisconnect(BLEServer* server) override {
    ble_service.connected_ = false;
    ble_service.rx_len_ = 0;
    Serial.println("BLE_CLIENT_DISCONNECTED");
    // Restart advertising so the next client can connect.
    BLEDevice::startAdvertising();
  }
};

class BleRxCallbacks : public BLECharacteristicCallbacks {
public:
  void onWrite(BLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.length() > 0) {
      ble_service.HandleReceived(
          reinterpret_cast<const uint8_t*>(value.data()), value.length());
    }
  }
};

// ==========================================================
// INITIALIZATION
// ==========================================================

bool BleService::Begin() {
  BLEDevice::init(kBleDeviceName);
  BLEDevice::setMTU(517);  // max BLE MTU

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new BleServerCallbacks());

  BLEService* service = server->createService(BLE_SERVICE_UUID);

  // ---- TX characteristic (notify: ESP32 -> Python) ----
  tx_char_ = service->createCharacteristic(
      BLE_TX_CHAR_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  tx_char_->addDescriptor(new BLE2902());  // enable notifications

  // ---- RX characteristic (write: Python -> ESP32) ----
  rx_char_ = service->createCharacteristic(
      BLE_RX_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  rx_char_->setCallbacks(new BleRxCallbacks());

  // Start the service and begin advertising.
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  // Advertise a preferred connection interval range (slave connection
  // interval AD field, units of 1.25 ms): 0x06 = 7.5 ms, 0x12 = 22.5 ms.
  // These are separate fields — setMinPreferred() only writes min_interval,
  // so calling it twice would discard the 0x06 minimum and leave the maximum
  // at the BLEAdvertising() default of 0x40 (80 ms).
  advertising->setMinPreferred(0x06);  // connection interval min
  advertising->setMaxPreferred(0x12);  // connection interval max

  BLEDevice::startAdvertising();

  initialized_ = true;
  Serial.println("BLE_SERVICE_INIT_OK");
  return true;
}

// ==========================================================
// STOP
// ==========================================================

void BleService::Stop() {
  if (!initialized_) return;
  BLEDevice::getAdvertising()->stop();
  connected_ = false;
  initialized_ = false;
  Serial.println("BLE_SERVICE_STOPPED");
}

// ==========================================================
// SEND COMMAND (to Python)
// ==========================================================

bool BleService::SendCommand(const char* command) {
  if (!connected_ || tx_char_ == nullptr || command == nullptr) {
    return false;
  }
  std::string str(command);
  str += "\n";
  tx_char_->setValue(str);
  tx_char_->notify();
  return true;
}

bool BleService::SendCommand(const String& command) {
  return SendCommand(command.c_str());
}

// ==========================================================
// RECEIVED DATA (from Python)
// ==========================================================

void BleService::HandleReceived(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    char c = (char)data[i];
    if (c == '\n' || c == '\r') {
      if (rx_len_ > 0) {
        rx_buffer_[rx_len_] = '\0';
        ProcessLine(rx_buffer_);
        rx_len_ = 0;
      }
    } else {
      if (rx_len_ < kRxBufferSize - 1) {
        rx_buffer_[rx_len_++] = c;
      }
    }
  }
}

void BleService::ProcessLine(const char* line) {
  Serial.print("BLE_RX: ");
  Serial.println(line);

  // Invoke the app callback if registered.
  if (command_callback_ != nullptr) {
    command_callback_(line);
  }

  // Also post a System event so the AppManager / apps can
  // react if needed.
  Event event;
  event.type = EventType::System;
  event.sender = AppId::Count;
  (void)event;  // kept for future expansion
}

// ==========================================================
// STATE QUERIES
// ==========================================================

bool BleService::IsConnected() const {
  return connected_;
}

void BleService::SetCommandCallback(BleCommandCallback callback) {
  command_callback_ = callback;
}

// ==========================================================
// EVENT SUBSCRIBER
// ==========================================================

void BleService::OnEvent(const Event& event) {
  // Apps post commands to the event bus that the BLE service
  // forwards to the Python client. Currently unused but kept
    // for extensibility.
}