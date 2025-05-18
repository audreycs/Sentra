#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <Adafruit_NeoPixel.h>
#include <Ticker.h>

#define RING_PIN       12      // data pin for your NeoPixel ring
#define NUMPIXELS      24      // change to match your ring’s LED count

#define HR_SERVICE_UUID "0000180d-0000-1000-8000-00805f9b34fb"
#define HR_CHAR_UUID    "00002a37-0000-1000-8000-00805f9b34fb"

const unsigned long ON_TIME = 250;


// NeoPixel setup
Adafruit_NeoPixel ring(NUMPIXELS, RING_PIN, NEO_GRB + NEO_KHZ800);

BLEAddress               polarAddress = BLEAddress("");
BLEClient*               pClient       = nullptr;
BLERemoteCharacteristic* pRemoteHR     = nullptr;
bool                     connected     = false;

// heart‐rate & tickers
static uint8_t           heartRate     = 0;
static Ticker            beatTicker;
static Ticker            offTicker;
static uint32_t          blinkColor;


void onBeat() {
  // set all pixels to blinkColor
  for (int i = 0; i < NUMPIXELS; i++) {
    ring.setPixelColor(i, blinkColor);
  }
  ring.show();
  // schedule turning off after ON_TIME ms
  offTicker.once_ms(ON_TIME, [](){
    ring.clear();
    ring.show();
  });
}

// scan callback: find Polar OH1 advertising HR service
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

// HR notification callback
void hrNotifyCallback(
  BLERemoteCharacteristic*,
  uint8_t* pData,
  size_t,
  bool
) {
  // parse 8‐bit vs 16‐bit HR
  if (pData[0] & 0x01) {
    heartRate = pData[1] | (pData[2] << 8);
  } else {
    heartRate = pData[1];
  }

  // print to Serial Monitor
  Serial.print("Heart Rate: ");
  Serial.print(heartRate);
  Serial.println(" BPM");

  // set blinkColor based on thresholds
  if (heartRate > 90) {
    blinkColor = ring.Color(255, 0, 0);   // red
  } else if (heartRate >= 70) {
    blinkColor = ring.Color(0, 255, 0);   // green
  } else {
    blinkColor = ring.Color(0, 0, 255);   // blue
  }

  // restart beatTicker at new interval
  beatTicker.detach();
  if (heartRate > 0) {
    unsigned long interval = 60000UL / heartRate;  // ms between beats
    beatTicker.attach_ms(interval, onBeat);
  }
}

void setup() {
  // start Serial for debug
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
    Serial.print("Connecting to ");
    Serial.println(polarAddress.toString().c_str());

    pClient = BLEDevice::createClient();
    if (pClient->connect(polarAddress)) {
      Serial.println("Connected — discovering HR service");
      BLERemoteService* service = pClient->getService(HR_SERVICE_UUID);
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
  // Everything else (blinking & printing) runs via callbacks & tickers
}

