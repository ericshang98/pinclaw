// Pinclaw BLE Audio Clip — XIAO nRF52840 Sense Firmware
// Records PDM microphone audio, encodes with Opus (CELT mode), streams via BLE in real-time
// iPhone triggers recording via BLE write command (0x01=start, 0x00=stop)

#include <bluefruit.h>
#include <PDM.h>

extern "C" {
  #include "opus.h"
}

// ============================================================
// BLE UUIDs — must match iOS app and Mac simulator
// ============================================================
#define SERVICE_UUID       "12345678-1234-1234-1234-123456789ABC"
#define TEXT_CHAR_UUID     "12345678-1234-1234-1234-123456789ABD"
#define AUDIO_CHAR_UUID    "12345678-1234-1234-1234-123456789ABE"
#define HEARTBEAT_CHAR_UUID "12345678-1234-1234-1234-123456789ABF"

// ============================================================
// BLE packet types
// ============================================================
#define PKT_START     0x01
#define PKT_DATA      0x02
#define PKT_END       0x03
#define PKT_HEARTBEAT 0x04

// ============================================================
// Single-button config
// ============================================================
#define PIN_BUTTON        5   // D5 — single button (active LOW, internal pullup)
#define DEBOUNCE_MS       50
#define LONG_PRESS_MS     500  // hold > 500ms = start recording
#define DOUBLE_TAP_MS     300  // window for second tap
#define CMD_PLAY          0x20 // button tap → phone triggers Interactive AI

// ============================================================
// Audio config
// ============================================================
#define SAMPLE_RATE       16000
#define PDM_BUFFER_SIZE   512    // samples per PDM callback
#define MAX_RECORD_SEC    10
#define SILENCE_THRESHOLD 600   // tuned for 0dB PDM gain (noise peak ~400)
#define SILENCE_TIMEOUT_MS 5000

// BLE MTU and packet sizing
// nRF52840 max MTU = 247, ATT header = 3, so max notify payload = 244
#define BLE_MTU           247
#define DATA_HEADER_SIZE  3     // type(1) + seqNo(2)
#define DATA_PAYLOAD_SIZE (BLE_MTU - DATA_HEADER_SIZE)  // 244

// ============================================================
// Opus encoder config
// ============================================================
#define OPUS_FRAME_SAMPLES 160   // 10ms at 16kHz
#define OPUS_MAX_PACKET    320   // max encoded frame bytes
#define OPUS_ENCODER_SIZE  7180  // opus_encoder_get_size(1)

// Codec ID for BLE START packet (matches OMI/Friend project)
#define CODEC_OPUS 0x14

// ============================================================
// State machine
// ============================================================
enum FirmwareState {
  STATE_INIT,
  STATE_IDLE,
  STATE_RECORDING
};

volatile FirmwareState currentState = STATE_INIT;

// ============================================================
// BLE objects
// ============================================================
BLEService        pinclawService(SERVICE_UUID);
BLECharacteristic textChar(TEXT_CHAR_UUID);
BLECharacteristic audioChar(AUDIO_CHAR_UUID);
BLECharacteristic heartbeatChar(HEARTBEAT_CHAR_UUID);

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

// Silence detection
volatile uint32_t lastSoundTime = 0;
volatile uint32_t recordStartTime = 0;

// Heartbeat
uint16_t heartbeatCounter = 0;
uint32_t lastHeartbeatTime = 0;
#define HEARTBEAT_INTERVAL_MS 50000  // 50 seconds

// Connection state
volatile bool isConnected = false;

// Serial test mode (record via serial command, dump Opus frames to serial)
volatile bool serialTestMode = false;

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

// === 2nd-order Butterworth high-pass filter at 80Hz ===
// Removes 50Hz mains hum + DC offset while preserving speech (fundamental ~100Hz+)
// Designed for fs=16000Hz, fc=80Hz
// Pre-computed coefficients (scipy.signal.butter(2, 80/8000, 'high'))
#define HPF2_B0  0.96907f
#define HPF2_B1 -1.93815f
#define HPF2_B2  0.96907f
#define HPF2_A1 -1.93729f
#define HPF2_A2  0.93900f
float hpfX1 = 0, hpfX2 = 0;  // input delay line
float hpfY1 = 0, hpfY2 = 0;  // output delay line

