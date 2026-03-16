/*
 * Pinclaw Noise Diagnostic Firmware
 *
 * Records PDM mic data and prints RMS noise level via Serial.
 * Tests different configurations to isolate the noise source:
 *
 *   Mode 0: Baseline — everything off, just PDM mic
 *   Mode 1: BLE advertising enabled
 *   Mode 2: BLE connected (wait for connection)
 *   Mode 3: I2S peripheral initialized (clock running, no audio)
 *   Mode 4: LED on (WS2812B)
 *   Mode 5: All peripherals on simultaneously
 *
 * Press PTT button (D5) to cycle through modes.
 * Serial output shows real-time RMS and peak values.
 */

#include <PDM.h>
#include <Adafruit_NeoPixel.h>
#include <bluefruit.h>

// Pin definitions (PCB V1.0)
#define PIN_PTT     5
#define PIN_ACTION  4
#define PIN_LED     6
#define PIN_MOTOR   7

// I2S pins (same as pinclaw_full)
#define PIN_I2S_DIN   1   // D1
#define PIN_I2S_LRCLK 2   // D2
#define PIN_I2S_BCLK  3   // D3

// Audio config
#define SAMPLE_RATE     16000
#define PDM_BUFFER_SIZE 512
#define ANALYSIS_WINDOW 16000  // 1 second of samples

// PDM buffers
short pdmBuffer[PDM_BUFFER_SIZE];
volatile int pdmSamplesRead = 0;

// Analysis buffers
int64_t sumSquares = 0;
int32_t peakVal = 0;
int32_t sampleCount = 0;
int32_t dcSum = 0;  // for DC offset calculation

// Mode
int currentMode = 0;
const int NUM_MODES = 6;
const char* modeNames[] = {
  "0: BASELINE (PDM only, all off)",
  "1: BLE advertising ON",
  "2: BLE connected (waiting...)",
  "3: I2S clock running",
  "4: LED on",
  "5: ALL peripherals ON"
};

// LED
Adafruit_NeoPixel pixel(1, PIN_LED, NEO_GRB + NEO_KHZ800);

// Debounce
unsigned long lastButtonPress = 0;

// BLE
BLEUart bleuart;
volatile bool bleConnected = false;

// I2S state
bool i2sRunning = false;

// ============================================================
// PDM callback
// ============================================================
void onPDMdata() {
  int bytesAvailable = PDM.available();
  PDM.read(pdmBuffer, bytesAvailable);
  pdmSamplesRead = bytesAvailable / 2;
}

// ============================================================
// I2S setup (clock only, no audio data)
// ============================================================
void startI2S() {
  if (i2sRunning) return;

  NRF_I2S->CONFIG.MODE      = I2S_CONFIG_MODE_MODE_Master;
  NRF_I2S->CONFIG.TXEN      = I2S_CONFIG_TXEN_TXEN_Enabled;
  NRF_I2S->CONFIG.RXEN      = I2S_CONFIG_RXEN_RXEN_Disabled;
  NRF_I2S->CONFIG.MCKEN     = I2S_CONFIG_MCKEN_MCKEN_Enabled;
  NRF_I2S->CONFIG.MCKFREQ   = I2S_CONFIG_MCKFREQ_MCKFREQ_32MDIV31;
  NRF_I2S->CONFIG.RATIO     = I2S_CONFIG_RATIO_RATIO_32X;
  NRF_I2S->CONFIG.SWIDTH    = I2S_CONFIG_SWIDTH_SWIDTH_16Bit;
  NRF_I2S->CONFIG.ALIGN     = I2S_CONFIG_ALIGN_ALIGN_Left;
  NRF_I2S->CONFIG.FORMAT    = I2S_CONFIG_FORMAT_FORMAT_I2S;
  NRF_I2S->CONFIG.CHANNELS  = I2S_CONFIG_CHANNELS_CHANNELS_Stereo;

  NRF_I2S->PSEL.SCK  = (g_ADigitalPinMap[PIN_I2S_BCLK]);
  NRF_I2S->PSEL.LRCK = (g_ADigitalPinMap[PIN_I2S_LRCLK]);
  NRF_I2S->PSEL.SDOUT = (g_ADigitalPinMap[PIN_I2S_DIN]);
  NRF_I2S->PSEL.SDIN  = I2S_PSEL_SDIN_CONNECT_Disconnected;
  NRF_I2S->PSEL.MCK   = I2S_PSEL_MCK_CONNECT_Disconnected;

  // Allocate a silent buffer
  static int32_t silentBuf[256] __attribute__((aligned(4)));
  memset(silentBuf, 0, sizeof(silentBuf));

  NRF_I2S->TXD.PTR = (uint32_t)silentBuf;
  NRF_I2S->RXTXD.MAXCNT = 256;

  NRF_I2S->ENABLE = 1;
  NRF_I2S->TASKS_START = 1;

  i2sRunning = true;
  Serial.println("[I2S] Clock started");
}

