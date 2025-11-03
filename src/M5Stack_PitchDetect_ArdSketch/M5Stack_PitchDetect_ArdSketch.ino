#include <M5Unified.h>
#include <math.h>

// =========================================
// Configuration
// =========================================
#define SAMPLE_RATE        8000
#define BLOCK_SIZE         256
#define POWER_THRESHOLD    200000.0f
#define NOTE_DETECTION_RATIO 1.5f
#define STABLE_FRAMES_TO_CONFIRM 3
#define SILENCE_FRAMES_FOR_OFF   5

// =========================================
// Globals
// =========================================
int16_t samples[BLOCK_SIZE];
uint8_t currentNote = 0;
uint8_t lastStableNote = 0;
int stableCount = 0;
int silenceCount = 0;
bool noteOn = false;
float lastPower = 0;  // <-- 音量レベル表示用

// Target MIDI note range (A#3..D#5)
const int noteMin = 58;
const int noteMax = 75;
const float noteFreqs[] = {
  233.08, 246.94, 261.63, 277.18, 293.66, 311.13, 329.63, 349.23,
  369.99, 392.00, 415.30, 440.00, 466.16, 493.88, 523.25, 554.37,
  587.33, 622.25
};

// =========================================
// MIDI send (UART2 output)
// =========================================
void sendMidiNoteOn(uint8_t note, uint8_t velocity) {
  Serial2.write(0x90);  // Note On, channel 1
  Serial2.write(note);
  Serial2.write(velocity);
}

void sendMidiNoteOff(uint8_t note, uint8_t velocity) {
  Serial2.write(0x80);  // Note Off, channel 1
  Serial2.write(note);
  Serial2.write(velocity);
}

// =========================================
// Goertzel algorithm
// =========================================
float goertzel(const int16_t *samples, int numSamples, float targetFreq) {
  float s_prev = 0.0;
  float s_prev2 = 0.0;
  float normalizedFreq = targetFreq / SAMPLE_RATE;
  float coeff = 2.0 * cos(2.0 * M_PI * normalizedFreq);

  for (int i = 0; i < numSamples; i++) {
    float s = samples[i] + coeff * s_prev - s_prev2;
    s_prev2 = s_prev;
    s_prev = s;
  }

  float power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
  return power;
}

// =========================================
// Audio capture and pitch detection
// =========================================
void processAudio() {
  if (!M5.Mic.isEnabled()) return;

  bool ok = M5.Mic.record(samples, BLOCK_SIZE, SAMPLE_RATE);
  if (!ok) return;

  int sampleCount = BLOCK_SIZE;

  // Compute total power
  double powerSum = 0;
  for (int i = 0; i < sampleCount; i++) {
    powerSum += (double)samples[i] * samples[i];
  }
  lastPower = powerSum / sampleCount;  // 平均化して画面表示に使用

  if (powerSum < POWER_THRESHOLD) {
    silenceCount++;
    if (noteOn && silenceCount > SILENCE_FRAMES_FOR_OFF) {
      sendMidiNoteOff(lastStableNote, 0x40);
      noteOn = false;
      lastStableNote = 0;
    }
    return;
  }

  silenceCount = 0;

  // Compute Goertzel magnitude for each note
  int bestIndex = -1;
  float bestMag = 0.0, secondBest = 0.0;
  for (int i = 0; i <= noteMax - noteMin; i++) {
    float mag = goertzel(samples, sampleCount, noteFreqs[i]);
    if (mag > bestMag) {
      secondBest = bestMag;
      bestMag = mag;
      bestIndex = i;
    } else if (mag > secondBest) {
      secondBest = mag;
    }
  }

  if (bestIndex < 0 || bestMag < secondBest * NOTE_DETECTION_RATIO) {
    return;
  }

  currentNote = noteMin + bestIndex;
  if (currentNote == lastStableNote) {
    stableCount++;
  } else {
    stableCount = 0;
  }

  if (stableCount >= STABLE_FRAMES_TO_CONFIRM) {
    if (!noteOn) {
      sendMidiNoteOn(currentNote, 0x40);
      noteOn = true;
      lastStableNote = currentNote;
    } else if (currentNote != lastStableNote) {
      sendMidiNoteOff(lastStableNote, 0x40);
      sendMidiNoteOn(currentNote, 0x40);
      lastStableNote = currentNote;
    }
  }
}

// =========================================
// Simple level meter
// =========================================
void drawLevelMeter(float power) {
  // normalize 0.0〜1.0範囲
  float normalized = power / (POWER_THRESHOLD * 5.0f);
  if (normalized > 1.0f) normalized = 1.0f;

  int barWidth = (int)(normalized * M5.Display.width());

  // メーター枠とバーを描画
  M5.Display.fillRect(0, 60, M5.Display.width(), 20, BLACK);
  M5.Display.fillRect(0, 60, barWidth, 20, GREEN);

  // 数値表示
  M5.Display.setCursor(0, 90);
  M5.Display.printf("Level: %.0f\n", power);
}

// =========================================
// Setup / Loop
// =========================================
void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = true;
  M5.begin(cfg);

  M5.Display.setTextSize(2);
  M5.Display.println("Pitch Detector (UART MIDI)");

  // UART2 (Core2 Port C = RX=13, TX=14)
  Serial2.begin(31250, SERIAL_8N1, 13, 14);

  M5.Mic.begin();
}

void loop() {
  M5.update();
  processAudio();

  // Display info
  M5.Display.setCursor(0, 30);
  if (noteOn) {
    M5.Display.printf("Note: %d   \n", lastStableNote);
  } else {
    M5.Display.print("Silence     \n");
  }

  drawLevelMeter(lastPower);
}
