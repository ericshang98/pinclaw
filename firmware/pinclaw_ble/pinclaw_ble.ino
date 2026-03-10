// Pinclaw BLE Audio Clip — XIAO nRF52840 Sense Firmware
// Records PDM microphone audio, encodes as IMA ADPCM, sends via BLE
// iPhone triggers recording via BLE write command (0x01=start, 0x00=stop)

#include <bluefruit.h>
#include <PDM.h>

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
// Audio config
// ============================================================
#define SAMPLE_RATE       16000
#define PDM_BUFFER_SIZE   512    // samples per PDM callback
#define MAX_RECORD_SEC    10
#define SILENCE_THRESHOLD 500
#define SILENCE_TIMEOUT_MS 2000

// IMA ADPCM: 16-bit -> 4-bit = 4:1 compression
// 16000 Hz * 2 bytes * 10s = 320000 bytes PCM
// ADPCM = 320000 / 4 = 80000 bytes
#define ADPCM_BUFFER_SIZE 80000

// BLE MTU and packet sizing
// nRF52840 max MTU = 247, ATT header = 3, so max notify payload = 244
#define BLE_MTU           247
#define DATA_HEADER_SIZE  3     // type(1) + seqNo(2)
#define DATA_PAYLOAD_SIZE (BLE_MTU - DATA_HEADER_SIZE)  // 244

