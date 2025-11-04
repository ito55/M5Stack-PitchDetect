#include <M5Unified.h>
#include <math.h>
#include <Unit_Encoder.h> // Include Unit Encoder library
// Unit Encoder Control:
// push: Resets POWER_THRESHOLD to 1000000.0f
// turn: Adjusts POWER_THRESHOLD (Increment 10000.0f per click)

// =========================================
// Configuration
// =========================================
#define SAMPLE_RATE        8000
#define BLOCK_SIZE         256
// POWER_THRESHOLD is changed to a float variable, adjustable by Unit Encoder
float POWER_THRESHOLD = 10000.0f; // Start at the minimum base threshold
#define NOTE_DETECTION_RATIO 1.2f 
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
float lastPower = 0;

Unit_Encoder sensor; // Declare Unit Encoder object

// New variables for virtual encoder tracking
signed short int last_hardware_value = 0; // Tracks the physical encoder's last reading
signed short int software_position = 0;   // The virtual, resettable position (0 = 10000.0f)

// Status: 0=Silence, 1=Active(No Pitch), 2=Pitch Unstable, 3=Pitch Stable, 4=Note On
int currentStatus = 0;

// Target MIDI note range (A#3..D#5)
const int noteMin = 58;
const int noteMax = 75;
const float noteFreqs[] = {
  233.08, 246.94, 261.63, 277.18, 293.66, 311.13, 329.63, 349.23,
  369.99, 392.00, 415.30, 440.00, 466.16, 493.88, 523.25, 554.37,
  587.33, 622.25
};

// =========================================
// Pitch Name Conversion
// =========================================
const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Converts MIDI note number to a string (e.g., 60 -> "C4")
String getNoteName(uint8_t midiNote) {
  if (midiNote == 0) return "-";
  
  int noteIndex = midiNote % 12; // 0-11 index
  int octave = (midiNote / 12) - 1; // MIDI C0 is 12, so C4 (60) is (60/12)-1 = 4
  
  String name = NOTE_NAMES[noteIndex];
  name += String(octave);
  return name;
}

// =========================================
// MIDI send (UART2 output)
// =========================================
void sendMidiNoteOn(uint8_t note, uint8_t velocity) {
  Serial2.write(0x90); // Note On, channel 1
  Serial2.write(note);
  Serial2.write(velocity);
}

void sendMidiNoteOff(uint8_t note, uint8_t velocity) {
  Serial2.write(0x80); // Note Off, channel 1
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
  lastPower = powerSum / sampleCount;

  if (powerSum < POWER_THRESHOLD) {
    silenceCount++;
    if (noteOn && silenceCount > SILENCE_FRAMES_FOR_OFF) {
      sendMidiNoteOff(lastStableNote, 0x40);
      noteOn = false;
      lastStableNote = 0;
    }
    // Status: Silence
    currentStatus = 0;
    return;
  }

  silenceCount = 0;
  // Status: Power Active (before pitch detection)
  currentStatus = 1;

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
    // Status: Pitch Unstable/Ambiguous
    currentStatus = 2;
    return;
  }

  currentNote = noteMin + bestIndex;
  if (currentNote == lastStableNote) {
    stableCount++;
  } else {
    stableCount = 0;
  }

  // Status: Pitch Stabilizing
  currentStatus = 3;

  if (stableCount >= STABLE_FRAMES_TO_CONFIRM) {
    if (!noteOn) {
      sendMidiNoteOn(currentNote, 0x40);
      noteOn = true;
      lastStableNote = currentNote;
      // Status: Note On (New Note)
      currentStatus = 4;
    } else if (currentNote != lastStableNote) {
      sendMidiNoteOff(lastStableNote, 0x40);
      sendMidiNoteOn(currentNote, 0x40);
      lastStableNote = currentNote;
      // Status: Note On (Note Change)
      currentStatus = 4;
    }
  }
}

