// Minimal BLE advertising test — just BLE, nothing else
#include <bluefruit.h>
#include <Adafruit_NeoPixel.h>

#define PIN_LED_DIN 6
Adafruit_NeoPixel pixel(1, PIN_LED_DIN, NEO_GRB + NEO_KHZ800);

BLEService testService("12345678-1234-1234-1234-123456789ABC");
BLECharacteristic textChar("12345678-1234-1234-1234-123456789ABD");

void setup() {
  Serial.begin(115200);

  pixel.begin();
  pixel.setBrightness(20);
  pixel.setPixelColor(0, pixel.Color(20, 0, 0));  // red = starting
  pixel.show();

  Serial.println("[TEST] Minimal BLE test v2");

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  bond_clear_prph();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("Pinclaw-1");
  Serial.println("[TEST] BLE initialized, bonds cleared");

  testService.begin();

  textChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY | CHR_PROPS_WRITE);
  textChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  textChar.setMaxLen(20);
  textChar.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(testService);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);

  pixel.setPixelColor(0, pixel.Color(0, 20, 0));  // green = BLE ready
  pixel.show();

  Serial.println("[TEST] BLE advertising as 'Pinclaw-1' — LED should be GREEN");
}

uint32_t lastBlink = 0;
bool blinkState = true;

void loop() {
  // Blink green to show firmware is alive
  if (millis() - lastBlink > 1000) {
    lastBlink = millis();
    blinkState = !blinkState;
    pixel.setPixelColor(0, blinkState ? pixel.Color(0, 20, 0) : pixel.Color(0, 0, 0));
    pixel.show();
  }
  delay(10);
}