void stopI2S() {
  if (!i2sRunning) return;
  NRF_I2S->TASKS_STOP = 1;
  delay(10);
  NRF_I2S->ENABLE = 0;

  // Reset pins to prevent floating clocks
  pinMode(PIN_I2S_BCLK, INPUT);
  pinMode(PIN_I2S_LRCLK, INPUT);
  pinMode(PIN_I2S_DIN, INPUT);

  i2sRunning = false;
  Serial.println("[I2S] Clock stopped");
}

// ============================================================
// BLE callbacks
// ============================================================
void connect_callback(uint16_t conn_handle) {
  (void)conn_handle;
  bleConnected = true;
  Serial.println("[BLE] Connected!");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;
  bleConnected = false;
  Serial.println("[BLE] Disconnected");
}

// ============================================================
// Apply mode configuration
// ============================================================
void applyMode(int mode) {
  // First, turn everything off
  Bluefruit.Advertising.stop();
  stopI2S();
  pixel.setPixelColor(0, 0, 0, 0);
  pixel.show();
  digitalWrite(PIN_MOTOR, LOW);

  // Stop and restart PDM to get a clean baseline
  PDM.end();
  delay(50);

  Serial.println("========================================");
  Serial.print("[MODE] Switching to: ");
  Serial.println(modeNames[mode]);
  Serial.println("========================================");

  switch (mode) {
    case 0:
      // Baseline: nothing else on
      break;

    case 1:
      // BLE advertising
      Bluefruit.Advertising.start(0);
      Serial.println("[BLE] Advertising started");
      break;

    case 2:
      // BLE connected — start advertising and wait
      Bluefruit.Advertising.start(0);
      Serial.println("[BLE] Advertising... connect your phone to test");
      break;

    case 3:
      // I2S clock running
      startI2S();
      break;

    case 4:
      // LED on (WS2812B protocol generates high-freq signals)
      pixel.setPixelColor(0, 0, 0, 50);  // dim blue
      pixel.show();
      Serial.println("[LED] Blue LED on");
      break;

    case 5:
      // Everything on
      Bluefruit.Advertising.start(0);
      startI2S();
      pixel.setPixelColor(0, 0, 0, 50);
      pixel.show();
      Serial.println("[ALL] All peripherals enabled");
      break;
  }

  // Restart PDM
  delay(100);  // let things settle
  if (!PDM.begin(1, SAMPLE_RATE)) {
    Serial.println("[ERROR] PDM start failed!");
  }

  // Reset analysis
  sumSquares = 0;
  peakVal = 0;
  sampleCount = 0;
  dcSum = 0;
}

