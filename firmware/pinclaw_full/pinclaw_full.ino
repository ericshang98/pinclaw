// Pinclaw Full Firmware — XIAO nRF52840 Sense (PCB V1.0)
// BLE Audio (Opus) + Buttons + I2S Speaker + SD Card + Motor + WS2812B LED
//
// Pin Map (PCB V1.0 — pinc sch2):
//   D0  = SD Card CS
//   D1  = I2S DIN  (MAX98357A audio data)
//   D2  = I2S LRCLK (MAX98357A word select)
//   D3  = I2S BCLK  (MAX98357A bit clock)
//   D5  = Single Button (active LOW, internal pullup)
//   D6  = WS2812B LED DIN
//   D7  = Vibration Motor (via 2N7002 MOSFET, HIGH=ON)
//   D8  = SD Card SCK  (SPI)
//   D9  = SD Card MISO (SPI)
//   D10 = SD Card MOSI (SPI)

#include <bluefruit.h>
#include <PDM.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_NeoPixel.h>
#include <nrf.h>

extern "C" {
  #include "opus.h"
}

// ============================================================
// Pin Definitions (PCB V1.0)
// ============================================================
#define PIN_SD_CS       0   // D0 — SD card chip select
#define PIN_I2S_DIN     1   // D1 — I2S data to MAX98357A
#define PIN_I2S_LRCLK   2   // D2 — I2S word select
#define PIN_I2S_BCLK    3   // D3 — I2S bit clock
#define PIN_BUTTON      5   // D5 — Single button (active LOW, internal pullup)
#define PIN_LED_DIN     6   // D6 — WS2812B RGB LED
#define PIN_MOTOR       7   // D7 — Vibration motor

// GPIO numbers for I2S register config (from variant.cpp)
#define GPIO_I2S_DIN    3   // P0.03 (D1)
#define GPIO_I2S_LRCLK  28  // P0.28 (D2)
#define GPIO_I2S_BCLK   29  // P0.29 (D3)

// ============================================================
// BLE UUIDs — must match iOS app
// ============================================================
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789ABC"
#define TEXT_CHAR_UUID      "12345678-1234-1234-1234-123456789ABD"
#define AUDIO_CHAR_UUID     "12345678-1234-1234-1234-123456789ABE"
#define HEARTBEAT_CHAR_UUID "12345678-1234-1234-1234-123456789ABF"
#define SPEAKER_CHAR_UUID   "12345678-1234-1234-1234-123456789AC0"

// ============================================================
// BLE packet types
// ============================================================
#define PKT_START     0x01
#define PKT_DATA      0x02
#define PKT_END       0x03
#define PKT_HEARTBEAT 0x04
#define CMD_PLAY      0x20  // Action button → phone triggers Interactive AI
#define CMD_SHUTDOWN  0x40  // App commands device to shut down

// Speaker (reverse audio) packet types
#define SPK_START     0x30
#define SPK_DATA      0x31
#define SPK_END       0x32
#define SPK_STOP      0x33

// ============================================================
// Audio config
// ============================================================
#define SAMPLE_RATE        16000
#define PDM_BUFFER_SIZE    512
#define MAX_RECORD_SEC     10
#define SILENCE_THRESHOLD  600   // tuned for +12dB PDM gain
#define SILENCE_TIMEOUT_MS 5000

// BLE MTU
#define BLE_MTU            247
#define DATA_HEADER_SIZE   3
#define DATA_PAYLOAD_SIZE  (BLE_MTU - DATA_HEADER_SIZE)

// ============================================================
// Opus encoder config
// ============================================================
#define OPUS_FRAME_SAMPLES 160   // 10ms at 16kHz
#define OPUS_MAX_PACKET    320   // max encoded frame bytes
#define OPUS_ENCODER_SIZE  7180  // opus_encoder_get_size(1)
#define CODEC_OPUS         0x14  // codec ID for BLE START packet

// Opus offline buffer (for SD card saving when BLE not connected)
#define OPUS_OFFLINE_BUF_SIZE  80000  // restored — testing if memory layout was the issue
#define OPUS_MAX_FRAMES        600    // 10s * 100 fps = 1000 max, 600 is comfortable

// ============================================================
// Single-button config
// ============================================================
#define DEBOUNCE_MS        50
#define LONG_PRESS_MS      500   // hold > 500ms = start recording
#define DOUBLE_TAP_MS      300   // window for second tap

// ============================================================
// I2S tone config
// ============================================================
// MCK = 32MHz / 32 = 1MHz, RATIO = 32X → LRCLK = 31250 Hz
#define I2S_SAMPLE_RATE    31250
#define I2S_BUF_SAMPLES    1024   // samples per DMA buffer
// DMA buffer: stereo 32-bit words (left + right packed as int32)
static int32_t i2sTxBuf[I2S_BUF_SAMPLES] __attribute__((aligned(4)));

// ============================================================
// State machine
// ============================================================
enum FirmwareState {
  STATE_INIT,
  STATE_IDLE,
  STATE_RECORDING,
  STATE_SD_SYNC,
  STATE_PLAYING_SPK
};

volatile FirmwareState currentState = STATE_INIT;

// ============================================================
// BLE objects
// ============================================================
BLEService        pinclawService(SERVICE_UUID);
BLECharacteristic textChar(TEXT_CHAR_UUID);
BLECharacteristic audioChar(AUDIO_CHAR_UUID);
BLECharacteristic heartbeatChar(HEARTBEAT_CHAR_UUID);
BLECharacteristic speakerChar(SPEAKER_CHAR_UUID);

// ============================================================
// Speaker playback (reverse audio: phone → hardware)
// ============================================================
#define SPK_RING_SIZE    8192   // 8KB ring buffer (~1s of ADPCM @ 16kHz)
#define SPK_DMA_SAMPLES  256    // samples per DMA half-buffer

uint8_t spkRingBuf[SPK_RING_SIZE];
volatile uint32_t spkRingHead = 0;  // BLE write position
volatile uint32_t spkRingTail = 0;  // I2S consume position

static int32_t spkDmaBufA[SPK_DMA_SAMPLES] __attribute__((aligned(4)));
static int32_t spkDmaBufB[SPK_DMA_SAMPLES] __attribute__((aligned(4)));
volatile bool spkUsingBufA = true;
volatile bool spkStreamEnded = false;  // set when SPK_END received
volatile bool spkPlaying = false;

// Speaker ADPCM decoder state (separate from recorder encoder)
int16_t spkAdpcmPrev = 0;
int8_t  spkAdpcmIndex = 0;

// ============================================================
// WS2812B LED
// ============================================================
Adafruit_NeoPixel pixel(1, PIN_LED_DIN, NEO_GRB + NEO_KHZ800);

// ============================================================
// PDM microphone
// ============================================================
short pdmBuffer[PDM_BUFFER_SIZE];
volatile int pdmSamplesRead = 0;

// ============================================================
// Opus encoder state (statically allocated, no malloc)
// ============================================================
static uint8_t opusState[OPUS_ENCODER_SIZE] __attribute__((aligned(4)));
static OpusEncoder* opusEnc = (OpusEncoder*)opusState;

// PCM accumulator for Opus frames (160 samples = 10ms)
int16_t pcmAccum[OPUS_FRAME_SAMPLES];
int pcmAccumPos = 0;
uint16_t opusSeqNo = 0;
uint8_t opusOutBuf[OPUS_MAX_PACKET];

