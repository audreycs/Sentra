#include <BluetoothSerial.h>               // for Classic-BT SPP
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <Adafruit_NeoPixel.h>
#include <Ticker.h>

#define RING_PIN       12
#define NUMPIXELS      24

#define HR_SERVICE_UUID "0000180d-0000-1000-8000-00805f9b34fb"
#define HR_CHAR_UUID    "00002a37-0000-1000-8000-00805f9b34fb"

const unsigned long ON_TIME = 250;

Adafruit_NeoPixel ring(NUMPIXELS, RING_PIN, NEO_GRB + NEO_KHZ800);
BluetoothSerial   SerialBT;    // <— SPP port

BLEAddress               polarAddress = BLEAddress("");
BLEClient*               pClient       = nullptr;
BLERemoteCharacteristic* pRemoteHR     = nullptr;
bool                     connected     = false;

static uint8_t           heartRate     = 0;
static Ticker            beatTicker;
static Ticker            offTicker;
static uint32_t          blinkColor;

void onBeat() {
  for (int i = 0; i < NUMPIXELS; i++)
    ring.setPixelColor(i, blinkColor);
  ring.show();
  offTicker.once_ms(ON_TIME, [](){
    ring.clear();
    ring.show();
  });
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (dev.haveServiceUUID() &&
        dev.isAdvertisingService(BLEUUID(HR_SERVICE_UUID))) {
      Serial.println("→ Found HR service");
      polarAddress = dev.getAddress();
      BLEDevice::getScan()->stop();
    }
  }
};

void hrNotifyCallback(
  BLERemoteCharacteristic*,
  uint8_t* pData,
  size_t,
  bool
) {
  // parse HR value
  if (pData[0] & 0x01) heartRate = pData[1] | (pData[2] << 8);
  else               heartRate = pData[1];

  // Print to USB Serial
  Serial.print("Heart Rate: ");
  Serial.print(heartRate);
  Serial.println(" BPM");

  // Print over BluetoothSerial
  SerialBT.print("Heart Rate: ");
  SerialBT.print(heartRate);
  SerialBT.println(" BPM");

  // pick blink color
  if (heartRate > 90)      blinkColor = ring.Color(255,0,0);
  else if (heartRate >=70) blinkColor = ring.Color(0,255,0);
  else                     blinkColor = ring.Color(0,0,255);

  // restart ticker
  beatTicker.detach();
  if (heartRate > 0) {
    unsigned long interval = 60000UL/heartRate;
    beatTicker.attach_ms(interval, onBeat);
  }
}

void setup(){
  // start USB Serial
  Serial.begin(115200);
  while(!Serial) delay(10);
  Serial.println("Starting BLE→Polar OH1…");

  // start Bluetooth SPP
  SerialBT.begin("ESP32_HR");
  Serial.println("Bluetooth SPP as “ESP32_HR”");

  // NeoPixel init
  ring.begin();
  ring.setBrightness(40);
  ring.clear();
  ring.show();

  // BLE scan
  BLEDevice::init("");
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  scan->setInterval(100);
  scan->setWindow( 99);
  scan->setActiveScan(true);
  scan->start(10);
}

void loop(){
  if (!connected && polarAddress.toString().length()>0) {
    Serial.print("Connecting to "); Serial.println(polarAddress.toString().c_str());
    pClient = BLEDevice::createClient();
    if (pClient->connect(polarAddress)) {
      if (auto svc = pClient->getService(HR_SERVICE_UUID)) {
        if (auto chr = svc->getCharacteristic(HR_CHAR_UUID)) {
          if (chr->canNotify()) {
            chr->registerForNotify(hrNotifyCallback);
            Serial.println("Subscribed to HR notifications");
            connected=true;
          }
        }
      }
    }
    if (!connected) {
      Serial.println("Connect failed; retrying in 5s");
      pClient->disconnect();
      delay(5000);
      BLEDevice::getScan()->start(10);
    }
  }
  // blinking & serial printing all happen in callbacks
}