// === Noise gate (two-stage) ===
// Stage 1: Per-sample soft gate — anything below SOFT_GATE is zeroed
// Stage 2: Per-chunk hard gate — if chunk RMS < HARD_GATE, zero the whole chunk
// This ensures silence is truly silent while preserving speech dynamics
#define SOFT_GATE_THRESHOLD  120   // per-sample: below this amplitude -> 0
#define HARD_GATE_THRESHOLD_SQ (400L * 400L)  // per-chunk RMS^2: below this -> all zeros

// Startup discard — skip first N ms of PDM data (transient)
#define STARTUP_DISCARD_MS 80
volatile bool startupDiscardDone = false;


// ============================================================
// Base64 encoder (for serial test mode)
// ============================================================
static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void serialBase64(const uint8_t* data, uint32_t len) {
  char line[77];  // 76 chars + null
  int linePos = 0;

  for (uint32_t i = 0; i < len; i += 3) {
    uint32_t n = ((uint32_t)data[i]) << 16;
    if (i + 1 < len) n |= ((uint32_t)data[i + 1]) << 8;
    if (i + 2 < len) n |= data[i + 2];

    line[linePos++] = b64chars[(n >> 18) & 63];
    line[linePos++] = b64chars[(n >> 12) & 63];
    line[linePos++] = (i + 1 < len) ? b64chars[(n >> 6) & 63] : '=';
    line[linePos++] = (i + 2 < len) ? b64chars[n & 63] : '=';

    if (linePos >= 76) {
      line[linePos] = '\0';
      Serial.println(line);
      linePos = 0;
    }
  }
  if (linePos > 0) {
    line[linePos] = '\0';
    Serial.println(line);
  }
}


// ============================================================
// PDM callback — called from interrupt context
// ============================================================
void onPDMdata() {
  int bytesAvailable = PDM.available();
  PDM.read(pdmBuffer, bytesAvailable);
  pdmSamplesRead = bytesAvailable / 2;
}


