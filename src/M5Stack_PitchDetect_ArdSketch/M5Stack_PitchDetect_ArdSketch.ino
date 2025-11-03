/*
  Purpose:
    - Sample audio from the built-in microphone of M5Stack Core2.
    - Estimate pitch (single-note monophonic input) within MIDI note range 58..75.
    - Send MIDI Note On/Off messages via M5 Unit MIDI or UART MIDI fallback.
    - Display activity and estimated MIDI note on the screen.

  Libraries required:
    - M5Unified (https://github.com/m5stack/M5Unified)
    - M5GFX (part of M5Unified / included)
    - Arduino built-in I2S (for ADC sampling on ESP32)

  Notes:
    - Goertzel filter bank is used to detect pitches in the fixed MIDI range.
*/

#include <M5Unified.h>
#include <driver/i2s.h>
#include <math.h>

// ---------- Configuration ----------
const int SAMPLE_RATE = 8000;        // Hz - audio sampling rate
const int BLOCK_SIZE = 256;          // samples per block for processing
const int MIDI_MIN = 58;
const int MIDI_MAX = 75;
const int NUM_NOTES = MIDI_MAX - MIDI_MIN + 1;

// Frequency table
double noteFreqs[NUM_NOTES];

// Thresholds and detection parameters
const float POWER_THRESHOLD = 1.5e6f; // Empirical threshold (may need tuning)
const float NOTE_DETECTION_RATIO = 1.8f; // required ratio vs second best
const int SILENCE_FRAMES_FOR_OFF = 3;
const int STABLE_FRAMES_TO_CONFIRM = 2;

// I2S config
#define I2S_PORT I2S_NUM_0

// Buffers and state
uint16_t i2sBuffer[BLOCK_SIZE];
float samples[BLOCK_SIZE];

typedef struct {
  double coeff;
  double targetFreq;
  double magnitude;
} GoertzelState;

GoertzelState gStates[NUM_NOTES];

int currentNote = -1;
int pendingNote = -1;
int stableCount = 0;
int silenceCount = 0;

LGFX_Sprite* sprite = nullptr;
bool midiUnitAvailable = false;
HardwareSerial* midiSerial = nullptr;

double midiNoteToFreq(int note) {
  return 440.0 * pow(2.0, (note - 69.0) / 12.0);
}

void sendMidiNoteOn(uint8_t note, uint8_t vel) {
  if (midiUnitAvailable) {
    auto u = M5.Units.getUnit("midi");
    if (u) {
      Stream* s = static_cast<Stream*>(u);
      if (s) { s->write((uint8_t[]){0x90, note, vel}, 3); return; }
    }
  }
  if (midiSerial) {
    midiSerial->write(0x90);
    midiSerial->write(note);
    midiSerial->write(vel);
  }
}

void sendMidiNoteOff(uint8_t note, uint8_t vel) {
  if (midiUnitAvailable) {
    auto u = M5.Units.getUnit("midi");
    if (u) {
      Stream* s = static_cast<Stream*>(u);
      if (s) { s->write((uint8_t[]){0x80, note, vel}, 3); return; }
    }
  }
  if (midiSerial) {
    midiSerial->write(0x80);
    midiSerial->write(note);
    midiSerial->write(vel);
  }
}

void initGoertzelStates() {
  for (int i = 0; i < NUM_NOTES; ++i) {
    gStates[i].targetFreq = noteFreqs[i];
    double k = 0.5 + ((BLOCK_SIZE * gStates[i].targetFreq) / SAMPLE_RATE);
    gStates[i].coeff = 2.0 * cos((2.0 * M_PI * k) / BLOCK_SIZE);
    gStates[i].magnitude = 0.0;
  }
}

void processGoertzel(float* inbuf, int len) {
  for (int i = 0; i < NUM_NOTES; ++i) {
    double q0 = 0, q1 = 0, q2 = 0;
    double coeff = gStates[i].coeff;
    for (int j = 0; j < len; ++j) {
      q0 = coeff * q1 - q2 + inbuf[j];
      q2 = q1;
      q1 = q0;
    }
    double real = q1 - q2 * cos(2.0 * M_PI * gStates[i].targetFreq / SAMPLE_RATE);
    double imag = q2 * sin(2.0 * M_PI * gStates[i].targetFreq / SAMPLE_RATE);
    double mag = real * real + imag * imag;
    gStates[i].magnitude = mag;
  }
}

void i2sInit() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = 0,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_0); // adjust if your board differs
  i2s_adc_enable(I2S_PORT);
}

bool readI2SBlock() {
  size_t bytesRead = 0;
  esp_err_t r = i2s_read(I2S_PORT, (void*)i2sBuffer, sizeof(i2sBuffer), &bytesRead, pdMS_TO_TICKS(200));
  if (r != ESP_OK || bytesRead != sizeof(i2sBuffer)) return false;
  double mean = 0.0;
  for (int i = 0; i < BLOCK_SIZE; ++i) {
    int16_t v = (int16_t)i2sBuffer[i];
    double f = (double)v / 32768.0;
    samples[i] = (float)f;
    mean += f;
  }
  mean /= BLOCK_SIZE;
  for (int i = 0; i < BLOCK_SIZE; ++i) samples[i] -= (float)mean;
  return true;
}