// Offline buffer for SD saving (when BLE not connected)
uint8_t opusOfflineBuf[OPUS_OFFLINE_BUF_SIZE];
uint16_t opusFrameLens[OPUS_MAX_FRAMES];
uint16_t opusOfflineFrameCount = 0;
uint32_t opusOfflineBufPos = 0;
bool opusOfflineMode = false;  // true when recording without BLE

// === 2nd-order Butterworth high-pass filter at 80Hz ===
#define HPF2_B0  0.96907f
#define HPF2_B1 -1.93815f
#define HPF2_B2  0.96907f
#define HPF2_A1 -1.93729f
#define HPF2_A2  0.93900f
float hpfX1 = 0, hpfX2 = 0;
float hpfY1 = 0, hpfY2 = 0;

// === Noise gate (two-stage) ===
#define SOFT_GATE_THRESHOLD  120
#define HARD_GATE_THRESHOLD_SQ (400L * 400L)

// Startup discard — skip first N ms of PDM data (transient)
#define STARTUP_DISCARD_MS 80
volatile bool startupDiscardDone = false;

// Silence detection
volatile uint32_t lastSoundTime = 0;
volatile uint32_t recordStartTime = 0;

// Keep ADPCM tables for speaker decoder only
static const int16_t stepTable[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
  34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
  143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
  494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
  1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
  4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
  12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
  32767
};

static const int8_t indexTable[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8
};

// ============================================================
// Deferred BLE command (set in callback, processed in loop)
// ============================================================
volatile uint8_t pendingBLECommand = 0;


// ============================================================
// Single-button state
// ============================================================
bool btnPressed = false;
bool lastBtnState = HIGH;
uint32_t lastBtnDebounce = 0;
uint32_t btnPressTime = 0;
uint32_t btnReleaseTime = 0;
uint8_t tapCount = 0;
bool btnRecording = false;  // true while long-press recording is active

// ============================================================
// SD Card
// ============================================================
SdFat sd;
bool sdReady = false;
uint16_t sdFileIndex = 0;  // next file number
#define SD_AUDIO_DIR "/audio"
#define SD_INDEX_FILE "/index.txt"

// ============================================================
// Heartbeat & connection
// ============================================================
uint16_t heartbeatCounter = 0;
uint32_t lastHeartbeatTime = 0;
#define HEARTBEAT_INTERVAL_MS 3000
volatile bool isConnected = false;

// ============================================================
// Power management — auto-shutdown after BLE disconnect
// ============================================================
#define AUTO_SHUTDOWN_MS      120000  // 2 min after BLE disconnect → deep sleep
#define POWER_ON_HOLD_MS      1500   // hold button 1.5s to confirm power on

volatile uint32_t disconnectTime = 0;
volatile bool autoShutdownPending = false;

// ============================================================
// Battery voltage (nRF52840 internal VBAT via P0.31)
// ============================================================
#define PIN_VBAT        31  // P0.31 — internal ADC for battery voltage
#define VBAT_DIVIDER    2   // on-board 1:1 voltage divider
uint16_t readBatteryMillivolts() {
  analogReference(AR_INTERNAL_3_0);  // 3.0V reference
  analogReadResolution(12);          // 12-bit (0-4095)
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) { raw += analogRead(PIN_VBAT); }
  raw /= 8;
  // Convert: (raw / 4095) * 3.0V * 2 (divider) * 1000 = millivolts
  return (uint16_t)((raw * 6000UL) / 4095);
}

// ============================================================
// I2S tone output (MAX98357A via nRF52840 hardware I2S)
// ============================================================
void i2sInit() {
  // Configure I2S pins using GPIO numbers
  NRF_I2S->PSEL.SCK   = GPIO_I2S_BCLK;   // P0.29 = D3
  NRF_I2S->PSEL.LRCK  = GPIO_I2S_LRCLK;  // P0.28 = D2
  NRF_I2S->PSEL.SDOUT = GPIO_I2S_DIN;     // P0.03 = D1
  NRF_I2S->PSEL.SDIN  = 0xFFFFFFFF;       // disconnect SDIN
  NRF_I2S->PSEL.MCK   = 0xFFFFFFFF;       // disconnect MCK (MAX98357A doesn't need it)

  // Master mode
  NRF_I2S->CONFIG.MODE     = I2S_CONFIG_MODE_MODE_Master;
  NRF_I2S->CONFIG.TXEN     = I2S_CONFIG_TXEN_TXEN_Enabled;
  NRF_I2S->CONFIG.RXEN     = I2S_CONFIG_RXEN_RXEN_Disabled;

  // MCK = 32MHz / 31 ≈ 1.032 MHz
  // RATIO = 32X → LRCLK = 1.032MHz / 32 ≈ 32258 Hz
  NRF_I2S->CONFIG.MCKFREQ  = I2S_CONFIG_MCKFREQ_MCKFREQ_32MDIV31;
  NRF_I2S->CONFIG.RATIO    = I2S_CONFIG_RATIO_RATIO_32X;

  // 16-bit sample, left-aligned, stereo (MAX98357A expects I2S standard)
  NRF_I2S->CONFIG.SWIDTH   = I2S_CONFIG_SWIDTH_SWIDTH_16Bit;
  NRF_I2S->CONFIG.ALIGN    = I2S_CONFIG_ALIGN_ALIGN_Left;
  NRF_I2S->CONFIG.FORMAT   = I2S_CONFIG_FORMAT_FORMAT_I2S;
  NRF_I2S->CONFIG.CHANNELS = I2S_CONFIG_CHANNELS_CHANNELS_Stereo;

  // Keep I2S data pin LOW when idle — prevents MAX98357A from amplifying floating pin noise
  pinMode(PIN_I2S_DIN, OUTPUT);
  digitalWrite(PIN_I2S_DIN, LOW);

  Serial.println("[I2S] Initialized (MAX98357A, ~32kHz)");
}

// Drive I2S pins LOW when not playing to keep MAX98357A in shutdown
// (MAX98357A auto-shuts down when BCLK stops, but floating DIN can cause noise)
void i2sQuietPins() {
  NRF_I2S->ENABLE = 0;
  // Disconnect I2S peripheral from pins temporarily
  NRF_I2S->PSEL.SCK   = 0xFFFFFFFF;
  NRF_I2S->PSEL.LRCK  = 0xFFFFFFFF;
  NRF_I2S->PSEL.SDOUT = 0xFFFFFFFF;
  // Drive all three pins LOW — ensures MAX98357A sees no clock and stays in shutdown
  pinMode(PIN_I2S_DIN, OUTPUT);
  digitalWrite(PIN_I2S_DIN, LOW);
  pinMode(PIN_I2S_LRCLK, OUTPUT);
  digitalWrite(PIN_I2S_LRCLK, LOW);
  pinMode(PIN_I2S_BCLK, OUTPUT);
  digitalWrite(PIN_I2S_BCLK, LOW);
}

// Reconnect I2S pins before playback
void i2sReconnectPins() {
  NRF_I2S->PSEL.SCK   = GPIO_I2S_BCLK;
  NRF_I2S->PSEL.LRCK  = GPIO_I2S_LRCLK;
  NRF_I2S->PSEL.SDOUT = GPIO_I2S_DIN;
}