// ============================================================
// State machine
// ============================================================
enum FirmwareState {
  STATE_INIT,
  STATE_IDLE,
  STATE_RECORDING,
  STATE_SENDING
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
// IMA ADPCM encoder state
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

// ============================================================
// Audio recording buffer
// ============================================================
uint8_t adpcmBuffer[ADPCM_BUFFER_SIZE];
volatile uint32_t adpcmWritePos = 0;
volatile bool     nibbleHigh = false;  // track nibble position

// Silence detection
volatile uint32_t lastSoundTime = 0;
volatile uint32_t recordStartTime = 0;

// Heartbeat
uint16_t heartbeatCounter = 0;
uint32_t lastHeartbeatTime = 0;
#define HEARTBEAT_INTERVAL_MS 50000  // 50 seconds

// Connection state
volatile bool isConnected = false;

// ============================================================
// IMA ADPCM encode one sample -> 4-bit nibble
// ============================================================
uint8_t adpcmEncodeSample(int16_t sample) {
  int32_t diff = sample - adpcmPrevSample;
  uint8_t nibble = 0;

  if (diff < 0) {
    nibble = 8;  // sign bit
    diff = -diff;
  }

  int16_t step = stepTable[adpcmIndex];
  int32_t tempStep = step;

  if (diff >= tempStep) { nibble |= 4; diff -= tempStep; }
  tempStep >>= 1;
  if (diff >= tempStep) { nibble |= 2; diff -= tempStep; }
  tempStep >>= 1;
  if (diff >= tempStep) { nibble |= 1; }

  // Decode to update predictor (must match decoder exactly)
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
// PDM callback — called from interrupt context
// ============================================================
void onPDMdata() {
  int bytesAvailable = PDM.available();
  PDM.read(pdmBuffer, bytesAvailable);
  pdmSamplesRead = bytesAvailable / 2;
}

// ============================================================
// Process PDM samples: encode + silence detection
// ============================================================
void processPDMSamples() {
  if (pdmSamplesRead == 0 || currentState != STATE_RECORDING) return;

  int count = pdmSamplesRead;
  pdmSamplesRead = 0;

  int16_t peakAmplitude = 0;

  for (int i = 0; i < count; i++) {
    int16_t sample = pdmBuffer[i];
    int16_t absSample = abs(sample);
    if (absSample > peakAmplitude) peakAmplitude = absSample;

    // Encode to ADPCM
    if (adpcmWritePos < ADPCM_BUFFER_SIZE) {
      uint8_t nibble = adpcmEncodeSample(sample);
      if (!nibbleHigh) {
        // Low nibble first (IMA ADPCM standard: low nibble in lower 4 bits)
        adpcmBuffer[adpcmWritePos] = nibble;
        nibbleHigh = true;
      } else {
        // High nibble
        adpcmBuffer[adpcmWritePos] |= (nibble << 4);
        nibbleHigh = false;
        adpcmWritePos++;
      }
    }
  }

  // Update silence detection
  if (peakAmplitude > SILENCE_THRESHOLD) {
    lastSoundTime = millis();
  }

  uint32_t now = millis();

  // Auto-stop on silence (2 seconds)
  if (now - lastSoundTime > SILENCE_TIMEOUT_MS) {
    Serial.println("[REC] Auto-stop: silence detected");
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

  // Reset ADPCM encoder
  adpcmPrevSample = 0;
  adpcmIndex = 0;
  adpcmWritePos = 0;
  nibbleHigh = false;

  // Reset silence detection
  lastSoundTime = millis();
  recordStartTime = millis();

  // Set LED blue for recording
  setLED(0, 0, 1);  // blue

  currentState = STATE_RECORDING;
}

void stopRecording() {
  if (currentState != STATE_RECORDING) return;

  // Flush last nibble if odd
  if (nibbleHigh) {
    adpcmWritePos++;
    nibbleHigh = false;
  }

  uint32_t duration = millis() - recordStartTime;
  Serial.printf("[REC] Stopped. Duration: %lu ms, ADPCM bytes: %lu\n", duration, adpcmWritePos);

  // Set LED off while preparing to send
  setLED(0, 0, 0);

  currentState = STATE_SENDING;
  sendAudioViaBLE();
}

// ============================================================
// WAV header builder — IMA ADPCM format
// ============================================================
// IMA ADPCM WAV structure:
//   RIFF header (12 bytes)
//   fmt  chunk  (28 bytes) — format 0x0011
//   fact chunk  (12 bytes)
//   data chunk  (8 bytes header + data)
// Total header: 60 bytes

#define WAV_HEADER_SIZE 60

void buildWAVHeader(uint8_t* header, uint32_t adpcmDataSize) {
  // IMA ADPCM parameters
  uint16_t blockAlign = 256;            // bytes per block
  uint16_t samplesPerBlock = 505;       // ((blockAlign - 4) * 2) + 1
  uint16_t bitsPerSample = 4;
  uint32_t byteRate = SAMPLE_RATE * blockAlign / samplesPerBlock;
  uint16_t extraSize = 2;              // cbSize: extra bytes in fmt chunk

  uint32_t totalSamples = (adpcmDataSize / (blockAlign > 0 ? blockAlign : 1)) * samplesPerBlock;

  // Total file size = header + data
  uint32_t fileSize = WAV_HEADER_SIZE + adpcmDataSize - 8;  // RIFF size excludes first 8 bytes

  int p = 0;

  // RIFF header
  header[p++] = 'R'; header[p++] = 'I'; header[p++] = 'F'; header[p++] = 'F';
  writeLE32(header, p, fileSize); p += 4;
  header[p++] = 'W'; header[p++] = 'A'; header[p++] = 'V'; header[p++] = 'E';

  // fmt chunk
  header[p++] = 'f'; header[p++] = 'm'; header[p++] = 't'; header[p++] = ' ';
  writeLE32(header, p, 20); p += 4;              // chunk size: 20 bytes (includes cbSize field and extra)
  writeLE16(header, p, 0x0011); p += 2;          // format: IMA ADPCM
  writeLE16(header, p, 1); p += 2;               // channels: mono
  writeLE32(header, p, SAMPLE_RATE); p += 4;     // sample rate
  writeLE32(header, p, byteRate); p += 4;        // byte rate
  writeLE16(header, p, blockAlign); p += 2;      // block align
  writeLE16(header, p, bitsPerSample); p += 2;   // bits per sample
  writeLE16(header, p, extraSize); p += 2;       // cbSize
  writeLE16(header, p, samplesPerBlock); p += 2;  // samples per block

  // fact chunk
  header[p++] = 'f'; header[p++] = 'a'; header[p++] = 'c'; header[p++] = 't';
  writeLE32(header, p, 4); p += 4;               // chunk size
  writeLE32(header, p, totalSamples); p += 4;    // total samples

  // data chunk header
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
// CRC32 (same polynomial as Mac simulator)
// ============================================================
uint32_t calculateCRC32(const uint8_t* data, uint32_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xEDB88320;
      else
        crc >>= 1;
    }
  }
  return crc ^ 0xFFFFFFFF;
}

// ============================================================
// BLE packet sender with flow control
// ============================================================
void sendAudioViaBLE() {
  if (!isConnected || !audioChar.notifyEnabled()) {
    Serial.println("[BLE] Not connected or notify not enabled");
    currentState = STATE_IDLE;
    setLED(0, 1, 0);  // green = connected idle
    return;
  }

  // Build WAV: header + ADPCM data
  uint8_t wavHeader[WAV_HEADER_SIZE];
  buildWAVHeader(wavHeader, adpcmWritePos);

  uint32_t totalSize = WAV_HEADER_SIZE + adpcmWritePos;
  Serial.printf("[BLE] Sending %lu bytes (header=%d, adpcm=%lu)\n",
                totalSize, WAV_HEADER_SIZE, adpcmWritePos);

  // Combine header + data for CRC calculation
  // We'll compute CRC incrementally to avoid needing another big buffer
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

  // --- START packet: [0x01][codec:0x03][totalSize:4B BE] = 6 bytes ---
  uint8_t startPkt[6];
  startPkt[0] = PKT_START;
  startPkt[1] = 0x03;  // codec: IMA ADPCM WAV
  startPkt[2] = (totalSize >> 24) & 0xFF;
  startPkt[3] = (totalSize >> 16) & 0xFF;
  startPkt[4] = (totalSize >> 8) & 0xFF;
  startPkt[5] = totalSize & 0xFF;

  sendWithRetry(startPkt, 6);

  // --- DATA packets ---
  uint16_t seqNo = 0;
  uint32_t bytesSent = 0;

  // First, send WAV header bytes
  // We'll create a virtual stream: wavHeader[0..59] + adpcmBuffer[0..adpcmWritePos-1]
  uint32_t totalToSend = totalSize;
  uint32_t streamPos = 0;

  while (streamPos < totalToSend) {
    uint8_t dataPkt[BLE_MTU];
    dataPkt[0] = PKT_DATA;
    dataPkt[1] = (seqNo >> 8) & 0xFF;
    dataPkt[2] = seqNo & 0xFF;

    uint16_t payloadLen = 0;
    uint32_t remaining = totalToSend - streamPos;
    uint16_t chunkSize = (remaining > DATA_PAYLOAD_SIZE) ? DATA_PAYLOAD_SIZE : remaining;

    for (uint16_t i = 0; i < chunkSize; i++) {
      uint32_t pos = streamPos + i;
      if (pos < WAV_HEADER_SIZE) {
        dataPkt[DATA_HEADER_SIZE + i] = wavHeader[pos];
      } else {
        dataPkt[DATA_HEADER_SIZE + i] = adpcmBuffer[pos - WAV_HEADER_SIZE];
      }
      payloadLen++;
    }

    sendWithRetry(dataPkt, DATA_HEADER_SIZE + payloadLen);

    streamPos += chunkSize;
    seqNo++;

    // Progress every 50 packets
    if (seqNo % 50 == 0) {
      Serial.printf("[BLE] Sent %u packets (%lu/%lu bytes)\n", seqNo, streamPos, totalToSend);
    }
  }

  // --- END packet: [0x03][CRC32:4B BE] ---
  uint8_t endPkt[5];
  endPkt[0] = PKT_END;
  endPkt[1] = (crc >> 24) & 0xFF;
  endPkt[2] = (crc >> 16) & 0xFF;
  endPkt[3] = (crc >> 8) & 0xFF;
  endPkt[4] = crc & 0xFF;

  sendWithRetry(endPkt, 5);

  Serial.printf("[BLE] Done! %u packets, CRC=0x%08lX\n", seqNo + 2, crc);

  currentState = STATE_IDLE;
  setLED(0, 1, 0);  // green = connected idle
}

// Send a BLE notification with retry and flow control
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
  currentState = STATE_INIT;
  setLED(1, 0, 0);  // red = disconnected

  // Stop recording if in progress
  if (currentState == STATE_RECORDING) {
    currentState = STATE_IDLE;  // will be overwritten to INIT above
  }

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

  Serial.println("=== Pinclaw BLE Audio Clip ===");
  Serial.println("Board: XIAO nRF52840 Sense");

  // --- BLE Setup ---
  // Configure MTU before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);  // +4 dBm
  Bluefruit.setName("Pinclaw-Clip");

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

  Serial.println("[OK] Advertising as 'Pinclaw-Clip'");

  // --- PDM Microphone ---
  PDM.onReceive(onPDMdata);
  PDM.setBufferSize(PDM_BUFFER_SIZE * 2);  // bytes, not samples

  if (!PDM.begin(1, SAMPLE_RATE)) {
    Serial.println("[ERROR] PDM microphone failed!");
    setLED(1, 0, 0);  // red = error
    while (1) delay(1000);
  }

  Serial.println("[OK] PDM microphone ready (16kHz mono)");
  Serial.println("[OK] Waiting for connection...");

  currentState = STATE_INIT;
  lastHeartbeatTime = millis();
}

// ============================================================
// Main loop
// ============================================================
void loop() {
  // Process PDM samples (encode + silence detection)
  if (currentState == STATE_RECORDING) {
    processPDMSamples();
  }

  // Heartbeat timer
  if (isConnected && (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)) {
    sendHeartbeat();
    lastHeartbeatTime = millis();
  }

  // Small delay to prevent busy-waiting
  delay(1);
}