// =========================================
// Simple level meter and Display Update
// =========================================
void drawLevelMeter(float power) {
  // --- Level Meter (Y=60 to Y=80) ---
  // Normalize to 0.0 to 1.0 range
  float normalized = power / (POWER_THRESHOLD * 5.0f);
  if (normalized > 1.0f) normalized = 1.0f;

  int barWidth = (int)(normalized * M5.Display.width());
  // Draw meter frame and bar
  M5.Display.fillRect(0, 60, M5.Display.width(), 20, BLACK);
  M5.Display.fillRect(0, 60, barWidth, 20, GREEN);

  // --- Level (Y=90) and Thres (Y=120) ---
  M5.Display.setTextSize(2);
  // Display Level numerical value
  M5.Display.setCursor(0, 90);
  M5.Display.printf("Level: %.0f\n", power);

  // Display adjusted POWER_THRESHOLD
  M5.Display.setCursor(0, 120);
  M5.Display.printf("Thres: %.0f   \n", POWER_THRESHOLD);
  
  // --- Pitch Name (Y=150) ---
  M5.Display.fillRect(0, 150, M5.Display.width(), 40, BLACK);
  M5.Display.setTextSize(4); // Larger text size
  M5.Display.setCursor(0, 150);
  
  // Determine text to display
  String pitchName = "-";
  if (currentStatus == 4) { // Only show stable notes
    pitchName = getNoteName(lastStableNote);
  } else if (currentStatus == 3) {
    pitchName = getNoteName(currentNote) + " (Stab)";
  }

  M5.Display.print(pitchName);
}

// =========================================
// Setup / Loop
// =========================================
void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = true;
  M5.begin(cfg);

  // Initialize Unit Encoder I2C (following Unit_Encoder_M5Unified.ino)
  int ex_sda = M5.getPin(m5::ex_i2c_sda);
  int ex_scl = M5.getPin(m5::ex_i2c_scl);
  if (ex_sda >= 0 && ex_scl >= 0) {
    Wire.begin(ex_sda, ex_scl);
  } else {
    Wire.begin();
  }
  sensor.begin(&Wire);
  
  // Initialize the hardware tracking variable
  last_hardware_value = sensor.getEncoderValue();

  M5.Display.setTextSize(2);
  M5.Display.println("Pitch Detector (UART MIDI)");

  // UART2 (Core2 Port C = RX=13, TX=14)
  Serial2.begin(31250, SERIAL_8N1, 13, 14);

  M5.Mic.begin();
}

void loop() {
  M5.update();

  // Get current hardware value
  signed short int current_hardware_value = sensor.getEncoderValue();
  
  // Calculate delta rotation since last loop
  signed short int delta = current_hardware_value - last_hardware_value;
  
  // Update tracking variable for next loop
  last_hardware_value = current_hardware_value;

  // Calculate POWER_THRESHOLD parameters
  float base_threshold = 10000.0f;
  float increment_per_turn = 5000.0f;


  // Control Unit Encoder LED based on button status
  bool btn_status = sensor.getButtonStatus();
  if (!btn_status) {
      // Button is pressed (Execute reset)
      const float DEFAULT_THRESHOLD = 1000000.0f; 
      
      // Calculate the software position corresponding to the default threshold (198)
      const signed short int RESET_ENCODER_VALUE = 198; 
      
      // 1. Set the virtual position to the reset value (198)
      software_position = RESET_ENCODER_VALUE;
      
      // 2. Set the threshold for immediate use/display
      POWER_THRESHOLD = DEFAULT_THRESHOLD;
      
      // Set LED to Purple (0xC800FF)
      sensor.setLEDColor(0, 0xC800FF);
  } else {
      // Button is not pressed

      // 3. Update the virtual position only if rotation occurred
      if (delta != 0) {
          software_position += delta;
      }
      
      // Apply limits to software position (prevents negative threshold)
      if (software_position < 0) software_position = 0;

      // 4. Calculate POWER_THRESHOLD from the virtual position
      POWER_THRESHOLD = base_threshold + (float)software_position * increment_per_turn;

      // Set LED to dim Green (0x001100)
      sensor.setLEDColor(0, 0x001100);
  }

  processAudio();

  // Display info
  M5.Display.setCursor(0, 30);
  // Status display update (Y=30)
  M5.Display.setTextSize(2);
  switch (currentStatus) {
    case 0:
      M5.Display.print("Silence     \n");
      break;
    case 1:
      M5.Display.print("Active      \n");
      break;
    case 2:
      M5.Display.print("P-Unstable  \n");
      break;
    case 3:
      M5.Display.printf("P-Stable: %d\n", currentNote);
      break;
    case 4:
      M5.Display.printf("Note: %d   \n", lastStableNote);
      break;
    default:
      M5.Display.print("Error       \n");
      break;
  }

  drawLevelMeter(lastPower);
  delay(20);
}