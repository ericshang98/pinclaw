// Pinclaw Full Firmware — XIAO nRF52840 Sense (PCB V1.0)
// BLE Audio + Buttons + I2S Speaker + SD Card + Motor + WS2812B LED
//
// Pin Map (PCB V1.0 — pinc sch2):
//   D0  = SD Card CS
//   D1  = I2S DIN  (MAX98357A audio data)
//   D2  = I2S LRCLK (MAX98357A word select)
//   D3  = I2S BCLK  (MAX98357A bit clock)
//   D4  = PTT Button (active LOW, internal pullup)
//   D5  = Action Button (active LOW, internal pullup)
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

// ============================================================
// Pin Definitions (PCB V1.0)
// ============================================================
#define PIN_SD_CS       0   // D0 — SD card chip select
#define PIN_I2S_DIN     1   // D1 — I2S data to MAX98357A
#define PIN_I2S_LRCLK   2   // D2 — I2S word select
#define PIN_I2S_BCLK    3   // D3 — I2S bit clock
#define PIN_PTT         5   // D5 — Push-to-talk button (PCB V1.0: pins swapped vs schematic)
#define PIN_ACTION      4   // D4 — Action button (PCB V1.0: pins swapped vs schematic)
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
#define SILENCE_THRESHOLD  150
#define SILENCE_TIMEOUT_MS 3000
#define ADPCM_BUFFER_SIZE  80000

// BLE MTU
#define BLE_MTU            247
#define DATA_HEADER_SIZE   3
#define DATA_PAYLOAD_SIZE  (BLE_MTU - DATA_HEADER_SIZE)

// ============================================================
// Button config
// ============================================================
#define DEBOUNCE_MS        50
#define LONG_PRESS_MS      3000  // 3s for long press

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
  STATE_SENDING,
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
// IMA ADPCM encoder
// ============================================================
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

int16_t adpcmPrevSample = 0;
int8_t  adpcmIndex = 0;

// Audio recording buffer (block-based IMA ADPCM)
uint8_t adpcmBuffer[ADPCM_BUFFER_SIZE];
volatile uint32_t adpcmWritePos = 0;

// Block tracking: blockAlign=256, preamble=4 bytes, nibble data=252 bytes
#define BLOCK_ALIGN       256
#define BLOCK_PREAMBLE    4
#define BLOCK_DATA_BYTES  (BLOCK_ALIGN - BLOCK_PREAMBLE)  // 252
#define SAMPLES_PER_BLOCK 505  // 1 (preamble) + 252*2 (nibbles)

volatile uint16_t blockNibbleCount = 0;  // nibbles written in current block (0..504)
volatile bool     nibbleHigh = false;    // current nibble position within byte

// Silence detection
volatile uint32_t lastSoundTime = 0;
volatile uint32_t recordStartTime = 0;

