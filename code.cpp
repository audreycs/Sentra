#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <Adafruit_NeoPixel.h>
#include <Ticker.h>

#define RING_PIN       12      // NeoPixel data pin
#define NUMPIXELS      24      // change to match your ring’s LED count

#define HR_SERVICE_UUID "0000180d-0000-1000-8000-00805f9b34fb"
#define HR_CHAR_UUID    "00002a37-0000-1000-8000-00805f9b34fb"

const unsigned long ON_TIME = 250;

Adafruit_NeoPixel ring(NUMPIXELS, RING_PIN, NEO_GRB + NEO_KHZ800);

BLEAddress               polarAddress = BLEAddress("");
BLEClient*               pClient       = nullptr;
BLERemoteCharacteristic* pRemoteHR     = nullptr;
bool                     connected     = false;

static uint8_t           heartRate     = 0;
static Ticker            beatTicker;
static Ticker            offTicker;
static uint32_t          blinkColor;

// This fires once per beat and handles the NeoPixel blink
void onBeat() {
  for (int i = 0; i < NUMPIXELS; i++) {
    ring.setPixelColor(i, blinkColor);
  }
  ring.show();
  offTicker.once_ms(ON_TIME, [](){
    ring.clear();
    ring.show();
  });
}

// Scan callback: discover your Polar OH1 advertising the HR service
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

// Notification callback: parse HR and schedule blinking; print only numeric HR
void hrNotifyCallback(
  BLERemoteCharacteristic*,
  uint8_t* pData,
  size_t,
  bool
) {
  // parse 8-bit vs 16-bit heart rate
  if (pData[0] & 0x01) {
    heartRate = pData[1] | (pData[2] << 8);
  } else {
    heartRate = pData[1];
  }

  // Send only the numeric value to Serial for plotting
  Serial.print("Heart Rate: ");
  Serial.print(heartRate);
  Serial.println(" BPM");

  // choose color by threshold
  if (heartRate > 85) {
    blinkColor = ring.Color(255, 0, 0);   // red
  } else if (heartRate >= 70) {
    blinkColor = ring.Color(0, 255, 0);   // green
  } else {
    blinkColor = ring.Color(0, 0, 255);   // blue
  }

  // restart the beat ticker at the new interval
  beatTicker.detach();
  if (heartRate > 0) {
    unsigned long interval = 60000UL / heartRate;  // ms between beats
    beatTicker.attach_ms(interval, onBeat);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("Starting BLE scan for Polar OH1...");

  // initialize NeoPixel ring
  ring.begin();
  ring.setBrightness(40);
  ring.clear();
  ring.show();

  // begin BLE scan
  BLEDevice::init("");
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setActiveScan(true);
  scan->start(10);  // scan for 10 seconds
}

void loop() {
  // connect & subscribe once address is known
  if (!connected && polarAddress.toString().length() > 0) {
    Serial.print("Connecting to "); Serial.println(polarAddress.toString().c_str());
    pClient = BLEDevice::createClient();
    if (pClient->connect(polarAddress)) {
      auto service = pClient->getService(HR_SERVICE_UUID);
      if (service) {
        pRemoteHR = service->getCharacteristic(HR_CHAR_UUID);
        if (pRemoteHR && pRemoteHR->canNotify()) {
          pRemoteHR->registerForNotify(hrNotifyCallback);
          Serial.println("Subscribed to HR notifications");
          connected = true;
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
  // All blinking and serial printing happen in callbacks
}
