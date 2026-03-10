// Pinclaw Hardware Test - Step 3: PDM Microphone
// Board: Seeed XIAO nRF52840 Sense

#include <Adafruit_TinyUSB.h>  // Required for USB Serial on nRF52840
#include <PDM.h>

#define SAMPLE_RATE   16000  // 16kHz - matches Pinclaw protocol
#define CHANNELS      1      // Mono
#define BUFFER_SIZE   512    // samples per callback

short sampleBuffer[BUFFER_SIZE];
volatile int samplesRead = 0;

void printLevel(int level) {
  int bars = map(level, 0, 20000, 0, 40);
  bars = constrain(bars, 0, 40);
  Serial.print("[");
  for (int i = 0; i < 40; i++) {
    Serial.print(i < bars ? "#" : " ");
  }
  Serial.print("] ");
  Serial.println(level);
}

void onPDMdata() {
  int bytesAvailable = PDM.available();
  PDM.read(sampleBuffer, bytesAvailable);
  samplesRead = bytesAvailable / 2;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  Serial.println("=== Pinclaw Microphone Test ===");
  Serial.println("Board: XIAO nRF52840 Sense");
  Serial.print("Sample rate: ");
  Serial.print(SAMPLE_RATE);
  Serial.println(" Hz");
  Serial.println("Speak into the microphone...");
  Serial.println();

  PDM.onReceive(onPDMdata);
  PDM.setBufferSize(BUFFER_SIZE * 2);

  if (!PDM.begin(CHANNELS, SAMPLE_RATE)) {
    Serial.println("ERROR: PDM microphone failed!");
    digitalWrite(LED_RED, LOW); // red = error
    while (1);
  }

  Serial.println("Microphone OK! GREEN = listening");
  digitalWrite(LED_GREEN, LOW);
}

void loop() {
  if (samplesRead > 0) {
    int maxVal = 0;
    for (int i = 0; i < samplesRead; i++) {
      int val = abs(sampleBuffer[i]);
      if (val > maxVal) maxVal = val;
    }

    printLevel(maxVal);

    // Blue flash when voice detected
    if (maxVal > 3000) {
      digitalWrite(LED_BLUE, LOW);
    } else {
      digitalWrite(LED_BLUE, HIGH);
    }

    samplesRead = 0;
  }
}