void i2sPlayTone(uint16_t freq, uint16_t durationMs) {
  if (freq == 0 || durationMs == 0) return;

  // Ensure PDM is off to avoid clock noise
  PDM.end();

  // Reconnect I2S pins (they're held LOW when idle)
  i2sReconnectPins();

  // Fill buffer with sine wave approximation (square-ish with volume control)
  // Each int32_t word = left sample (upper 16) + right sample (lower 16)
  // MAX98357A default: left channel when LRCLK is low

  uint32_t samplesPerCycle = I2S_SAMPLE_RATE / freq;
  if (samplesPerCycle == 0) samplesPerCycle = 1;
  int16_t amplitude = 8000;  // moderate volume

  // Fill buffer with tone data
  for (uint32_t i = 0; i < I2S_BUF_SAMPLES; i++) {
    // Simple sine approximation using phase
    uint32_t phase = (i % samplesPerCycle) * 360 / samplesPerCycle;
    int16_t sample;

    // Piecewise sine approximation: good enough for tones
    if (phase < 90) {
      sample = (int16_t)((int32_t)amplitude * phase / 90);
    } else if (phase < 180) {
      sample = (int16_t)((int32_t)amplitude * (180 - phase) / 90);
    } else if (phase < 270) {
      sample = (int16_t)(-(int32_t)amplitude * (phase - 180) / 90);
    } else {
      sample = (int16_t)(-(int32_t)amplitude * (360 - phase) / 90);
    }

    // Pack stereo: same sample in both channels
    // nRF52840 I2S: 32-bit word = [left:16][right:16]
    i2sTxBuf[i] = ((int32_t)sample << 16) | ((uint16_t)sample);
  }

  // Set DMA buffer
  NRF_I2S->TXD.PTR = (uint32_t)i2sTxBuf;
  NRF_I2S->RXTXD.MAXCNT = I2S_BUF_SAMPLES;  // number of 32-bit words

  // Clear events
  NRF_I2S->EVENTS_TXPTRUPD = 0;
  NRF_I2S->EVENTS_STOPPED = 0;

  // Enable and start
  NRF_I2S->ENABLE = 1;
  NRF_I2S->TASKS_START = 1;

  // Play for the requested duration (buffer loops automatically via TXPTRUPD)
  // IMPORTANT: use delay(1) not delayMicroseconds() — must yield to FreeRTOS
  // so the BLE SoftDevice can process events and maintain the connection.
  uint32_t endTime = millis() + durationMs;
  while (millis() < endTime) {
    if (NRF_I2S->EVENTS_TXPTRUPD) {
      NRF_I2S->EVENTS_TXPTRUPD = 0;
      // Re-point to same buffer for continuous looping
      NRF_I2S->TXD.PTR = (uint32_t)i2sTxBuf;
    }
    delay(1);  // yield to RTOS — keeps BLE alive
  }

  // Stop I2S — then quiet pins to prevent noise
  NRF_I2S->TASKS_STOP = 1;
  while (!NRF_I2S->EVENTS_STOPPED) {
    delayMicroseconds(10);
  }
  NRF_I2S->EVENTS_STOPPED = 0;
  i2sQuietPins();  // Drive I2S pins LOW to keep MAX98357A silent
}

// ============================================================
// Tone sequences (using I2S)
// ============================================================
void toneStartup() {
  i2sPlayTone(800, 80);
  delay(60);
  i2sPlayTone(1200, 80);
  delay(60);
  i2sPlayTone(1600, 100);
}

void toneConnected() {
  i2sPlayTone(1000, 60);
  delay(40);
  i2sPlayTone(2000, 80);
}

void toneDisconnected() {
  i2sPlayTone(2000, 60);
  delay(40);
  i2sPlayTone(1000, 80);
}

void toneRecStart() {
  i2sPlayTone(1200, 60);
}

void toneRecStop() {
  i2sPlayTone(800, 60);
}

void toneError() {
  i2sPlayTone(300, 200);
}

void toneConfirm() {
  i2sPlayTone(1500, 40);
}

void toneSyncStart() {
  i2sPlayTone(600, 60);
  delay(40);
  i2sPlayTone(900, 60);
  delay(40);
  i2sPlayTone(1200, 60);
}

void toneSyncDone() {
  i2sPlayTone(1200, 60);
  delay(40);
  i2sPlayTone(1600, 60);
  delay(40);
  i2sPlayTone(2000, 80);
}

void toneShutdown() {
  i2sPlayTone(1600, 100);
  delay(60);
  i2sPlayTone(1000, 100);
  delay(60);
  i2sPlayTone(600, 150);
}

void toneBleEnabled() {
  i2sPlayTone(800, 60);
  delay(40);
  i2sPlayTone(1200, 60);
  delay(40);
  i2sPlayTone(1600, 60);
  delay(40);
  i2sPlayTone(2000, 80);
}

// ============================================================
// Power management — System OFF deep sleep
// ============================================================

