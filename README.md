# M5Stack Audio Pitch Detector

This project captures audio from the built-in microphone of M5Stack Core2, estimates the pitch (monophonic) and sends corresponding MIDI Note On/Off messages via the M5 Unit MIDI (or a MIDI UART fallback). The pitch detection operates only within MIDI note numbers 58..75; inputs outside this range are not expected.

Features
- Monophonic pitch detection (single note assumed)
- Goertzel filter bank-based pitch estimation (fast and suitable for fixed note set)
- MIDI output via Unit MIDI or UART (31250 baud) fallback
- On-screen status display: sound activity, detected MIDI note, and signal power
- Note On/Off behavior:
  - Send Note On when a pitch is detected and stable
  - Send Note Off when sound stops (silence)
  - If pitch changes while sound continues, send Note Off for the previous note and Note On for the new note

Repository contents
- src\M5Stack_PitchDetect_ArdSketch\M5Stack_PitchDetect_ArdSketch.ino — main Arduino sketch (example)
- README.md — this file (English)
- README.ja.md — Japanese README

Requirements
- M5Stack Core2 (ESP32-based)
- M5 Unit MIDI (preferred) or external MIDI connection using UART2 TX pin
- Arduino IDE
- Libraries:
  - M5Unified (includes M5GFX) — https://github.com/m5stack/M5Unified
  - Arduino core for ESP32 (matching your board)

Notes about hardware and ADC
- The sketch uses I2S ADC built-in mode (ESP32 ADC via I2S). On some Core2 revisions, the internal microphone or ADC channel mapping may differ. You might need to adjust the ADC channel in i2s_set_adc_mode(...) and/or I2S configuration depending on your hardware revision.
- MIDI Unit support in M5Unified may expose a Stream-like interface. If the Unit API differs in your M5Unified version the sketch falls back to UART2 (Serial2 at 31,250 bps). Adjust pins/connection if necessary.

Quick start (Arduino IDE)
1. Install the ESP32 Arduino core and configure your board for M5Stack Core2.
2. Install M5Unified library (follow its README).
3. Open the sketch file `M5Stack_PitchDetect_ArdSketch.ino`.
4. Select the correct board and COM port.
5. Build and upload to your M5Core2.
6. Connect M5 Unit MIDI or wire TX (GPIO17) to your MIDI device input (through proper MIDI DIN interface and isolation).
7. Play a single monophonic pitch between MIDI notes 58..75 and observe the screen / MIDI output.

Configuration & Tuning
- SAMPLE_RATE and BLOCK_SIZE: tradeoff between latency and frequency resolution. Default: 8 kHz, 256 samples.
- POWER_THRESHOLD: adjust to match your microphone sensitivity and environment. If too high, note detection may not trigger; if too low, noise may trigger false notes.
- NOTE_DETECTION_RATIO: increases robustness to harmonics by requiring the chosen bin to be sufficiently stronger than the next-best bin.
- STABLE_FRAMES_TO_CONFIRM and SILENCE_FRAMES_FOR_OFF: adjust for debounce and note-off latency.

Troubleshooting
- No audio detected / never shows active:
  - Verify ADC channel and I2S ADC mapping for your Core2 revision.
  - Lower POWER_THRESHOLD to make detection more sensitive.
- Wrong or unstable pitch:
  - Increase BLOCK_SIZE (improves frequency resolution) or increase STABLE_FRAMES_TO_CONFIRM.
  - Check microphone wiring or hardware revision differences.
- No MIDI output:
  - If using Unit MIDI, verify the Unit is attached and accessible via M5.Units.getUnit("midi").
  - If using UART fallback, ensure Serial2 pins match your wiring and MIDI DIN interface is correct.