// ============================================================
// Initialize Opus encoder
// ============================================================
void initOpusEncoder() {
  int err = opus_encoder_init(opusEnc, 16000, 1, OPUS_APPLICATION_RESTRICTED_LOWDELAY);
  if (err != OPUS_OK) {
    Serial.printf("[OPUS] encoder_init failed: %d\n", err);
    return;
  }

  opus_encoder_ctl(opusEnc, OPUS_SET_BITRATE(32000));
  opus_encoder_ctl(opusEnc, OPUS_SET_VBR(1));
  opus_encoder_ctl(opusEnc, OPUS_SET_VBR_CONSTRAINT(0));
  opus_encoder_ctl(opusEnc, OPUS_SET_COMPLEXITY(3));
  opus_encoder_ctl(opusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  opus_encoder_ctl(opusEnc, OPUS_SET_LSB_DEPTH(16));
  opus_encoder_ctl(opusEnc, OPUS_SET_DTX(0));
  opus_encoder_ctl(opusEnc, OPUS_SET_INBAND_FEC(0));
  opus_encoder_ctl(opusEnc, OPUS_SET_PACKET_LOSS_PERC(0));

  Serial.println("[OPUS] Encoder initialized (32kbps VBR, complexity=3, CELT lowdelay)");
}


// ============================================================
// Send one Opus frame via BLE as a DATA packet
// ============================================================
void sendOpusFrame(const uint8_t* frameData, int frameLen) {
  if (!isConnected || !audioChar.notifyEnabled()) return;

  // [0x02][seqNo 2B BE][opus_frame_data...]
  uint8_t pkt[DATA_HEADER_SIZE + OPUS_MAX_PACKET];
  pkt[0] = PKT_DATA;
  pkt[1] = (opusSeqNo >> 8) & 0xFF;
  pkt[2] = opusSeqNo & 0xFF;
  memcpy(&pkt[DATA_HEADER_SIZE], frameData, frameLen);

  sendWithRetry(pkt, DATA_HEADER_SIZE + frameLen);
  opusSeqNo++;
}


// ============================================================
// Encode accumulated PCM and send via BLE (or dump to serial)
// ============================================================
void encodeAndSendFrame() {
  int encodedLen = opus_encode(opusEnc, pcmAccum, OPUS_FRAME_SAMPLES, opusOutBuf, OPUS_MAX_PACKET);

  if (encodedLen < 0) {
    Serial.printf("[OPUS] encode error: %d\n", encodedLen);
    return;
  }

  if (serialTestMode) {
    // Dump Opus frame to serial for test script
    Serial.printf(">>> OPUS_FRAME %d\n", encodedLen);
    serialBase64(opusOutBuf, encodedLen);
  } else {
    sendOpusFrame(opusOutBuf, encodedLen);
  }
}


// ============================================================
// Process PDM samples: filter + encode + stream
// ============================================================
void processPDMSamples() {
  if (pdmSamplesRead == 0 || currentState != STATE_RECORDING) return;

  int count = pdmSamplesRead;
  pdmSamplesRead = 0;

  // Discard startup transient
  if (!startupDiscardDone) {
    if (millis() - recordStartTime < STARTUP_DISCARD_MS) {
      return;
    }
    startupDiscardDone = true;
  }

  int16_t peakAmplitude = 0;

  // --- Pass 1: HPF + per-sample soft gate ---
  int64_t energySum = 0;
  for (int i = 0; i < count; i++) {
    float x = (float)pdmBuffer[i];

    // 2nd-order Butterworth HPF at 80Hz — kills 50Hz hum + DC
    float y = HPF2_B0 * x + HPF2_B1 * hpfX1 + HPF2_B2 * hpfX2
                           - HPF2_A1 * hpfY1 - HPF2_A2 * hpfY2;
    hpfX2 = hpfX1; hpfX1 = x;
    hpfY2 = hpfY1; hpfY1 = y;

    int16_t sample = (int16_t)constrain((int32_t)y, -32768, 32767);

    // Per-sample soft gate: small amplitudes -> zero
    if (abs(sample) < SOFT_GATE_THRESHOLD) {
      sample = 0;
    }

    pdmBuffer[i] = sample;
    energySum += (int32_t)sample * sample;
  }

  // --- Per-chunk hard gate: if overall energy is low, zero everything ---
  int64_t avgEnergySq = energySum / count;
  bool gated = (avgEnergySq < HARD_GATE_THRESHOLD_SQ);

  // --- Pass 2: Accumulate into Opus frames and encode ---
  for (int i = 0; i < count; i++) {
    int16_t sample = gated ? 0 : pdmBuffer[i];

    int16_t absSample = abs(sample);
    if (absSample > peakAmplitude) peakAmplitude = absSample;

    // Feed into PCM accumulator
    pcmAccum[pcmAccumPos++] = sample;

    // When we have a full Opus frame (160 samples = 10ms), encode and send
    if (pcmAccumPos >= OPUS_FRAME_SAMPLES) {
      encodeAndSendFrame();
      pcmAccumPos = 0;
    }
  }

  // Update silence detection
  if (peakAmplitude > SILENCE_THRESHOLD) {
    lastSoundTime = millis();
  }

  uint32_t now = millis();

  // Auto-stop on silence (5 seconds)
  if (now - lastSoundTime > SILENCE_TIMEOUT_MS) {
    Serial.println("[REC] Auto-stop: silence detected");
    stopRecording();
    return;
  }

  // In serial test mode, use fixed 5-second duration
  if (serialTestMode && (now - recordStartTime > 5000UL)) {
    Serial.println("[TEST] 5s test recording complete");
    stopRecording();
    return;
  }

  // Hard stop at max duration
  if (now - recordStartTime > (MAX_RECORD_SEC * 1000UL)) {
    Serial.println("[REC] Auto-stop: max duration reached");
    stopRecording();
    return;
  }
}


// ============================================================
// Recording control
// ============================================================
void startRecording() {
  if (currentState != STATE_IDLE) return;

  Serial.println("[REC] Starting recording...");

  // Reset Opus encoder state
  opus_encoder_ctl(opusEnc, OPUS_RESET_STATE);

  // Reset PCM accumulator and sequence number
  pcmAccumPos = 0;
  opusSeqNo = 0;

  // Reset filter state
  hpfX1 = hpfX2 = hpfY1 = hpfY2 = 0;
  startupDiscardDone = false;

  // Reset silence detection
  lastSoundTime = millis();
  recordStartTime = millis();

  // Send START packet immediately: [0x01][codec=0x14][0x00][0x00][0x00][0x00]
  if (!serialTestMode) {
    uint8_t startPkt[6];
    startPkt[0] = PKT_START;
    startPkt[1] = CODEC_OPUS;  // codec: Opus (0x14 = 20)
    startPkt[2] = 0x00;
    startPkt[3] = 0x00;
    startPkt[4] = 0x00;
    startPkt[5] = 0x00;
    sendWithRetry(startPkt, 6);
    Serial.println("[BLE] START packet sent (codec=Opus)");
  } else {
    Serial.println(">>> OPUS_START");
  }

  // Start PDM mic
  if (!PDM.begin(1, SAMPLE_RATE)) {
    Serial.println("[REC] PDM start failed!");
    return;
  }

  // PDM gain: +12dB (0x40), matches OMI/Friend config
  // 0x28=0dB, each +1 = +0.5dB
  NRF_PDM->GAINL = 0x40;
  NRF_PDM->GAINR = 0x40;

  Serial.println("[REC] PDM started (gain=+12dB)");

  // Set LED blue for recording
  setLED(0, 0, 1);  // blue

  currentState = STATE_RECORDING;
}

void stopRecording() {
  if (currentState != STATE_RECORDING) return;

  // Stop PDM mic
  PDM.end();
  Serial.println("[REC] PDM stopped");

  // Flush remaining PCM accumulator (pad with zeros if < 160 samples)
  if (pcmAccumPos > 0) {
    // Zero-pad the remainder
    for (int i = pcmAccumPos; i < OPUS_FRAME_SAMPLES; i++) {
      pcmAccum[i] = 0;
    }
    encodeAndSendFrame();
    pcmAccumPos = 0;
  }

  uint32_t duration = millis() - recordStartTime;
  Serial.printf("[REC] Stopped. Duration: %lu ms, Opus frames: %u\n", duration, opusSeqNo);

  // Send END packet: [0x03][totalFrames 4B BE]
  if (!serialTestMode) {
    uint8_t endPkt[5];
    endPkt[0] = PKT_END;
    endPkt[1] = (opusSeqNo >> 24) & 0xFF;
    endPkt[2] = (opusSeqNo >> 16) & 0xFF;
    endPkt[3] = (opusSeqNo >> 8) & 0xFF;
    endPkt[4] = opusSeqNo & 0xFF;
    sendWithRetry(endPkt, 5);
    Serial.printf("[BLE] END packet sent (frames=%u)\n", opusSeqNo);
  } else {
    Serial.printf(">>> OPUS_END %u\n", opusSeqNo);
  }

  // Set LED off
  setLED(0, 0, 0);

  if (serialTestMode) {
    serialTestMode = false;
  }

  currentState = isConnected ? STATE_IDLE : STATE_INIT;
  if (isConnected) setLED(0, 1, 0);      // green = connected idle
  else setLED(1, 0, 0);                    // red = disconnected
}


// ============================================================
// BLE packet sender with flow control
// ============================================================
void sendWithRetry(const uint8_t* data, uint16_t len) {
  uint32_t start = millis();
  while (!audioChar.notify(data, len)) {
    if (millis() - start > 500) {
      Serial.println("[BLE] Send timeout, dropping packet");
      return;
    }
    delay(1);  // yield to BLE stack
  }
}


// ============================================================
// LED control (XIAO nRF52840 — active LOW)
// ============================================================
void setLED(bool red, bool green, bool blue) {
  digitalWrite(LED_RED,   red   ? LOW : HIGH);
  digitalWrite(LED_GREEN, green ? LOW : HIGH);
  digitalWrite(LED_BLUE,  blue  ? LOW : HIGH);
}


// ============================================================
// BLE callbacks
// ============================================================
void onConnect(uint16_t conn_handle) {
  isConnected = true;
  currentState = STATE_IDLE;
  setLED(0, 1, 0);  // green = connected

  BLEConnection* conn = Bluefruit.Connection(conn_handle);
  char name[32] = {0};
  conn->getPeerName(name, sizeof(name));
  Serial.printf("[BLE] Connected: %s\n", name);
}

void onDisconnect(uint16_t conn_handle, uint8_t reason) {
  isConnected = false;

  // Stop recording if in progress
  if (currentState == STATE_RECORDING) {
    PDM.end();
    pcmAccumPos = 0;
  }

  currentState = STATE_INIT;
  setLED(1, 0, 0);  // red = disconnected

  Serial.printf("[BLE] Disconnected, reason=0x%02X\n", reason);
}

// Text characteristic write handler — iPhone sends recording commands
void onTextWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len < 1) return;

  uint8_t command = data[0];
  Serial.printf("[CMD] Received command: 0x%02X\n", command);

  switch (command) {
    case 0x01:  // Start recording
      startRecording();
      break;
    case 0x00:  // Stop recording
      if (currentState == STATE_RECORDING) {
        stopRecording();
      }
      break;
    default:
      Serial.printf("[CMD] Unknown command: 0x%02X\n", command);
      break;
  }
}