// ============================================================
// Button state
// ============================================================
volatile bool pttPressed = false;
volatile bool actionPressed = false;
uint32_t pttPressTime = 0;
uint32_t actionPressTime = 0;
bool lastPttState = HIGH;
bool lastActionState = HIGH;
uint32_t lastPttDebounce = 0;
uint32_t lastActionDebounce = 0;

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
#define HEARTBEAT_INTERVAL_MS 50000
volatile bool isConnected = false;

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
  uint32_t endTime = millis() + durationMs;
  while (millis() < endTime) {
    if (NRF_I2S->EVENTS_TXPTRUPD) {
      NRF_I2S->EVENTS_TXPTRUPD = 0;
      // Re-point to same buffer for continuous looping
      NRF_I2S->TXD.PTR = (uint32_t)i2sTxBuf;
    }
    delayMicroseconds(100);
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

// Save current ADPCM buffer to SD card as WAV file
bool sdSaveRecording() {
  if (!sdReady || adpcmWritePos == 0) return false;

  char filename[32];
  snprintf(filename, sizeof(filename), "%s/%04u.wav", SD_AUDIO_DIR, sdFileIndex);

  Serial.printf("[SD] Saving %s (%lu bytes)...\n", filename, adpcmWritePos);

  File32 file = sd.open(filename, O_WRITE | O_CREAT | O_TRUNC);
  if (!file) {
    Serial.println("[SD] Failed to create file");
    return false;
  }

  // Write WAV header
  uint8_t wavHeader[60];
  buildWAVHeader(wavHeader, adpcmWritePos);
  file.write(wavHeader, 60);

  // Write ADPCM data in chunks
  uint32_t written = 0;
  while (written < adpcmWritePos) {
    uint32_t chunk = min((uint32_t)512, adpcmWritePos - written);
    file.write(adpcmBuffer + written, chunk);
    written += chunk;
  }

  file.close();

  sdFileIndex++;
  sdSaveIndex();

  Serial.printf("[SD] Saved! Total files: %u\n", sdFileIndex);
  return true;
}

// Count how many WAV files exist on SD
uint16_t sdCountFiles() {
  if (!sdReady) return 0;

  uint16_t count = 0;
  File32 dir = sd.open(SD_AUDIO_DIR);
  if (!dir) return 0;

  File32 entry;
  while (entry.openNext(&dir, O_READ)) {
    char name[32];
    entry.getName(name, sizeof(name));
    if (strstr(name, ".wav")) count++;
    entry.close();
  }
  dir.close();
  return count;
}

// Sync all SD recordings via BLE, then delete them
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
    if (!strstr(name, ".wav")) {
      entry.close();
      continue;
    }

    uint32_t fileSize = entry.fileSize();
    if (fileSize <= 60) {  // skip empty/header-only files
      entry.close();
      continue;
    }

    Serial.printf("[SYNC] Sending %s (%lu bytes)\n", name, fileSize);

    // Send START packet
    uint8_t startPkt[6];
    startPkt[0] = PKT_START;
    startPkt[1] = 0x03;  // IMA ADPCM WAV codec
    startPkt[2] = (fileSize >> 24) & 0xFF;
    startPkt[3] = (fileSize >> 16) & 0xFF;
    startPkt[4] = (fileSize >> 8) & 0xFF;
    startPkt[5] = fileSize & 0xFF;
    sendWithRetry(startPkt, 6);

    // Send DATA packets (read file in chunks)
    uint16_t seqNo = 0;
    uint32_t crc = 0xFFFFFFFF;
    uint8_t readBuf[DATA_PAYLOAD_SIZE];

    while (entry.available()) {
      int bytesRead = entry.read(readBuf, DATA_PAYLOAD_SIZE);
      if (bytesRead <= 0) break;

      // Update CRC
      for (int i = 0; i < bytesRead; i++) {
        crc ^= readBuf[i];
        for (int j = 0; j < 8; j++) {
          if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
          else crc >>= 1;
        }
      }

      // Build DATA packet
      uint8_t dataPkt[BLE_MTU];
      dataPkt[0] = PKT_DATA;
      dataPkt[1] = (seqNo >> 8) & 0xFF;
      dataPkt[2] = seqNo & 0xFF;
      memcpy(dataPkt + DATA_HEADER_SIZE, readBuf, bytesRead);
      sendWithRetry(dataPkt, DATA_HEADER_SIZE + bytesRead);

      seqNo++;
      if (seqNo % 50 == 0) {
        Serial.printf("[SYNC] %u packets sent\n", seqNo);
      }
    }

    crc ^= 0xFFFFFFFF;

    // Send END packet
    uint8_t endPkt[5];
    endPkt[0] = PKT_END;
    endPkt[1] = (crc >> 24) & 0xFF;
    endPkt[2] = (crc >> 16) & 0xFF;
    endPkt[3] = (crc >> 8) & 0xFF;
    endPkt[4] = crc & 0xFF;
    sendWithRetry(endPkt, 5);

    entry.close();
    sent++;

    // Small delay between files
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
        char name[32];
        e.getName(name, sizeof(name));
        if (strstr(name, ".wav")) {
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
// IMA ADPCM encode one sample
// ============================================================
uint8_t adpcmEncodeSample(int16_t sample) {
  int32_t diff = sample - adpcmPrevSample;
  uint8_t nibble = 0;

  if (diff < 0) {
    nibble = 8;
    diff = -diff;
  }

  int16_t step = stepTable[adpcmIndex];
  int32_t tempStep = step;

  if (diff >= tempStep) { nibble |= 4; diff -= tempStep; }
  tempStep >>= 1;
  if (diff >= tempStep) { nibble |= 2; diff -= tempStep; }
  tempStep >>= 1;
  if (diff >= tempStep) { nibble |= 1; }

  int32_t delta = step >> 3;
  if (nibble & 4) delta += step;
  if (nibble & 2) delta += step >> 1;
  if (nibble & 1) delta += step >> 2;
  if (nibble & 8) delta = -delta;

  adpcmPrevSample += delta;
  if (adpcmPrevSample > 32767)  adpcmPrevSample = 32767;
  if (adpcmPrevSample < -32768) adpcmPrevSample = -32768;

  adpcmIndex += indexTable[nibble];
  if (adpcmIndex < 0)  adpcmIndex = 0;
  if (adpcmIndex > 88) adpcmIndex = 88;

  return nibble & 0x0F;
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
// Process PDM samples
// ============================================================
// Write block preamble at current adpcmWritePos
void writeBlockPreamble() {
  if (adpcmWritePos + BLOCK_PREAMBLE > ADPCM_BUFFER_SIZE) return;
  adpcmBuffer[adpcmWritePos]     = adpcmPrevSample & 0xFF;
  adpcmBuffer[adpcmWritePos + 1] = (adpcmPrevSample >> 8) & 0xFF;
  adpcmBuffer[adpcmWritePos + 2] = (uint8_t)adpcmIndex;
  adpcmBuffer[adpcmWritePos + 3] = 0;  // reserved
  adpcmWritePos += BLOCK_PREAMBLE;
  blockNibbleCount = 0;
  nibbleHigh = false;
}

void processPDMSamples() {
  if (pdmSamplesRead == 0 || currentState != STATE_RECORDING) return;

  int count = pdmSamplesRead;
  pdmSamplesRead = 0;

  int16_t peakAmplitude = 0;

  for (int i = 0; i < count; i++) {
    int16_t sample = pdmBuffer[i];
    int16_t absSample = abs(sample);
    if (absSample > peakAmplitude) peakAmplitude = absSample;

    if (adpcmWritePos >= ADPCM_BUFFER_SIZE) continue;

    // Start new block if needed
    if (blockNibbleCount == 0 && !nibbleHigh) {
      writeBlockPreamble();
    }

    // Encode sample to 4-bit nibble
    uint8_t nibble = adpcmEncodeSample(sample);

    if (adpcmWritePos < ADPCM_BUFFER_SIZE) {
      if (!nibbleHigh) {
        adpcmBuffer[adpcmWritePos] = nibble;  // low nibble
        nibbleHigh = true;
      } else {
        adpcmBuffer[adpcmWritePos] |= (nibble << 4);  // high nibble
        nibbleHigh = false;
        adpcmWritePos++;
      }
      blockNibbleCount++;

      // Block full? (504 nibbles = 252 bytes of nibble data)
      if (blockNibbleCount >= (BLOCK_DATA_BYTES * 2)) {
        blockNibbleCount = 0;
        nibbleHigh = false;
      }
    }
  }

  if (peakAmplitude > SILENCE_THRESHOLD) {
    lastSoundTime = millis();
  }

  uint32_t now = millis();

  if (now - lastSoundTime > SILENCE_TIMEOUT_MS) {
    Serial.println("[REC] Auto-stop: silence");
    stopRecording();
    return;
  }

  if (now - recordStartTime > (MAX_RECORD_SEC * 1000UL)) {
    Serial.println("[REC] Auto-stop: max duration");
    stopRecording();
    return;
  }
}

// ============================================================
// Recording control
// ============================================================
void startRecording() {
  if (currentState != STATE_IDLE) return;

  Serial.println("[REC] Starting...");
  toneRecStart();
  motorPulse(50);

  adpcmPrevSample = 0;
  adpcmIndex = 0;
  adpcmWritePos = 0;
  nibbleHigh = false;
  blockNibbleCount = 0;

  lastSoundTime = millis();
  recordStartTime = millis();

  // Start PDM mic (kept off when idle to avoid clock noise on I2S)
  if (!PDM.begin(1, SAMPLE_RATE)) {
    Serial.println("[REC] PDM start failed!");
    toneError();
    return;
  }

  // Override PDM hardware gain register directly (same approach as Omi firmware).
  // Arduino PDM.setGain() uses software scaling which doesn't improve SNR.
  // nRF52840 PDM GAINL/GAINR register: 0x28=0dB, 0x3C=+20dB, 0x50=+40dB
  // Omi uses 0x3C (+20dB) as default. We use the same.
  NRF_PDM->GAINL = 0x3C;  // +20dB left channel
  NRF_PDM->GAINR = 0x3C;  // +20dB right channel
  Serial.println("[REC] PDM started (hardware gain=0x3C, +20dB)");

  setLED(0, 0, 1);  // blue = recording
  currentState = STATE_RECORDING;
}

void stopRecording() {
  if (currentState != STATE_RECORDING) return;

  // Stop PDM mic immediately to eliminate clock noise
  PDM.end();
  Serial.println("[REC] PDM stopped");

  // Flush current nibble byte if odd
  if (nibbleHigh) {
    adpcmWritePos++;
    nibbleHigh = false;
  }

  // Pad current block to full blockAlign boundary
  uint32_t blockRemainder = adpcmWritePos % BLOCK_ALIGN;
  if (blockRemainder > 0) {
    uint32_t padBytes = BLOCK_ALIGN - blockRemainder;
    if (adpcmWritePos + padBytes <= ADPCM_BUFFER_SIZE) {
      memset(adpcmBuffer + adpcmWritePos, 0, padBytes);
      adpcmWritePos += padBytes;
    }
  }

  uint32_t duration = millis() - recordStartTime;
  Serial.printf("[REC] Stopped. %lu ms, %lu bytes\n", duration, adpcmWritePos);

  toneRecStop();
  motorPulse(50);
  setLED(0, 0, 0);

  currentState = STATE_SENDING;

  if (isConnected && audioChar.notifyEnabled()) {
    sendAudioViaBLE();
  } else if (sdReady) {
    if (sdSaveRecording()) {
      toneConfirm();
    } else {
      toneError();
    }
    currentState = STATE_IDLE;
    setLED(isConnected ? 0 : 1, isConnected ? 1 : 0, 0);
  } else {
    Serial.println("[REC] No BLE and no SD — audio lost!");
    toneError();
    currentState = STATE_IDLE;
    setLED(1, 0, 0);
  }
}

// ============================================================
// WAV header builder
// ============================================================
#define WAV_HEADER_SIZE 60

void buildWAVHeader(uint8_t* header, uint32_t adpcmDataSize) {
  uint16_t blockAlign = 256;
  uint16_t samplesPerBlock = 505;
  uint16_t bitsPerSample = 4;
  uint32_t byteRate = SAMPLE_RATE * blockAlign / samplesPerBlock;
  uint16_t extraSize = 2;

  uint32_t totalSamples = (adpcmDataSize / (blockAlign > 0 ? blockAlign : 1)) * samplesPerBlock;
  uint32_t fileSize = WAV_HEADER_SIZE + adpcmDataSize - 8;

  int p = 0;

  header[p++] = 'R'; header[p++] = 'I'; header[p++] = 'F'; header[p++] = 'F';
  writeLE32(header, p, fileSize); p += 4;
  header[p++] = 'W'; header[p++] = 'A'; header[p++] = 'V'; header[p++] = 'E';

  header[p++] = 'f'; header[p++] = 'm'; header[p++] = 't'; header[p++] = ' ';
  writeLE32(header, p, 20); p += 4;
  writeLE16(header, p, 0x0011); p += 2;
  writeLE16(header, p, 1); p += 2;
  writeLE32(header, p, SAMPLE_RATE); p += 4;
  writeLE32(header, p, byteRate); p += 4;
  writeLE16(header, p, blockAlign); p += 2;
  writeLE16(header, p, bitsPerSample); p += 2;
  writeLE16(header, p, extraSize); p += 2;
  writeLE16(header, p, samplesPerBlock); p += 2;

  header[p++] = 'f'; header[p++] = 'a'; header[p++] = 'c'; header[p++] = 't';
  writeLE32(header, p, 4); p += 4;
  writeLE32(header, p, totalSamples); p += 4;

  header[p++] = 'd'; header[p++] = 'a'; header[p++] = 't'; header[p++] = 'a';
  writeLE32(header, p, adpcmDataSize); p += 4;
}

void writeLE16(uint8_t* buf, int offset, uint16_t val) {
  buf[offset]     = val & 0xFF;
  buf[offset + 1] = (val >> 8) & 0xFF;
}

void writeLE32(uint8_t* buf, int offset, uint32_t val) {
  buf[offset]     = val & 0xFF;
  buf[offset + 1] = (val >> 8) & 0xFF;
  buf[offset + 2] = (val >> 16) & 0xFF;
  buf[offset + 3] = (val >> 24) & 0xFF;
}

// ============================================================
// BLE audio send
// ============================================================
void sendAudioViaBLE() {
  if (!isConnected || !audioChar.notifyEnabled()) {
    Serial.println("[BLE] Not ready for sending");
    currentState = STATE_IDLE;
    setLED(0, 1, 0);
    return;
  }

  uint8_t wavHeader[WAV_HEADER_SIZE];
  buildWAVHeader(wavHeader, adpcmWritePos);

  uint32_t totalSize = WAV_HEADER_SIZE + adpcmWritePos;
  Serial.printf("[BLE] Sending %lu bytes\n", totalSize);

  // CRC
  uint32_t crc = 0xFFFFFFFF;
  for (int i = 0; i < WAV_HEADER_SIZE; i++) {
    crc ^= wavHeader[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
      else crc >>= 1;
    }
  }
  for (uint32_t i = 0; i < adpcmWritePos; i++) {
    crc ^= adpcmBuffer[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
      else crc >>= 1;
    }
  }
  crc ^= 0xFFFFFFFF;

  // START
  uint8_t startPkt[6];
  startPkt[0] = PKT_START;
  startPkt[1] = 0x03;
  startPkt[2] = (totalSize >> 24) & 0xFF;
  startPkt[3] = (totalSize >> 16) & 0xFF;
  startPkt[4] = (totalSize >> 8) & 0xFF;
  startPkt[5] = totalSize & 0xFF;
  sendWithRetry(startPkt, 6);

  // DATA
  uint16_t seqNo = 0;
  uint32_t streamPos = 0;

  while (streamPos < totalSize) {
    uint8_t dataPkt[BLE_MTU];
    dataPkt[0] = PKT_DATA;
    dataPkt[1] = (seqNo >> 8) & 0xFF;
    dataPkt[2] = seqNo & 0xFF;

    uint32_t remaining = totalSize - streamPos;
    uint16_t chunkSize = (remaining > DATA_PAYLOAD_SIZE) ? DATA_PAYLOAD_SIZE : remaining;

    for (uint16_t i = 0; i < chunkSize; i++) {
      uint32_t pos = streamPos + i;
      if (pos < WAV_HEADER_SIZE) {
        dataPkt[DATA_HEADER_SIZE + i] = wavHeader[pos];
      } else {
        dataPkt[DATA_HEADER_SIZE + i] = adpcmBuffer[pos - WAV_HEADER_SIZE];
      }
    }

    sendWithRetry(dataPkt, DATA_HEADER_SIZE + chunkSize);
    streamPos += chunkSize;
    seqNo++;

    if (seqNo % 50 == 0) {
      Serial.printf("[BLE] %u packets (%lu/%lu)\n", seqNo, streamPos, totalSize);
    }
  }

  // END
  uint8_t endPkt[5];
  endPkt[0] = PKT_END;
  endPkt[1] = (crc >> 24) & 0xFF;
  endPkt[2] = (crc >> 16) & 0xFF;
  endPkt[3] = (crc >> 8) & 0xFF;
  endPkt[4] = crc & 0xFF;
  sendWithRetry(endPkt, 5);

  Serial.printf("[BLE] Done! %u pkts, CRC=0x%08lX\n", seqNo + 2, crc);

  currentState = STATE_IDLE;
  setLED(0, 1, 0);
}

void sendWithRetry(const uint8_t* data, uint16_t len) {
  uint32_t start = millis();
  while (!audioChar.notify(data, len)) {
    if (millis() - start > 500) {
      Serial.println("[BLE] Send timeout");
      return;
    }
    delay(1);
  }
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
  currentState = STATE_IDLE;
  setLED(0, 1, 0);

  BLEConnection* conn = Bluefruit.Connection(conn_handle);
  char name[32] = {0};
  conn->getPeerName(name, sizeof(name));
  Serial.printf("[BLE] Connected: %s\n", name);

  toneConnected();
  motorPulse(100);

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

  currentState = STATE_INIT;
  setLED(1, 0, 0);
  Serial.printf("[BLE] Disconnected, reason=0x%02X\n", reason);

  toneDisconnected();
  motorPulse(50);
  delay(100);
  motorPulse(50);
}

void onTextWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len < 1) return;

  uint8_t command = data[0];
  Serial.printf("[CMD] BLE command: 0x%02X\n", command);

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
// Button handling (called from loop)
// ============================================================
void handleButtons() {
  uint32_t now = millis();

  // --- PTT Button (D4) ---
  bool pttReading = digitalRead(PIN_PTT);
  if (pttReading != lastPttState) {
    lastPttDebounce = now;
  }
  if ((now - lastPttDebounce) > DEBOUNCE_MS) {
    bool newPressed = (pttReading == LOW);
    if (newPressed != pttPressed) {
      pttPressed = newPressed;
      if (pttPressed) {
        Serial.println("[BTN] PTT pressed");
        pttPressTime = now;
        if (currentState == STATE_IDLE) {
          startRecording();
        }
      } else {
        Serial.println("[BTN] PTT released");
        if (currentState == STATE_RECORDING) {
          stopRecording();
        }
      }
    }
  }
  lastPttState = pttReading;

  // --- Action Button (D5) ---
  bool actionReading = digitalRead(PIN_ACTION);
  if (actionReading != lastActionState) {
    lastActionDebounce = now;
  }
  if ((now - lastActionDebounce) > DEBOUNCE_MS) {
    bool newPressed = (actionReading == LOW);
    if (newPressed != actionPressed) {
      actionPressed = newPressed;
      if (actionPressed) {
        actionPressTime = now;
        Serial.println("[BTN] Action pressed");
      } else {
        uint32_t holdDuration = now - actionPressTime;
        Serial.printf("[BTN] Action released (%lu ms)\n", holdDuration);

        if (holdDuration >= LONG_PRESS_MS) {
          if (isConnected && currentState == STATE_IDLE) {
            Serial.println("[BTN] Long press -> SD sync");
            currentState = STATE_SD_SYNC;
            sdSyncViaBLE();
          } else if (!isConnected) {
            Serial.println("[BTN] Long press but no BLE — listing SD files");
            uint16_t count = sdCountFiles();
            Serial.printf("[SD] %u recordings stored\n", count);
            toneConfirm();
          }
        } else {
          // Short press → send PLAY command to phone (Interactive AI)
          if (isConnected && textChar.notifyEnabled()) {
            uint8_t cmd = CMD_PLAY;
            textChar.notify(&cmd, 1);
            Serial.println("[BTN] Short press -> PLAY command sent to phone");
            toneConfirm();
            motorPulse(30);
          } else {
            Serial.printf("[BTN] Short press -> status (BLE=%d, SD=%d, files=%u)\n",
                          isConnected, sdReady, sdReady ? sdCountFiles() : 0);
            toneConfirm();
            motorPulse(30);
          }
        }
      }
    }
  }
  lastActionState = actionReading;
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);

  // WS2812B LED
  pixel.begin();
  pixel.setBrightness(LED_BRIGHTNESS);
  setLED(1, 0, 0);  // red = init

  // Vibration motor
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);

  // Buttons
  pinMode(PIN_PTT, INPUT_PULLUP);
  pinMode(PIN_ACTION, INPUT_PULLUP);

  Serial.println("=== Pinclaw Full Firmware (PCB V1.0) ===");
  Serial.println("Board: XIAO nRF52840 Sense");
  Serial.println("Audio: MAX98357A I2S");
  Serial.println("LED: WS2812B");
  Serial.println("Features: BLE + Buttons + I2S Speaker + SD Card + Motor");
  Serial.println();

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
  PDM.setGain(40);  // Boost PDM gain (default ~20, max 80) for better SNR
  // PDM is NOT started here to avoid clock noise coupling into I2S.
  // It will be started in startRecording() and stopped in stopRecording().
  Serial.println("[OK] PDM mic configured (gain=40, will start on record)");

  // --- Ready ---
  Serial.println();
  Serial.println("=== READY ===");
  Serial.println("PTT (D4): Hold to record");
  Serial.println("Action (D5): Short=status, Long(3s)=sync SD via BLE");
  Serial.printf("SD: %s (%u files)\n", sdReady ? "OK" : "N/A",
                sdReady ? sdCountFiles() : 0);
  Serial.println("BLE: Waiting for connection...");
  Serial.println();

  currentState = STATE_INIT;
  lastHeartbeatTime = millis();

  // Final ready tone
  delay(100);
  toneConnected();
}

// ============================================================
// Main loop
// ============================================================
void loop() {
  // Process mic data while recording
  if (currentState == STATE_RECORDING) {
    processPDMSamples();
  }

  // Process speaker I2S DMA feed
  if (currentState == STATE_PLAYING_SPK) {
    spkProcessI2S();
  }

  // Handle button presses (not during sending, sync, or speaker playback)
  if (currentState != STATE_SENDING && currentState != STATE_SD_SYNC && currentState != STATE_PLAYING_SPK) {
    handleButtons();
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

  // Standalone idle: can still record with PTT
  if (!isConnected && currentState == STATE_INIT) {
    currentState = STATE_IDLE;
    setLED(1, 0, 0);  // red = standalone mode
  }

  delay(1);
}