// Minimal deep sleep — used before peripherals are initialized
void enterDeepSleepRaw() {
  uint32_t pinNum = g_ADigitalPinMap[PIN_BUTTON];
  nrf_gpio_cfg_sense_input(pinNum, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  NRF_POWER->SYSTEMOFF = 1;
  while (1);  // never reached
}

// Full deep sleep — shutdown tone + LED off + System OFF
void enterDeepSleep() {
  Serial.println("[PWR] Entering deep sleep...");

  // Stop recording if active
  if (currentState == STATE_RECORDING) {
    PDM.end();
  }

  // Stop speaker if playing
  if (spkPlaying) {
    NRF_I2S->TASKS_STOP = 1;
    delay(10);
  }

  // Play shutdown tone
  toneShutdown();
  delay(200);

  // Turn off LED
  pixel.setPixelColor(0, 0);
  pixel.show();

  // Quiet I2S pins
  i2sQuietPins();

  // Stop BLE advertising
  Bluefruit.Advertising.stop();

  Serial.println("[PWR] Goodbye!");
  delay(50);  // let serial flush

  // Configure button pin as wake source (sense LOW = pressed)
  uint32_t pinNum = g_ADigitalPinMap[PIN_BUTTON];
  nrf_gpio_cfg_sense_input(pinNum, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

  // Must use SoftDevice API when BLE stack is active (NRF_POWER->SYSTEMOFF won't work)
  sd_power_system_off();
  while (1);  // never reached
}

// ============================================================
// LED control (WS2812B via NeoPixel)
// ============================================================
#define LED_BRIGHTNESS 20  // 0-255, keep low to reduce power noise on shared rail

void setLED(bool red, bool green, bool blue) {
  pixel.setPixelColor(0, pixel.Color(
    red   ? LED_BRIGHTNESS : 0,
    green ? LED_BRIGHTNESS : 0,
    blue  ? LED_BRIGHTNESS : 0
  ));
  pixel.show();
}

// ============================================================
// Vibration motor
// ============================================================
void motorPulse(uint16_t ms) {
  digitalWrite(PIN_MOTOR, HIGH);
  delay(ms);
  digitalWrite(PIN_MOTOR, LOW);
}

// ============================================================
// SD Card functions
// ============================================================
bool sdInit() {
  Serial.println("[SD] Initializing...");

  if (!sd.begin(PIN_SD_CS, SD_SCK_MHZ(4))) {
    Serial.println("[SD] Init FAILED — no card or bad wiring");
    return false;
  }

  // Create audio directory if missing
  if (!sd.exists(SD_AUDIO_DIR)) {
    if (!sd.mkdir(SD_AUDIO_DIR)) {
      Serial.println("[SD] Failed to create /audio directory");
      return false;
    }
  }

  // Read file index (persistent counter)
  if (sd.exists(SD_INDEX_FILE)) {
    File32 f = sd.open(SD_INDEX_FILE, O_READ);
    if (f) {
      char buf[8] = {0};
      f.read(buf, sizeof(buf) - 1);
      sdFileIndex = atoi(buf);
      f.close();
      Serial.printf("[SD] Resuming at file index %u\n", sdFileIndex);
    }
  }

  // Report card info
  uint32_t cardSize = sd.card()->sectorCount();
  Serial.printf("[SD] Ready. Card size: %lu MB, next file: %u\n",
                (cardSize / 2048), sdFileIndex);
  return true;
}

void sdSaveIndex() {
  File32 f = sd.open(SD_INDEX_FILE, O_WRITE | O_CREAT | O_TRUNC);
  if (f) {
    f.print(sdFileIndex);
    f.close();
  }
}

// Save current Opus offline buffer to SD card as .opus file
// Format: "OPUS" magic(4) + frameCount(2 LE) + [frameLen(2 LE)]... + [frameData...]
bool sdSaveRecording() {
  if (!sdReady || opusOfflineFrameCount == 0) return false;

  char filename[32];
  snprintf(filename, sizeof(filename), "%s/%04u.opus", SD_AUDIO_DIR, sdFileIndex);

  Serial.printf("[SD] Saving %s (%u frames, %lu bytes)...\n",
                filename, opusOfflineFrameCount, opusOfflineBufPos);

  File32 file = sd.open(filename, O_WRITE | O_CREAT | O_TRUNC);
  if (!file) {
    Serial.println("[SD] Failed to create file");
    return false;
  }

  // Write header: magic + frame count
  file.write((const uint8_t*)"OPUS", 4);
  uint8_t fc[2] = { (uint8_t)(opusOfflineFrameCount & 0xFF),
                     (uint8_t)((opusOfflineFrameCount >> 8) & 0xFF) };
  file.write(fc, 2);

  // Write frame length table
  for (uint16_t i = 0; i < opusOfflineFrameCount; i++) {
    uint8_t fl[2] = { (uint8_t)(opusFrameLens[i] & 0xFF),
                       (uint8_t)((opusFrameLens[i] >> 8) & 0xFF) };
    file.write(fl, 2);
  }

  // Write frame data
  uint32_t written = 0;
  while (written < opusOfflineBufPos) {
    uint32_t chunk = min((uint32_t)512, opusOfflineBufPos - written);
    file.write(opusOfflineBuf + written, chunk);
    written += chunk;
  }

  file.close();

  sdFileIndex++;
  sdSaveIndex();

  Serial.printf("[SD] Saved! Total files: %u\n", sdFileIndex);
  return true;
}

// Count how many audio files exist on SD (.opus or .wav)
uint16_t sdCountFiles() {
  if (!sdReady) return 0;

  uint16_t count = 0;
  File32 dir = sd.open(SD_AUDIO_DIR);
  if (!dir) return 0;

  File32 entry;
  while (entry.openNext(&dir, O_READ)) {
    char name[32];
    entry.getName(name, sizeof(name));
    if (strstr(name, ".opus") || strstr(name, ".wav")) count++;
    entry.close();
  }
  dir.close();
  return count;
}

// Sync all SD recordings via BLE as Opus streams, then delete them
void sdSyncViaBLE() {
  if (!sdReady || !isConnected) return;

  uint16_t count = sdCountFiles();
  if (count == 0) {
    Serial.println("[SYNC] No files to sync");
    toneConfirm();
    return;
  }

  Serial.printf("[SYNC] Syncing %u files via BLE...\n", count);
  toneSyncStart();
  setLED(1, 0, 1);  // purple = syncing

  File32 dir = sd.open(SD_AUDIO_DIR);
  if (!dir) {
    toneError();
    return;
  }

  File32 entry;
  uint16_t sent = 0;
  while (entry.openNext(&dir, O_READ)) {
    char name[32];
    entry.getName(name, sizeof(name));
    bool isOpus = (strstr(name, ".opus") != NULL);
    bool isWav  = (strstr(name, ".wav") != NULL);
    if (!isOpus && !isWav) {
      entry.close();
      continue;
    }

    uint32_t fileSize = entry.fileSize();
    if (fileSize <= 6) {
      entry.close();
      continue;
    }

    Serial.printf("[SYNC] Sending %s (%lu bytes)\n", name, fileSize);

    if (isOpus) {
      // Read Opus file header
      uint8_t hdr[6];
      entry.read(hdr, 6);
      uint16_t frameCount = hdr[4] | ((uint16_t)hdr[5] << 8);

      // Read frame length table
      uint16_t frameLens[OPUS_MAX_FRAMES];
      uint16_t fc = min(frameCount, (uint16_t)OPUS_MAX_FRAMES);
      for (uint16_t i = 0; i < fc; i++) {
        uint8_t fl[2];
        entry.read(fl, 2);
        frameLens[i] = fl[0] | ((uint16_t)fl[1] << 8);
      }

      // Send START packet with Opus codec
      uint8_t startPkt[6];
      startPkt[0] = PKT_START;
      startPkt[1] = CODEC_OPUS;
      startPkt[2] = 0x00;
      startPkt[3] = 0x00;
      startPkt[4] = 0x00;
      startPkt[5] = 0x00;
      sendWithRetry(startPkt, 6);

      // Send each Opus frame as a DATA packet
      uint16_t seqNo = 0;
      uint8_t frameBuf[OPUS_MAX_PACKET];
      for (uint16_t i = 0; i < fc; i++) {
        int bytesRead = entry.read(frameBuf, frameLens[i]);
        if (bytesRead <= 0) break;

        uint8_t dataPkt[DATA_HEADER_SIZE + OPUS_MAX_PACKET];
        dataPkt[0] = PKT_DATA;
        dataPkt[1] = (seqNo >> 8) & 0xFF;
        dataPkt[2] = seqNo & 0xFF;
        memcpy(dataPkt + DATA_HEADER_SIZE, frameBuf, bytesRead);
        sendWithRetry(dataPkt, DATA_HEADER_SIZE + bytesRead);

        seqNo++;
        if (seqNo % 50 == 0) {
          Serial.printf("[SYNC] %u frames sent\n", seqNo);
        }
      }

      // Send END packet with frame count
      uint8_t endPkt[5];
      endPkt[0] = PKT_END;
      endPkt[1] = (seqNo >> 24) & 0xFF;
      endPkt[2] = (seqNo >> 16) & 0xFF;
      endPkt[3] = (seqNo >> 8) & 0xFF;
      endPkt[4] = seqNo & 0xFF;
      sendWithRetry(endPkt, 5);
    }
    // Legacy .wav files are skipped for now (old ADPCM format)

    entry.close();
    sent++;
    delay(100);
  }
  dir.close();

  // Delete synced files
  if (sent > 0) {
    Serial.printf("[SYNC] Clearing %u synced files...\n", sent);
    File32 dir2 = sd.open(SD_AUDIO_DIR);
    if (dir2) {
      File32 e;
      while (e.openNext(&dir2, O_WRITE)) {
        char ename[32];
        e.getName(ename, sizeof(ename));
        if (strstr(ename, ".opus") || strstr(ename, ".wav")) {
          e.remove();
        } else {
          e.close();
        }
      }
      dir2.close();
    }
    sdFileIndex = 0;
    sdSaveIndex();
  }

  Serial.printf("[SYNC] Done! Sent %u files\n", sent);
  toneSyncDone();
  currentState = STATE_IDLE;
  setLED(0, 1, 0);  // green
}

// ============================================================
// Opus encoder init
// ============================================================
void initOpusEncoder() {
  int requiredSize = opus_encoder_get_size(1);
  Serial.printf("[OPUS] Required encoder size: %d bytes, allocated: %d bytes\n",
                requiredSize, OPUS_ENCODER_SIZE);
  if (requiredSize > OPUS_ENCODER_SIZE) {
    Serial.println("[OPUS] CRITICAL: buffer too small! Memory corruption will occur!");
    return;
  }

  int err = opus_encoder_init(opusEnc, 16000, 1, OPUS_APPLICATION_RESTRICTED_LOWDELAY);
  if (err != OPUS_OK) {
    Serial.printf("[OPUS] encoder_init failed: %d\n", err);
    return;
  }

  opus_encoder_ctl(opusEnc, OPUS_SET_BITRATE(32000));
  opus_encoder_ctl(opusEnc, OPUS_SET_VBR(1));
  opus_encoder_ctl(opusEnc, OPUS_SET_VBR_CONSTRAINT(0));
  opus_encoder_ctl(opusEnc, OPUS_SET_COMPLEXITY(1));
  opus_encoder_ctl(opusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  opus_encoder_ctl(opusEnc, OPUS_SET_LSB_DEPTH(16));
  opus_encoder_ctl(opusEnc, OPUS_SET_DTX(0));
  opus_encoder_ctl(opusEnc, OPUS_SET_INBAND_FEC(0));
  opus_encoder_ctl(opusEnc, OPUS_SET_PACKET_LOSS_PERC(0));

  Serial.printf("[OPUS] Encoder OK (32kbps VBR, complexity=1, CELT lowdelay, stack hwm=%lu)\n",
                uxTaskGetStackHighWaterMark(NULL) * 4);
}

// ============================================================
// Send one Opus frame via BLE as a DATA packet
// ============================================================
void sendOpusFrame(const uint8_t* frameData, int frameLen) {
  if (!isConnected || !audioChar.notifyEnabled()) return;

  uint8_t pkt[DATA_HEADER_SIZE + OPUS_MAX_PACKET];
  pkt[0] = PKT_DATA;
  pkt[1] = (opusSeqNo >> 8) & 0xFF;
  pkt[2] = opusSeqNo & 0xFF;
  memcpy(&pkt[DATA_HEADER_SIZE], frameData, frameLen);

  sendWithRetry(pkt, DATA_HEADER_SIZE + frameLen);
  opusSeqNo++;
}

// ============================================================
// Encode accumulated PCM and send/buffer
// ============================================================
void encodeAndSendFrame() {
  int encodedLen = opus_encode(opusEnc, pcmAccum, OPUS_FRAME_SAMPLES, opusOutBuf, OPUS_MAX_PACKET);

  // Check stack after Opus encoding (first frame only)
  static bool stackChecked = false;
  if (!stackChecked) {
    uint32_t hwm = uxTaskGetStackHighWaterMark(NULL) * 4;
    Serial.printf("[OPUS] Stack high water mark after encode: %lu bytes remaining\n", hwm);
    stackChecked = true;
  }

  if (encodedLen < 0) {
    Serial.printf("[OPUS] encode error: %d\n", encodedLen);
    return;
  }

  if (opusOfflineMode) {
    if (opusOfflineBufPos + encodedLen <= OPUS_OFFLINE_BUF_SIZE &&
        opusOfflineFrameCount < OPUS_MAX_FRAMES) {
      memcpy(opusOfflineBuf + opusOfflineBufPos, opusOutBuf, encodedLen);
      opusFrameLens[opusOfflineFrameCount] = encodedLen;
      opusOfflineBufPos += encodedLen;
      opusOfflineFrameCount++;
    }
  } else {
    sendOpusFrame(opusOutBuf, encodedLen);
  }
}

// ============================================================
// PDM callback
// ============================================================
void onPDMdata() {
  int bytesAvailable = PDM.available();
  PDM.read(pdmBuffer, bytesAvailable);
  pdmSamplesRead = bytesAvailable / 2;
}

// ============================================================
// Process PDM samples: HPF + noise gate + Opus encode + stream
// ============================================================
void processPDMSamples() {
  if (pdmSamplesRead == 0 || currentState != STATE_RECORDING) return;

  int count = pdmSamplesRead;
  pdmSamplesRead = 0;

  if (!startupDiscardDone) {
    if (millis() - recordStartTime < STARTUP_DISCARD_MS) return;
    startupDiscardDone = true;
  }

  int16_t peakAmplitude = 0;

  int64_t energySum = 0;
  for (int i = 0; i < count; i++) {
    float x = (float)pdmBuffer[i];
    float y = HPF2_B0 * x + HPF2_B1 * hpfX1 + HPF2_B2 * hpfX2
                           - HPF2_A1 * hpfY1 - HPF2_A2 * hpfY2;
    hpfX2 = hpfX1; hpfX1 = x;
    hpfY2 = hpfY1; hpfY1 = y;

    int16_t sample = (int16_t)constrain((int32_t)y, -32768, 32767);
    if (abs(sample) < SOFT_GATE_THRESHOLD) sample = 0;
    pdmBuffer[i] = sample;
    energySum += (int32_t)sample * sample;
  }

  int64_t avgEnergySq = energySum / count;
  bool gated = (avgEnergySq < HARD_GATE_THRESHOLD_SQ);

  for (int i = 0; i < count; i++) {
    int16_t sample = gated ? 0 : pdmBuffer[i];
    int16_t absSample = abs(sample);
    if (absSample > peakAmplitude) peakAmplitude = absSample;

    pcmAccum[pcmAccumPos++] = sample;
    if (pcmAccumPos >= OPUS_FRAME_SAMPLES) {
      encodeAndSendFrame();  // hands off to Opus task (8KB stack)
      pcmAccumPos = 0;
    }
  }

  if (peakAmplitude > SILENCE_THRESHOLD) lastSoundTime = millis();

  uint32_t now = millis();
  if (now - lastSoundTime > SILENCE_TIMEOUT_MS) {
    Serial.println("[REC] Auto-stop: silence");
    stopRecording();
    return;
  }
  if (now - recordStartTime > (MAX_RECORD_SEC * 1000UL)) {
    Serial.println("[REC] Auto-stop: max duration");
    stopRecording();
  }
}

// ============================================================
// Recording control
// ============================================================
void startRecording() {
  if (currentState != STATE_IDLE) return;
  if (spkPlaying) return;

  Serial.println("[REC] Starting...");

  // FULLY kill I2S — not just TASKS_STOP, nuke the entire peripheral
  NRF_I2S->TASKS_STOP = 1;
  delayMicroseconds(200);
  NRF_I2S->ENABLE = 0;
  NRF_I2S->TXD.PTR = 0;
  NRF_I2S->RXTXD.MAXCNT = 0;
  NRF_I2S->PSEL.SCK   = 0xFFFFFFFF;
  NRF_I2S->PSEL.LRCK  = 0xFFFFFFFF;
  NRF_I2S->PSEL.SDOUT = 0xFFFFFFFF;
  NRF_I2S->PSEL.SDIN  = 0xFFFFFFFF;
  NRF_I2S->PSEL.MCK   = 0xFFFFFFFF;
  // Drive pins low to keep MAX98357A silent
  pinMode(PIN_I2S_DIN, OUTPUT);   digitalWrite(PIN_I2S_DIN, LOW);
  pinMode(PIN_I2S_LRCLK, OUTPUT); digitalWrite(PIN_I2S_LRCLK, LOW);
  pinMode(PIN_I2S_BCLK, OUTPUT);  digitalWrite(PIN_I2S_BCLK, LOW);

  // Reset Opus encoder state
  opus_encoder_ctl(opusEnc, OPUS_RESET_STATE);
  pcmAccumPos = 0;
  opusSeqNo = 0;

  // Reset filter state
  hpfX1 = hpfX2 = hpfY1 = hpfY2 = 0;
  startupDiscardDone = false;

  // Determine mode: real-time BLE or offline SD buffering
  opusOfflineMode = !(isConnected && audioChar.notifyEnabled());
  opusOfflineFrameCount = 0;
  opusOfflineBufPos = 0;

  // Send START packet (BLE mode only)
  if (!opusOfflineMode) {
    uint8_t startPkt[6];
    startPkt[0] = PKT_START;
    startPkt[1] = CODEC_OPUS;
    startPkt[2] = 0x00;
    startPkt[3] = 0x00;
    startPkt[4] = 0x00;
    startPkt[5] = 0x00;
    sendWithRetry(startPkt, 6);
    Serial.println("[BLE] START packet sent (codec=Opus)");
  } else {
    Serial.println("[REC] Offline mode — buffering for SD");
  }

  lastSoundTime = millis();
  recordStartTime = millis();

  // Start PDM mic (kept off when idle to avoid clock noise on I2S)
  if (!PDM.begin(1, SAMPLE_RATE)) {
    Serial.println("[REC] PDM start failed!");
    return;
  }

  // PDM gain: +12dB
  NRF_PDM->GAINL = 0x40;
  NRF_PDM->GAINR = 0x40;

  Serial.println("[REC] PDM started (gain=+12dB, Opus encoding)");

  setLED(0, 0, 1);  // blue = recording
  currentState = STATE_RECORDING;
}

void stopRecording() {
  if (currentState != STATE_RECORDING) return;

  // Stop PDM mic immediately to eliminate clock noise
  PDM.end();
  Serial.println("[REC] PDM stopped");

  // Flush remaining PCM accumulator (pad with zeros)
  if (pcmAccumPos > 0) {
    for (int i = pcmAccumPos; i < OPUS_FRAME_SAMPLES; i++) {
      pcmAccum[i] = 0;
    }
    encodeAndSendFrame();
    pcmAccumPos = 0;
  }

  uint32_t duration = millis() - recordStartTime;
  Serial.printf("[REC] Stopped. %lu ms, Opus frames: %u\n", duration, opusSeqNo);

  if (!opusOfflineMode) {
    // Send END packet via BLE
    uint8_t endPkt[5];
    endPkt[0] = PKT_END;
    endPkt[1] = (opusSeqNo >> 24) & 0xFF;
    endPkt[2] = (opusSeqNo >> 16) & 0xFF;
    endPkt[3] = (opusSeqNo >> 8) & 0xFF;
    endPkt[4] = opusSeqNo & 0xFF;
    sendWithRetry(endPkt, 5);
    Serial.printf("[BLE] END packet sent (frames=%u)\n", opusSeqNo);
  } else if (sdReady) {
    // Save offline buffer to SD card
    if (!sdSaveRecording()) {
      Serial.println("[REC] SD save failed");
    }
  } else {
    Serial.println("[REC] No BLE and no SD — audio lost!");
  }

  // No motor/tone during stop — avoid blocking BLE
  setLED(0, 0, 0);

  currentState = STATE_IDLE;
  setLED(isConnected ? 0 : 1, isConnected ? 1 : 0, 0);
}

// (WAV header and batch BLE send removed — Opus streams in real-time)

void sendWithRetry(const uint8_t* data, uint16_t len) {
  // Try a few times then drop — never busy-wait, it starves the SoftDevice
  for (int attempt = 0; attempt < 3; attempt++) {
    if (audioChar.notify(data, len)) return;
    delay(2);  // yield to RTOS + SoftDevice
  }
  // Packet dropped — better than killing BLE connection
}

// ============================================================
// Speaker ADPCM decoder (phone → hardware)
// ============================================================
int16_t spkAdpcmDecodeSample(uint8_t nibble) {
  int16_t step = stepTable[spkAdpcmIndex];
  int32_t delta = step >> 3;
  if (nibble & 4) delta += step;
  if (nibble & 2) delta += step >> 1;
  if (nibble & 1) delta += step >> 2;
  if (nibble & 8) delta = -delta;

  spkAdpcmPrev += delta;
  if (spkAdpcmPrev > 32767)  spkAdpcmPrev = 32767;
  if (spkAdpcmPrev < -32768) spkAdpcmPrev = -32768;

  spkAdpcmIndex += indexTable[nibble & 0x0F];
  if (spkAdpcmIndex < 0)  spkAdpcmIndex = 0;
  if (spkAdpcmIndex > 88) spkAdpcmIndex = 88;

  return spkAdpcmPrev;
}

// Available bytes in ring buffer
uint32_t spkRingAvailable() {
  int32_t avail = (int32_t)spkRingHead - (int32_t)spkRingTail;
  if (avail < 0) avail += SPK_RING_SIZE;
  return (uint32_t)avail;
}

// Free space in ring buffer
uint32_t spkRingFree() {
  return SPK_RING_SIZE - 1 - spkRingAvailable();
}

// Write data into ring buffer
void spkRingWrite(const uint8_t* data, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    spkRingBuf[spkRingHead] = data[i];
    spkRingHead = (spkRingHead + 1) % SPK_RING_SIZE;
  }
}

// Read one byte from ring buffer
uint8_t spkRingRead() {
  uint8_t val = spkRingBuf[spkRingTail];
  spkRingTail = (spkRingTail + 1) % SPK_RING_SIZE;
  return val;
}

// Fill a DMA buffer from ring buffer (ADPCM decode → stereo PCM)
void spkFillDmaBuffer(int32_t* buf, uint32_t samples) {
  for (uint32_t i = 0; i < samples; i++) {
    int16_t sample = 0;
    if (spkRingAvailable() > 0) {
      uint8_t byte = spkRingRead();
      // Low nibble first, then high nibble
      int16_t sampleLo = spkAdpcmDecodeSample(byte & 0x0F);
      int16_t sampleHi = spkAdpcmDecodeSample((byte >> 4) & 0x0F);
      // Average the two samples (or use first; we output 2 samples per byte)
      sample = sampleLo;
      // Pack stereo: same sample both channels
      buf[i] = ((int32_t)sample << 16) | ((uint16_t)sample);
      // Use second sample for next iteration if we have room
      if (i + 1 < samples) {
        i++;
        buf[i] = ((int32_t)sampleHi << 16) | ((uint16_t)sampleHi);
      }
    } else {
      // Underrun — fill silence
      buf[i] = 0;
    }
  }
}

// Start I2S for speaker playback
void spkStartI2S() {
  // Ensure PDM mic is off to prevent clock noise coupling
  PDM.end();

  // Reconnect I2S pins
  i2sReconnectPins();

  // Reset decoder
  spkAdpcmPrev = 0;
  spkAdpcmIndex = 0;

  // Pre-fill both DMA buffers
  spkFillDmaBuffer(spkDmaBufA, SPK_DMA_SAMPLES);
  spkFillDmaBuffer(spkDmaBufB, SPK_DMA_SAMPLES);
  spkUsingBufA = true;

  // I2S is already configured from i2sInit(), just set buffer and start
  NRF_I2S->TXD.PTR = (uint32_t)spkDmaBufA;
  NRF_I2S->RXTXD.MAXCNT = SPK_DMA_SAMPLES;
  NRF_I2S->EVENTS_TXPTRUPD = 0;
  NRF_I2S->EVENTS_STOPPED = 0;
  NRF_I2S->ENABLE = 1;
  NRF_I2S->TASKS_START = 1;

  spkPlaying = true;
  Serial.println("[SPK] I2S playback started");
}

// Stop I2S speaker playback
void spkStopI2S() {
  if (!spkPlaying) return;

  NRF_I2S->TASKS_STOP = 1;
  while (!NRF_I2S->EVENTS_STOPPED) {
    delayMicroseconds(10);
  }
  NRF_I2S->EVENTS_STOPPED = 0;
  i2sQuietPins();  // Drive I2S pins LOW to keep MAX98357A silent

  spkPlaying = false;
  spkRingHead = 0;
  spkRingTail = 0;
  Serial.println("[SPK] I2S playback stopped");
}

// Called from loop() to feed I2S DMA
void spkProcessI2S() {
  if (!spkPlaying) return;

  if (NRF_I2S->EVENTS_TXPTRUPD) {
    NRF_I2S->EVENTS_TXPTRUPD = 0;

    // Fill the buffer that just finished playing
    if (spkUsingBufA) {
      spkFillDmaBuffer(spkDmaBufB, SPK_DMA_SAMPLES);
      NRF_I2S->TXD.PTR = (uint32_t)spkDmaBufB;
    } else {
      spkFillDmaBuffer(spkDmaBufA, SPK_DMA_SAMPLES);
      NRF_I2S->TXD.PTR = (uint32_t)spkDmaBufA;
    }
    spkUsingBufA = !spkUsingBufA;

    // Check if stream ended and buffer is empty
    if (spkStreamEnded && spkRingAvailable() == 0) {
      spkStopI2S();
      currentState = STATE_IDLE;
      setLED(isConnected ? 0 : 1, isConnected ? 1 : 0, 0);
      toneConfirm();
      Serial.println("[SPK] Playback complete");
    }
  }
}

// BLE write callback for speaker characteristic
void onSpeakerWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len < 1) return;
  uint8_t type = data[0];

  switch (type) {
    case SPK_START: {
      Serial.println("[SPK] START received");
      // Reset state
      spkRingHead = 0;
      spkRingTail = 0;
      spkStreamEnded = false;
      spkAdpcmPrev = 0;
      spkAdpcmIndex = 0;
      currentState = STATE_PLAYING_SPK;
      setLED(1, 1, 0);  // yellow = playing
      motorPulse(30);
      // Don't start I2S yet — wait for enough data to buffer
      break;
    }
    case SPK_DATA: {
      // [0x31][seqNo:2BE][ADPCM data...]
      if (len <= 3) break;
      uint16_t payloadLen = len - 3;
      uint8_t* payload = data + 3;

      // Write to ring buffer (drop if full)
      if (spkRingFree() >= payloadLen) {
        spkRingWrite(payload, payloadLen);
      } else {
        Serial.println("[SPK] Ring buffer overflow — dropping packet");
      }

      // Start I2S once we have enough buffered data (~2KB = ~250ms)
      if (!spkPlaying && spkRingAvailable() >= 2048) {
        spkStartI2S();
      }
      break;
    }
    case SPK_END: {
      Serial.println("[SPK] END received");
      spkStreamEnded = true;
      // If I2S hasn't started yet (very short audio), start now
      if (!spkPlaying && spkRingAvailable() > 0) {
        spkStartI2S();
      }
      break;
    }
    case SPK_STOP: {
      Serial.println("[SPK] STOP — immediate halt");
      spkStopI2S();
      spkStreamEnded = true;
      currentState = STATE_IDLE;
      setLED(isConnected ? 0 : 1, isConnected ? 1 : 0, 0);
      break;
    }
  }
}