// ============================================================
// Process and analyze PDM samples
// ============================================================
void processSamples() {
  if (pdmSamplesRead == 0) return;

  int count = pdmSamplesRead;
  pdmSamplesRead = 0;

  for (int i = 0; i < count; i++) {
    int16_t sample = pdmBuffer[i];
    dcSum += sample;

    int32_t absSample = abs((int32_t)sample);
    if (absSample > peakVal) peakVal = absSample;

    sumSquares += (int64_t)sample * sample;
    sampleCount++;
  }

  // Print analysis every 1 second
  if (sampleCount >= ANALYSIS_WINDOW) {
    float rms = sqrt((double)sumSquares / sampleCount);
    float dcOffset = (float)dcSum / sampleCount;

    // Also calculate AC-only RMS (remove DC offset)
    // RMS_ac = sqrt(RMS_total^2 - DC^2)
    float rmsAC = sqrt(max(0.0, (double)sumSquares / sampleCount - dcOffset * dcOffset));

    // Convert to approximate dB (relative to 16-bit full scale)
    float dbFS = 20.0 * log10(max(1.0f, rms) / 32768.0);
    float dbFS_AC = 20.0 * log10(max(1.0f, rmsAC) / 32768.0);

    Serial.print("[NOISE] Mode ");
    Serial.print(currentMode);
    Serial.print(" | RMS: ");
    Serial.print(rms, 1);
    Serial.print(" | AC-RMS: ");
    Serial.print(rmsAC, 1);
    Serial.print(" | Peak: ");
    Serial.print(peakVal);
    Serial.print(" | DC offset: ");
    Serial.print(dcOffset, 1);
    Serial.print(" | dBFS: ");
    Serial.print(dbFS, 1);
    Serial.print(" | AC-dBFS: ");
    Serial.println(dbFS_AC, 1);

    // Visual bar (AC RMS level)
    int barLen = min(50, (int)(rmsAC / 100));
    Serial.print("  [");
    for (int i = 0; i < 50; i++) {
      Serial.print(i < barLen ? '#' : ' ');
    }
    Serial.println("]");

    // Reset for next window
    sumSquares = 0;
    peakVal = 0;
    sampleCount = 0;
    dcSum = 0;
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);  // wait up to 3s for Serial

  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║   PINCLAW NOISE DIAGNOSTIC FIRMWARE      ║");
  Serial.println("║                                          ║");
  Serial.println("║   Press PTT (D5) to cycle modes          ║");
  Serial.println("║   Watch Serial for RMS noise levels      ║");
  Serial.println("║                                          ║");
  Serial.println("║   Lower RMS/dBFS = less noise = better   ║");
  Serial.println("╚══════════════════════════════════════════╝");
  Serial.println();

  // Button
  pinMode(PIN_PTT, INPUT_PULLUP);
  pinMode(PIN_ACTION, INPUT_PULLUP);

  // Motor off
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);

  // I2S pins as input initially
  pinMode(PIN_I2S_DIN, INPUT);
  pinMode(PIN_I2S_LRCLK, INPUT);
  pinMode(PIN_I2S_BCLK, INPUT);

  // LED
  pixel.begin();
  pixel.setPixelColor(0, 0, 0, 0);
  pixel.show();

  // BLE init (but don't start advertising yet)
  Bluefruit.begin();
  Bluefruit.setName("Pinclaw-Noise-Test");
  Bluefruit.setTxPower(4);
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  bleuart.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 244);
  Bluefruit.Advertising.setFastTimeout(30);

  // PDM
  PDM.onReceive(onPDMdata);
  PDM.setBufferSize(PDM_BUFFER_SIZE * 2);

  // Start in mode 0
  applyMode(0);

  Serial.println("[READY] Recording noise levels...");
  Serial.println();
}

// ============================================================
// Main loop
// ============================================================
void loop() {
  // Check button press
  if (digitalRead(PIN_PTT) == LOW) {
    if (millis() - lastButtonPress > 300) {
      lastButtonPress = millis();
      currentMode = (currentMode + 1) % NUM_MODES;
      applyMode(currentMode);
    }
    // Wait for release
    while (digitalRead(PIN_PTT) == LOW) delay(10);
  }

  // Process mic data
  processSamples();

  delay(1);
}
