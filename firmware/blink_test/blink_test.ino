// Pinclaw Hardware Test - Step 2: Blink
// Board: Seeed XIAO nRF52840 Sense

#include <Adafruit_TinyUSB.h>  // Required for USB Serial on nRF52840

// LED_RED (11), LED_GREEN (13), LED_BLUE (12) already defined in variant.h

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); // wait up to 3s

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // All off (HIGH = off, active-low LEDs)
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  Serial.println("=== Pinclaw Hardware Test ===");
  Serial.println("Board: XIAO nRF52840 Sense");
  Serial.println("LED cycling: RED -> GREEN -> BLUE");
}

void loop() {
  digitalWrite(LED_RED, LOW);
  Serial.println("RED");
  delay(500);
  digitalWrite(LED_RED, HIGH);

  digitalWrite(LED_GREEN, LOW);
  Serial.println("GREEN");
  delay(500);
  digitalWrite(LED_GREEN, HIGH);

  digitalWrite(LED_BLUE, LOW);
  Serial.println("BLUE");
  delay(500);
  digitalWrite(LED_BLUE, HIGH);

  delay(200);
}