// ============================================================
// BLE callbacks
// ============================================================
void onConnect(uint16_t conn_handle) {
  isConnected = true;
  autoShutdownPending = false;  // cancel auto-shutdown on reconnect
  currentState = STATE_IDLE;
  setLED(0, 1, 0);

  BLEConnection* conn = Bluefruit.Connection(conn_handle);
  char name[32] = {0};
  conn->getPeerName(name, sizeof(name));
  Serial.printf("[BLE] Connected: %s\n", name);

  // Vibration-only feedback — I2S tones conflict with BLE SoftDevice and
  // can crash the MCU if a button press triggers PDM while I2S is active.
  motorPulse(80);
  delay(60);
  motorPulse(80);

  // Report SD card status
  uint16_t sdFiles = sdCountFiles();
  if (sdFiles > 0) {
    Serial.printf("[BLE] %u offline recordings on SD card\n", sdFiles);
  }
}

void onDisconnect(uint16_t conn_handle, uint8_t reason) {
  isConnected = false;

  if (currentState == STATE_RECORDING) {
    stopRecording();
  }

  // Stop any active I2S/speaker playback to avoid orphaned DMA
  if (spkPlaying) {
    spkStopI2S();
  }

  // Start auto-shutdown timer — device will shut down if no reconnect
  disconnectTime = millis();
  autoShutdownPending = true;

  currentState = STATE_INIT;
  setLED(1, 0, 0);
  Serial.printf("[BLE] Disconnected, reason=0x%02X — auto-shutdown in %ds\n",
                reason, AUTO_SHUTDOWN_MS / 1000);

  // Vibration-only feedback — I2S tones in BLE callbacks cause crashes
  motorPulse(50);
  delay(80);
  motorPulse(50);
}

void onTextWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len < 1) return;
  // CRITICAL: BLE write callbacks run in SoftDevice high-priority context.
  // Must return FAST — any blocking (motor, PDM, I2S) here kills the connection.
  // Defer all processing to loop() via pendingBLECommand.
  pendingBLECommand = data[0];
  Serial.printf("[CMD] BLE command queued: 0x%02X\n", pendingBLECommand);
}

void handleBLECommand(uint8_t command) {
  Serial.printf("[CMD] Processing: 0x%02X\n", command);

  switch (command) {
    case 0x01:
      startRecording();
      break;
    case 0x00:
      if (currentState == STATE_RECORDING) {
        stopRecording();
      }
      break;
    case 0x10:  // Sync SD recordings
      if (currentState == STATE_IDLE) {
        currentState = STATE_SD_SYNC;
        sdSyncViaBLE();
      }
      break;
    case CMD_SHUTDOWN:
      Serial.println("[CMD] Shutdown requested by app");
      enterDeepSleep();
      break;
    default:
      Serial.printf("[CMD] Unknown: 0x%02X\n", command);
      break;
  }
}

// ============================================================
// Heartbeat
// ============================================================
void sendHeartbeat() {
  if (!isConnected || !heartbeatChar.notifyEnabled()) return;

  uint16_t battMv = readBatteryMillivolts();

  uint8_t pkt[6];
  pkt[0] = PKT_HEARTBEAT;
  pkt[1] = (heartbeatCounter >> 8) & 0xFF;
  pkt[2] = heartbeatCounter & 0xFF;
  pkt[3] = (currentState == STATE_RECORDING) ? 0x01 : 0x00;
  pkt[4] = (battMv >> 8) & 0xFF;
  pkt[5] = battMv & 0xFF;

  if (heartbeatChar.notify(pkt, 6)) {
    Serial.printf("[HB] #%u (rec=%d, sd=%d, files=%u, batt=%umV)\n",
                  heartbeatCounter,
                  currentState == STATE_RECORDING,
                  sdReady,
                  sdReady ? sdCountFiles() : 0,
                  battMv);
  }
  heartbeatCounter++;
}