double computePower(float* buf, int len) {
  double s = 0.0;
  for (int i = 0; i < len; ++i) {
    double v = buf[i];
    s += v * v;
  }
  return s;
}

void updateDisplay(bool active, int midiNote, double power) {
  sprite->fillSprite(TFT_BLACK);
  sprite->setTextColor(TFT_WHITE, TFT_BLACK);
  sprite->setTextSize(2);
  sprite->drawString("Pitch->MIDI", 10, 6);
  sprite->setTextSize(2);
  if (active) {
    sprite->setTextColor(TFT_GREEN, TFT_BLACK);
    sprite->drawString("Sound: ON", 10, 36);
    sprite->setTextColor(TFT_YELLOW, TFT_BLACK);
    char buf[32];
    if (midiNote >= 0) {
      sprintf(buf, "Note: %d", midiNote);
    } else {
      sprintf(buf, "Note: -");
    }
    sprite->drawString(buf, 10, 64);
  } else {
    sprite->setTextColor(TFT_RED, TFT_BLACK);
    sprite->drawString("Sound: OFF", 10, 36);
    sprite->setTextColor(TFT_YELLOW, TFT_BLACK);
    sprite->drawString("Note: -", 10, 64);
  }
  char pbuf[48];
  sprintf(pbuf, "Power: %.1f", power);
  sprite->setTextSize(1);
  sprite->setTextColor(TFT_WHITE, TFT_BLACK);
  sprite->drawString(pbuf, 10, 96);
  sprite->pushSprite(0, 0);
}

void initMidiSerialFallback() {
  const int TX_PIN = 17;
  const int RX_PIN = 16;
  Serial2.begin(31250, SERIAL_8N1, RX_PIN, TX_PIN);
  midiSerial = &Serial2;
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);

  sprite = new LGFX_Sprite(&M5.Display);
  sprite->setColorDepth(8);
  sprite->createSprite(320, 240);
  M5.Display.clear();

  for (int n = MIDI_MIN; n <= MIDI_MAX; ++n) {
    noteFreqs[n - MIDI_MIN] = midiNoteToFreq(n);
  }

  initGoertzelStates();

  auto u = M5.Units.getUnit("midi");
  if (u) midiUnitAvailable = true;
  else {
    midiUnitAvailable = false;
    initMidiSerialFallback();
  }

  i2sInit();
  updateDisplay(false, -1, 0.0);
  Serial.println("MIDI Pitch Detect started");
}

void loop() {
  bool ok = readI2SBlock();
  if (!ok) { delay(10); return; }

  double power = computePower(samples, BLOCK_SIZE);
  bool active = power > POWER_THRESHOLD;

  if (!active) silenceCount++; else silenceCount = 0;

  if (!active && silenceCount >= SILENCE_FRAMES_FOR_OFF) {
    if (currentNote != -1) {
      sendMidiNoteOff((uint8_t)currentNote, 64);
      currentNote = -1;
    }
    pendingNote = -1;
    stableCount = 0;
    updateDisplay(false, -1, power);
    return;
  }

  if (active) {
    processGoertzel(samples, BLOCK_SIZE);

    int bestIdx = -1;
    double bestMag = 0.0;
    double secondMag = 0.0;
    for (int i = 0; i < NUM_NOTES; ++i) {
      double m = gStates[i].magnitude;
      if (m > bestMag) { secondMag = bestMag; bestMag = m; bestIdx = i; }
      else if (m > secondMag) secondMag = m;
    }

    int detectedMidi = -1;
    if (bestIdx >= 0) {
      detectedMidi = MIDI_MIN + bestIdx;
      if (bestMag < POWER_THRESHOLD * 1e-3) detectedMidi = -1;
      else if ((secondMag > 0) && (bestMag / (secondMag + 1e-9) < NOTE_DETECTION_RATIO)) detectedMidi = -1;
    }

    if (detectedMidi != pendingNote) {
      pendingNote = detectedMidi;
      stableCount = 1;
    } else stableCount++;

    if (pendingNote != -1 && stableCount >= STABLE_FRAMES_TO_CONFIRM) {
      if (currentNote == -1) {
        sendMidiNoteOn((uint8_t)pendingNote, 100);
        currentNote = pendingNote;
      } else if (currentNote != pendingNote) {
        sendMidiNoteOff((uint8_t)currentNote, 64);
        delay(2);
        sendMidiNoteOn((uint8_t)pendingNote, 100);
        currentNote = pendingNote;
      }
    }
    int showNote = (pendingNote != -1) ? pendingNote : currentNote;
    updateDisplay(true, showNote, power);
  }
}