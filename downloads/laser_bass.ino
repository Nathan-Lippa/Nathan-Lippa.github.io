//  LASER BASS for Arduino and electronics hardware
//  Output: USB MIDI via Serial at 31250 baud (hardware midi baud rate)
//
//  PITCH BEHAVIOUR (fretless bass):
//    - Pot at rest (0) -> open string pitch (pitch bend center)
//    - Pot at maximum  -> +FRET_RANGE semitones above open (two octaves)
//    - Pitch bend updates continuously while laser is blocked !! (can slide up frets)
//    - One MIDI channel per string means independent pitch bend
//    - Pitch bend range message sent on startup
//
#define BLOCKED_STATE HIGH //not sure why but it works

// Label pins (const int for READ ONLY):
const int LASER_PINS[4] = { 2, 3, 4, 5 };
const int POT_PINS[4] = { A0, A1, A2, A3 };

// MIDI Configuration for open bass tuning:
//    E2=40, A2=45, D3=50, G3=55
const uint8_t BASE_NOTES[4] = { 40, 45, 50, 55 };
const uint8_t MIDI_CHANNELS[4] = { 2, 3, 4, 5 };
const uint8_t VELOCITY = 64;  // arbitrary lol

// Semitone range of the membrane pot from bottom to top.
//    24 = two octaves.
const uint8_t FRET_RANGE = 24;

// Laser jitter prevention
const unsigned long LASERJIT_MS = 6;

// Pitch bend jitter prevention
//    (minimum pitch bend change before new message)
const int PB_THRESHOLD = 30;  // out of 16383 (14-bit RPN)

// More variables
bool laserBlocked[4] = { false, false, false, false };
bool lastRaw[4] = { false, false, false, false };
unsigned long rawChangeTime[4] = { 0, 0, 0, 0 }; // 32-bit non-negative
bool noteActive[4] = { false, false, false, false };
int lastPitchBend[4] = { 8192, 8192, 8192, 8192 };  // "open"


//  MIDI SEND FUNCTIONS
//  All MIDI is sent Serial at 31250 baud (for now).

void midiNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  Serial.write(0x90 | (ch - 1));
  Serial.write(note);
  Serial.write(vel);
}

void midiNoteOff(uint8_t ch, uint8_t note) {
  Serial.write(0x80 | (ch - 1));
  Serial.write(note);
  Serial.write(0);
}

// value: 0–16383  (8192 "open")
void midiPitchBend(uint8_t ch, int value) {
  Serial.write(0xE0 | (ch - 1));
  Serial.write(value & 0x7F);         // LSB (lower 7 bits)
  Serial.write((value >> 7) & 0x7F);  // MSB (upper 7 bits)
}

void midiCC(uint8_t ch, uint8_t cc, uint8_t val) {
  Serial.write(0xB0 | (ch - 1));
  Serial.write(cc);
  Serial.write(val);
}

// Sends MIDI RPN to set pitch bend sensitivity in semitones.
void setPitchBendRange(uint8_t ch, uint8_t semitones) {
  midiCC(ch, 101, 0);        // RPN MSB = 0
  midiCC(ch, 100, 0);        // RPN LSB = 0 -> Pitch Bend Sensitivity
  midiCC(ch, 6, semitones);  // Data Entry: semitone range
  midiCC(ch, 38, 0);         // Data Entry LSB: 0 cents
  midiCC(ch, 101, 127);      // Null RPN
  midiCC(ch, 100, 127);
}

//  PITCH BEND MAPPING
int potToPitchBend(int potVal) {
  //  Pot reads 0–1023 (ADC). Maps to 8192–16383 (pitch bend).
  //      8192  = "open" (no lower half!)
  //      16383 = FRET_RANGE semitones above open (two octaves)
  return map(potVal, 0, 1023, 8192, 16383);
}

//  SETUP
void setup() {
  Serial.begin(31250);  // MIDI baud rate

  for (int i = 0; i < 4; i++) {
    pinMode(LASER_PINS[i], INPUT_PULLUP);
  }

  // Wait before sending init messages
  delay(300);

  // Initialise each MIDI channel: set pitch bend range and center it
  for (int i = 0; i < 4; i++) {
    setPitchBendRange(MIDI_CHANNELS[i], FRET_RANGE);
    midiPitchBend(MIDI_CHANNELS[i], 8192);
  }
}


//  MAIN LOOP
void loop() {
  unsigned long now = millis();

  for (int i = 0; i < 4; i++) {

    // Read laser and check for jitter
    bool raw = (digitalRead(LASER_PINS[i]) == BLOCKED_STATE);

    if (raw != lastRaw[i]) {
      lastRaw[i] = raw;
      rawChangeTime[i] = now;
    }

    // Only change after input has been stable for LASERJIT_MS
    if ((now - rawChangeTime[i] >= LASERJIT_MS) && (raw != laserBlocked[i])) {
      laserBlocked[i] = raw; 

      if (laserBlocked[i]) {
        // Beam broken -> trigger note
        // Set pitch bend BEFORE Note ON so no transient
        int pb = potToPitchBend(analogRead(POT_PINS[i]));
        midiPitchBend(MIDI_CHANNELS[i], pb);
        midiNoteOn(MIDI_CHANNELS[i], BASE_NOTES[i], VELOCITY);
        noteActive[i] = true;
        lastPitchBend[i] = pb;

      } else {
        // Beam restored -> release note
        if (noteActive[i]) {
          midiNoteOff(MIDI_CHANNELS[i], BASE_NOTES[i]);
          noteActive[i] = false;
        }
      }
    }

    // Continuous pitch bend while note is held
    if (noteActive[i]) {
      int pb = potToPitchBend(analogRead(POT_PINS[i]));
      if (abs(pb - lastPitchBend[i]) > PB_THRESHOLD) {
        midiPitchBend(MIDI_CHANNELS[i], pb);
        lastPitchBend[i] = pb;
      }
    }
  }
}