// ============================================================
// Single-button handling (called from loop)
//
// Gestures:
//   Long press (>500ms hold) → record audio (release to stop)
//   Single tap (<500ms)      → PLAY command (Interactive AI)
//   Double tap               → toggle BLE advertising
// ============================================================
void onSingleTap() {
  if (isConnected && textChar.notifyEnabled()) {
    uint8_t cmd = CMD_PLAY;
    textChar.notify(&cmd, 1);
    Serial.println("[BTN] Single tap -> PLAY");
    delay(5);  // let BLE radio finish before drawing motor current
    motorPulse(30);
  } else {
    Serial.printf("[BTN] Single tap (no BLE) -> status: SD=%d, files=%u\n",
                  sdReady, sdReady ? sdCountFiles() : 0);
    motorPulse(30);
  }
}

void onDoubleTap() {
  if (Bluefruit.Advertising.isRunning()) {
    Bluefruit.Advertising.stop();
    Serial.println("[BTN] Double tap -> BLE advertising OFF");
    setLED(1, 1, 0);  // yellow = advertising off
  } else {
    Bluefruit.Advertising.start(0);
    Serial.println("[BTN] Double tap -> BLE advertising ON");
    setLED(0, 0, 1);  // blue = advertising
  }
  motorPulse(50);
}