// ============================================================
// Heartbeat
// ============================================================
void sendHeartbeat() {
  if (!isConnected || !heartbeatChar.notifyEnabled()) return;

  // [0x04][counter:2B BE][flags:1B]
  uint8_t pkt[4];
  pkt[0] = PKT_HEARTBEAT;
  pkt[1] = (heartbeatCounter >> 8) & 0xFF;
  pkt[2] = heartbeatCounter & 0xFF;
  pkt[3] = (currentState == STATE_RECORDING) ? 0x01 : 0x00;

  if (heartbeatChar.notify(pkt, 4)) {
    Serial.printf("[HB] #%u sent (flags=0x%02X)\n", heartbeatCounter, pkt[3]);
  }
  heartbeatCounter++;
}


// ============================================================
// Single-button handling
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
  } else {
    Serial.println("[BTN] Single tap (no BLE)");
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
  // Don't block if no USB serial
  // while (!Serial) delay(10);

  // LED setup
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  setLED(1, 0, 0);  // red = initializing

  // Single button
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.println("=== Pinclaw BLE Audio Clip (Opus) ===");
  Serial.println("Board: XIAO nRF52840 Sense");

  // --- Initialize Opus encoder ---
  initOpusEncoder();

  // --- BLE Setup ---
  // Configure MTU before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);  // +4 dBm
  Bluefruit.setName("Pinclaw-5");

  // Connection callbacks
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  // --- Service setup ---
  pinclawService.begin();

  // Text characteristic: read + notify + write (iPhone sends commands)
  textChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY | CHR_PROPS_WRITE);
  textChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  textChar.setMaxLen(20);
  textChar.setWriteCallback(onTextWrite);
  textChar.begin();

  // Audio characteristic: read + notify (firmware sends audio data)
  audioChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  audioChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  audioChar.setMaxLen(BLE_MTU - 3);  // max notify payload
  audioChar.begin();

  // Heartbeat characteristic: read + notify
  heartbeatChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  heartbeatChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  heartbeatChar.setMaxLen(4);
  heartbeatChar.begin();

  // --- Advertising ---
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(pinclawService);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.setInterval(32, 244);  // fast=20ms, slow=152.5ms
  Bluefruit.Advertising.setFastTimeout(30);     // 30s fast advertising
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);  // advertise forever

  Serial.println("[OK] Advertising as 'Pinclaw-5'");

  // --- PDM Microphone (lazy init — started only when recording) ---
  PDM.onReceive(onPDMdata);
  PDM.setBufferSize(PDM_BUFFER_SIZE * 2);  // bytes, not samples
  // PDM is NOT started here — will be started in startRecording()
  Serial.println("[OK] PDM mic configured (will start on record)");
  Serial.println("[OK] Button (D5): Hold=record, Tap=play, DoubleTap=BLE toggle");
  Serial.println("[OK] Waiting for connection...");

  currentState = STATE_INIT;
  lastHeartbeatTime = millis();
}


// ============================================================
// Main loop
// ============================================================
void loop() {
  // Process PDM samples (filter + encode + stream)
  if (currentState == STATE_RECORDING) {
    processPDMSamples();
  }

  // Handle single button
  handleButton();

  // Heartbeat timer
  if (isConnected && (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)) {
    sendHeartbeat();
    lastHeartbeatTime = millis();
  }

  // Update state on first connection
  if (isConnected && currentState == STATE_INIT) {
    currentState = STATE_IDLE;
  }

  // Standalone idle: allow button recording even without BLE
  if (!isConnected && currentState == STATE_INIT) {
    currentState = STATE_IDLE;
    setLED(1, 0, 0);  // red = standalone mode
  }

  // Serial test command: send 'T' to trigger test recording
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'T' && currentState != STATE_RECORDING) {
      Serial.println("[TEST] Serial test mode activated");
      serialTestMode = true;
      currentState = STATE_IDLE;  // allow recording even without BLE
      startRecording();
    }
  }

  // Small delay to prevent busy-waiting
  delay(1);
}