void handleButton() {
  uint32_t now = millis();

  // --- Debounce ---
  bool reading = digitalRead(PIN_BUTTON);
  if (reading != lastBtnState) {
    lastBtnDebounce = now;
  }
  lastBtnState = reading;
  if ((now - lastBtnDebounce) < DEBOUNCE_MS) return;

  bool newPressed = (reading == LOW);

  if (newPressed != btnPressed) {
    btnPressed = newPressed;

    if (btnPressed) {
      // --- Button pressed ---
      btnPressTime = now;
      if (tapCount == 1 && (now - btnReleaseTime) < DOUBLE_TAP_MS) {
        tapCount = 2;  // second press within double-tap window
      } else {
        tapCount = 1;
      }

    } else {
      // --- Button released ---
      btnReleaseTime = now;

      if (btnRecording) {
        // Was recording via long press — stop
        btnRecording = false;
        if (currentState == STATE_RECORDING) {
          stopRecording();
        }
        tapCount = 0;
        return;
      }

      if (tapCount == 2) {
        // Double tap completed on release
        onDoubleTap();
        tapCount = 0;
        return;
      }
      // tapCount == 1, short release — wait for possible double tap
    }
  }

  // --- Long press detection (while still holding) ---
  if (btnPressed && !btnRecording && tapCount == 1) {
    if ((now - btnPressTime) >= LONG_PRESS_MS) {
      btnRecording = true;
      tapCount = 0;
      if (currentState == STATE_IDLE) {
        startRecording();
        motorPulse(30);  // haptic feedback: recording started
      }
    }
  }

  // --- Single tap timeout (released, waiting for possible double tap) ---
  if (!btnPressed && tapCount == 1 && !btnRecording) {
    if ((now - btnReleaseTime) >= DOUBLE_TAP_MS) {
      onSingleTap();
      tapCount = 0;
    }
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);

  // Any button press from System OFF wakes the device — no confirmation needed.
  // Clear reset reason flags.
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  NRF_POWER->RESETREAS = NRF_POWER->RESETREAS;

  // WS2812B LED
  pixel.begin();
  pixel.setBrightness(LED_BRIGHTNESS);
  setLED(1, 0, 0);  // red = init

  // Vibration motor
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);

  Serial.println("=== Pinclaw Full Firmware (PCB V1.0) — Opus ===");
  Serial.println("Board: XIAO nRF52840 Sense");
  Serial.println("Audio: Opus CELT 32kbps + MAX98357A I2S");
  Serial.println("LED: WS2812B");
  Serial.println("Features: BLE + Buttons + I2S Speaker + SD Card + Motor");
  Serial.println();

  // Initialize Opus encoder
  initOpusEncoder();

  // I2S audio init
  i2sInit();

  // Startup tone + vibration
  toneStartup();
  motorPulse(100);
  delay(200);

  // Quiet I2S pins after startup tones — keeps MAX98357A silent until next playback
  i2sQuietPins();

  // --- SD Card ---
  sdReady = sdInit();
  if (sdReady) {
    Serial.println("[OK] SD card ready");
    toneConfirm();
  } else {
    Serial.println("[WARN] SD card not available — BLE-only mode");
  }

  // --- BLE ---
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  bond_clear_prph();  // clear corrupted bond data from flash
  Bluefruit.setTxPower(4);
  Bluefruit.setName("Pinclaw-Clip");

  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  pinclawService.begin();

  textChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY | CHR_PROPS_WRITE);
  textChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  textChar.setMaxLen(20);
  textChar.setWriteCallback(onTextWrite);
  textChar.begin();

  audioChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  audioChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  audioChar.setMaxLen(BLE_MTU - 3);
  audioChar.begin();

  heartbeatChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  heartbeatChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  heartbeatChar.setMaxLen(6);
  heartbeatChar.begin();

  // Speaker characteristic — phone writes audio data to hardware
  speakerChar.setProperties(CHR_PROPS_WRITE_WO_RESP);
  speakerChar.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  speakerChar.setMaxLen(BLE_MTU - 3);
  speakerChar.setWriteCallback(onSpeakerWrite);
  speakerChar.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(pinclawService);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);

  Serial.println("[OK] BLE advertising as 'Pinclaw-Clip'");

  // --- PDM Microphone (lazy init — started only when recording) ---
  PDM.onReceive(onPDMdata);
  PDM.setBufferSize(PDM_BUFFER_SIZE * 2);
  // PDM gain is set via NRF_PDM registers in startRecording() (+12dB)
  // PDM is NOT started here to avoid clock noise coupling into I2S.
  Serial.println("[OK] PDM mic configured (gain=+12dB on record)");

  // --- Ready ---
  Serial.println();
  Serial.println("=== READY ===");
  Serial.println("Button (D5): Hold=record, Tap=play, DoubleTap=BLE toggle");
  Serial.printf("SD: %s (%u files)\n", sdReady ? "OK" : "N/A",
                sdReady ? sdCountFiles() : 0);
  Serial.println("BLE: Waiting for connection...");
  Serial.println();

  currentState = STATE_INIT;
  lastHeartbeatTime = millis();

  delay(100);
  toneConnected();
}

// ============================================================
// Main loop
// ============================================================
void loop() {
  // --- Process deferred BLE commands (set by onTextWrite callback) ---
  if (pendingBLECommand != 0) {
    uint8_t cmd = pendingBLECommand;
    pendingBLECommand = 0;
    handleBLECommand(cmd);
  }

  if (autoShutdownPending && (millis() - disconnectTime >= AUTO_SHUTDOWN_MS)) {
    Serial.println("[PWR] Auto-shutdown — no BLE reconnection");
    enterDeepSleep();
  }

  // Process mic data while recording
  if (currentState == STATE_RECORDING) {
    processPDMSamples();
  }

  // Process speaker I2S DMA feed
  if (currentState == STATE_PLAYING_SPK) {
    spkProcessI2S();
  }

  // Handle button (not during sync or speaker playback)
  if (currentState != STATE_SD_SYNC && currentState != STATE_PLAYING_SPK) {
    handleButton();
  }

  // Heartbeat
  if (isConnected && (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)) {
    sendHeartbeat();
    lastHeartbeatTime = millis();
  }

  // Update state on first connection
  if (isConnected && currentState == STATE_INIT) {
    currentState = STATE_IDLE;
  }

  // Standalone idle: can still record with button long press
  if (!isConnected && currentState == STATE_INIT) {
    currentState = STATE_IDLE;
    setLED(1, 0, 0);  // red = standalone mode
  }

  delay(1);
}
