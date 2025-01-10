#include <Arduino.h>
#include <arduino-timer.h>
#include <FastLED.h>

#include "pinAssignments.h"
#include "main.h"

// ---------------------------------------------------------------------------------
//                            USER-CONFIGURABLE CONSTANTS
// ---------------------------------------------------------------------------------
#define NUM_LEDS       250
#define DATA_PIN       12

// LED brightness & power settings
#define LED_BRIGHTNESS 255
#define LED_VOLTAGE    12
#define LED_MAX_MA     2000

// Button
const unsigned long DEBOUNCE_DELAY  = 50;     // 50 ms for debouncing
const unsigned long LONG_PRESS_TIME = 3000;   // 3s hold to start the game

// Rods
#define RODS_MAX             10


static RodInfo rodsOriginal[RODS_MAX] = {
  { OUTPUT_MAGNET_05, INPUT_SENSOR_05, 11 },
  { OUTPUT_MAGNET_04, INPUT_SENSOR_04, 33 },
  { OUTPUT_MAGNET_03, INPUT_SENSOR_03, 56 },
  { OUTPUT_MAGNET_02, INPUT_SENSOR_02, 77 },
  { OUTPUT_MAGNET_01, INPUT_SENSOR_01, 100},
  { OUTPUT_MAGNET_06, INPUT_SENSOR_06, 150 },
  { OUTPUT_MAGNET_07, INPUT_SENSOR_07, 172 },
  { OUTPUT_MAGNET_08, INPUT_SENSOR_08, 193 },
  { OUTPUT_MAGNET_09, INPUT_SENSOR_09, 216 },
  { OUTPUT_MAGNET_10, INPUT_SENSOR_10, 238 }
};

static RodInfo rods[RODS_MAX] = {
  { OUTPUT_MAGNET_05, INPUT_SENSOR_05, 11 },
  { OUTPUT_MAGNET_04, INPUT_SENSOR_04, 33 },
  { OUTPUT_MAGNET_03, INPUT_SENSOR_03, 56 },
  { OUTPUT_MAGNET_02, INPUT_SENSOR_02, 77 },
  { OUTPUT_MAGNET_01, INPUT_SENSOR_01, 100},
  { OUTPUT_MAGNET_06, INPUT_SENSOR_06, 150 },
  { OUTPUT_MAGNET_07, INPUT_SENSOR_07, 172 },
  { OUTPUT_MAGNET_08, INPUT_SENSOR_08, 193 },
  { OUTPUT_MAGNET_09, INPUT_SENSOR_09, 216 },
  { OUTPUT_MAGNET_10, INPUT_SENSOR_10, 238 }
};

// ---------------------------------------------------------------------------------
//                           GLOBALS & SHARED VARIABLES
// ---------------------------------------------------------------------------------
auto timer        = timer_create_default();
CRGB leds[NUM_LEDS];

GameState   currentState   = GameState::INIT;
Difficulty  currentDiff    = Difficulty::EASY;  // default difficulty
int         rodsLeft       = RODS_MAX;

// Button handling
unsigned long lastDebounceTime   = 0;
unsigned long buttonPressStart   = 0;  // when the button was first pressed
bool          buttonHeld         = false;
int           lastButtonState    = HIGH;
int           buttonState        = HIGH;

// At global scope:
bool rodDropped[RODS_MAX] = { false };  // All false initially
bool rodLastSensorState[RODS_MAX] = { false }; // track old sensor states to detect transitions
unsigned long rodMissingSince[RODS_MAX] = {0};

// ---------------------------------------------------------------------------------
//                            FORWARD DECLARATIONS
// ---------------------------------------------------------------------------------
void setIDLEState();
void setupAndStartGame();
void startCountdown();
bool startRandomDropTimer(void *argument = nullptr);
bool dropRod(void *argument = nullptr);

void handleButton();
void updateLEDs();
void showMissingRodEffects();
void showCountdownAnimation();
void showGamePulseColor();
void showGameFinishedBlink();

bool allRodsInPlace();
void setRodLedColor(int rodIndex, CRGB color);
void shuffleArray(int *list, int elem);
CRGB getDifficultyColor();

// ---------------------------------------------------------------------------------
//                                    SETUP
// ---------------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  Serial.println("INIT"); 

  FastLED.setMaxPowerInVoltsAndMilliamps(LED_VOLTAGE, LED_MAX_MA);
  FastLED.addLeds<WS2815, DATA_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  randomSeed(analogRead(A0));

  // Pin Setup
  pinMode(INPUT_BUTTON_01, INPUT_PULLUP);

  pinMode(INPUT_SENSOR_01, INPUT);
  pinMode(INPUT_SENSOR_02, INPUT);
  pinMode(INPUT_SENSOR_03, INPUT);
  pinMode(INPUT_SENSOR_04, INPUT);
  pinMode(INPUT_SENSOR_05, INPUT);
  pinMode(INPUT_SENSOR_06, INPUT);
  pinMode(INPUT_SENSOR_07, INPUT);
  pinMode(INPUT_SENSOR_08, INPUT);
  pinMode(INPUT_SENSOR_09, INPUT);
  pinMode(INPUT_SENSOR_10, INPUT);

  pinMode(OUTPUT_MAGNET_01, OUTPUT);
  pinMode(OUTPUT_MAGNET_02, OUTPUT);
  pinMode(OUTPUT_MAGNET_03, OUTPUT);
  pinMode(OUTPUT_MAGNET_04, OUTPUT);
  pinMode(OUTPUT_MAGNET_05, OUTPUT);
  pinMode(OUTPUT_MAGNET_06, OUTPUT);
  pinMode(OUTPUT_MAGNET_07, OUTPUT);
  pinMode(OUTPUT_MAGNET_08, OUTPUT);
  pinMode(OUTPUT_MAGNET_09, OUTPUT);
  pinMode(OUTPUT_MAGNET_10, OUTPUT);

  // Initially, set all magnets to HIGH (rod locked)
  
  //for (int i = 0; i < RODS_MAX; i++) {
  //  digitalWrite(allRods[i], HIGH);
  //}

  currentState = GameState::IDLE;
}

// Return true if rod i is *stably* missing
// for at least 100ms. Otherwise, false.
bool isRodMissingStable(int i) {
  // 1) If sensor reads HIGH => "missing" in your code
  bool isMissingNow = (digitalRead(rods[i].sensorPin) == HIGH);
  
  if (isMissingNow) {
    // Start timing if we haven't started yet
    if (rodMissingSince[i] == 0) {
      rodMissingSince[i] = millis();  // begin 100ms countdown
    }
    else {
      // We've been missing for some time, check if > 100ms
      if (millis() - rodMissingSince[i] >= 200) {
        // stable missing
        return true;
      }
    }
  } else {
    // The rod is present => reset the timestamp
    rodMissingSince[i] = 0;
  }

  return false;  // if we haven’t consistently missed for 100ms, false
}

void updateMagnets() {
  for (int i = 0; i < RODS_MAX; i++) {
    // Check if the rod was forcibly dropped:
    bool forciblyDropped = rodDropped[i];

    // Check if the rod is stably missing:
    bool stablyMissing = isRodMissingStable(i);

    // If rod is forcibly dropped OR stably missing => turn magnet off
    if (forciblyDropped || stablyMissing) {
      digitalWrite(rods[i].magnetPin, LOW);
    } else {
      // else we turn magnet on
      digitalWrite(rods[i].magnetPin, HIGH);
    }
  }
}

// ---------------------------------------------------------------------------------
//                                     LOOP
// ---------------------------------------------------------------------------------
void loop() {
  timer.tick();        // let our asynchronous timers run
  handleButton();      // check for short or long press

  // Update the LED strip based on the current state
  updateLEDs();
  FastLED.show();

  updateMagnets();
}

// ---------------------------------------------------------------------------------
//                              BUTTON HANDLING
// ---------------------------------------------------------------------------------
void handleButton() {
  int reading = digitalRead(INPUT_BUTTON_01);

  // Debounce
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        // Button just pressed
        buttonPressStart = millis();
        buttonHeld       = false;  // reset hold-flag
      } else {
        // Button just released
        unsigned long pressDuration = millis() - buttonPressStart;
        if (pressDuration < LONG_PRESS_TIME) {
          // It's a short press
          if (currentState == GameState::IDLE) {
            // Cycle difficulty if all rods in place
            if (allRodsInPlace()) {
              switch (currentDiff) {
                case Difficulty::EASY:   currentDiff = Difficulty::MEDIUM; break;
                case Difficulty::MEDIUM: currentDiff = Difficulty::HARD;   break;
                case Difficulty::HARD:   currentDiff = Difficulty::EASY;   break;
              }
              Serial.println("Short press -> cycle difficulty");
            }
          }
          else if (currentState == GameState::FINISHED) {
            // Return to IDLE state (reset)
            currentState = GameState::INIT;
            setIDLEState();
          }
        }
      }
    }

    // Check for long press while the button is still down
    if (buttonState == LOW && !buttonHeld) {
      unsigned long holdTime = millis() - buttonPressStart;
      if (holdTime >= LONG_PRESS_TIME) {
        buttonHeld = true;

        // Long press action: START THE GAME if in IDLE and rods are in place
        if (currentState == GameState::IDLE && allRodsInPlace()) {
          Serial.println("Long press -> Start game");
          // Start a visual "filling" or set a state that triggers the countdown next
          currentState = GameState::COUNTDOWN;
          // Could also insert a small filler animation here if desired
        }
      }
    }
  }

  lastButtonState = reading;
}

unsigned long getCurrentPressDuration() {
  if (buttonState == LOW) {
    // Still pressed, measure how long
    return millis() - buttonPressStart;
  } else {
    // Not pressed
    return 0;
  }
}

void drawLongPressProgress() {
  // How long has the button been held so far?
  unsigned long holdTime = getCurrentPressDuration();

  // Constrain it not to exceed LONG_PRESS_TIME
  if (holdTime > LONG_PRESS_TIME) {
    holdTime = LONG_PRESS_TIME;
  }

  // Calculate fraction from 0.0 to 1.0
  float fraction = float(holdTime) / float(LONG_PRESS_TIME);

  // Number of LEDs to fill based on fraction
  int fillCount = int(fraction * NUM_LEDS);

  // We'll fill from index 0 up to fillCount in white (or any color you like)
  for (int i = 0; i < fillCount; i++) {
    leds[i] = CRGB::White; 
  }
}

// ---------------------------------------------------------------------------------
//                         LED UPDATING (STATE-BASED)
// ---------------------------------------------------------------------------------
void updateLEDs() {
  // Clear the LED buffer
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  switch (currentState) {

    case GameState::IDLE:
      if (allRodsInPlace()) {
        // Fill with difficulty color first
        fill_solid(leds, NUM_LEDS, getDifficultyColor());

        // If the button is currently pressed, show the progress bar
        if (buttonState == LOW) {
          drawLongPressProgress(); 
        }

      } else {
        // Otherwise show missing rods
        showMissingRodEffects();
      }
      break;


    case GameState::COUNTDOWN:
      // 3, 2, 1 countdown. Usually done over ~3 seconds
      // You can do a finite state machine or a timer-based approach
      showCountdownAnimation();
      break;

    case GameState::PLAYING:
      // Pulsate or do a color chasing effect in the difficulty color
      showGamePulseColor();
      break;

    case GameState::FINISHED:
      // Blink 3 times or some finishing sequence
      showGameFinishedBlink();
      break;

    case GameState::INIT:
      // We arrive here after the game is done or hardware reset
      // Set everything to locked and transition to IDLE
      setIDLEState();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------------
//                                ROD / SENSOR UTILS
// ---------------------------------------------------------------------------------
bool allRodsInPlace() {
  for (int i = 0; i < RODS_MAX; i++) {
    if (digitalRead(rods[i].sensorPin) == HIGH) {
      // If sensor is LOW => rod is not present
      return false;
    }
  }
  return true;
}


void showMissingRodEffects() {
  for (int i = 0; i < RODS_MAX; i++) {
    bool isMissing = (digitalRead(rods[i].sensorPin) == HIGH);
    int center     = rods[i].ledNumber;   // The “middle” LED for this rod

    if (isMissing) {
      // Example: 3-LED breathing effect around center
      uint8_t breathe = beatsin8(50, 10, 255); // adjust speed, min, max
      for (int offset = -1; offset <= 1; offset++) {
        int idx = center + offset;
        if (idx >= 0 && idx < NUM_LEDS) {
          leds[idx] = CRGB(breathe, 0, 0); // red-ish breathing
        }
      }
    } else {
      // Show 3 green LEDs if the rod is present
      for (int offset = -1; offset <= 1; offset++) {
        int idx = center + offset;
        if (idx >= 0 && idx < NUM_LEDS) {
          leds[idx] = CRGB::Green;
        }
      }
    }
  }
}

// Helper to set a specific rod region to a color
void setRodLedColor(int rodIndex, CRGB color) {
  // Example: color 25 LEDs for rod i
  int startIdx = rodIndex * 25;
  int endIdx   = startIdx + 25;
  if (endIdx >= NUM_LEDS) endIdx = NUM_LEDS - 1;
  for (int j = startIdx; j < endIdx; j++) {
    leds[j] = color;
  }
}

// ---------------------------------------------------------------------------------
//                               STATE TRANSITIONS
// ---------------------------------------------------------------------------------
void setIDLEState() {
  // Activate all magnets (ensure rods are locked in place)
  //for (int i = 0; i < RODS_MAX; i++) {
  //  digitalWrite(rods[i].magnetPin, HIGH);
  //}
  for (int i = 0; i < RODS_MAX; i++) {
    rodDropped[i] = false;
  }

  currentState = GameState::IDLE;
}

// Called after countdown finishes
void setupAndStartGame() {
  rodsLeft = RODS_MAX;

  // For each difficulty, define how we drop rods
  switch (currentDiff) {
    case Difficulty::EASY: {

      // Restore the array to a known left->right ordering
      for (int i = 0; i < RODS_MAX; i++) {
        rods[i] = rodsOriginal[i];
      } 
      // “The rods are beginning to fall from left to right by random time intervals.”
      // We can simulate left->right by not shuffling:
      //   allRods[] has a known left->right ordering, so we skip shuffle
      //   but use random intervals each time.
      // Alternatively, if the array is not in left->right order physically,
      // you might skip shuffle or reorder allRods[] to match physical layout.

      rodsLeft = RODS_MAX;
      // Start the timer with the “left->right random interval” logic
      // we’ll drop rods one by one from 0..9
      timer.in(1000, startRandomDropTimer); 
    } break;

    case Difficulty::MEDIUM: {
      // “Random rods at a fixed time interval of 3 seconds.”
      // Shuffle the array so rods fall in random order, but keep the interval fixed
      shuffleRods(rods, RODS_MAX);
      rodsLeft = RODS_MAX;
      // Start first drop in 0.5s, then each subsequent 3s
      timer.in(500, startRandomDropTimer);
    } break;

    case Difficulty::HARD: {
      // “Random rods at random intervals.” 
      shuffleRods(rods, RODS_MAX);
      rodsLeft = RODS_MAX;
      // Start with random intervals
      timer.in(1000, startRandomDropTimer);
    } break;
  }

  currentState = GameState::PLAYING;
}


// ---------------------------------------------------------------------------------
//                                   COUNTDOWN
// ---------------------------------------------------------------------------------
static unsigned long countdownStart    = 0;
static int           countdownSeconds  = 3;

void showCountdownAnimation() {
  // On first entry to COUNTDOWN, record the start time
  if (countdownStart == 0) {
    countdownStart   = millis();
    countdownSeconds = 3;
  }

  unsigned long elapsed = millis() - countdownStart;
  int newCount = 3 - (elapsed / 1000); // each second is a step down
  if (newCount != countdownSeconds) {
    // We changed from 3->2, or 2->1, or 1->0
    countdownSeconds = newCount;
    Serial.print("Countdown: "); Serial.println(countdownSeconds);
  }

  // LED visualization for countdown (simple example: fill from edges to center)
  float fraction = float(elapsed % 1000) / 1000.0f; // fraction of the current second
  int center = NUM_LEDS / 2;
  int half  = map(fraction * 100, 0, 100, 0, center); // grows from 0..center

  // fill from edges inwards (or vice versa)
  for (int i = 0; i < half; i++) {
    leds[center + i] = CRGB::White;
    leds[center - i] = CRGB::White;
  }

  // Once we get to 0, start the game
  if (countdownSeconds <= 0) {
    // reset for next time
    countdownStart = 0;
    currentState   = GameState::PLAYING;
    setupAndStartGame();
  }
}

// ---------------------------------------------------------------------------------
//                          “PLAYING” MODE LIGHTING
// ---------------------------------------------------------------------------------
void showGamePulseColor() {
  // Pulsate in the color of the current difficulty
  CRGB color = getDifficultyColor();
  uint8_t wave = beatsin8(20, 20, 255);  // 10 BPM for pulsing
  CRGB scaled = color;
  scaled.nscale8(wave);
  fill_solid(leds, NUM_LEDS, scaled);
}

// ---------------------------------------------------------------------------------
//                             FINISHED MODE LIGHTING
// ---------------------------------------------------------------------------------
static unsigned long finishBlinkStart = 0;
static int           blinkCount       = 0;

void showGameFinishedBlink() {
  // On first entry
  if (finishBlinkStart == 0) {
    finishBlinkStart = millis();
    blinkCount       = 0;
  }
  unsigned long now = millis();
  unsigned long dt  = now - finishBlinkStart;

  // Blink 3 times in total, each blink = 300ms on, 300ms off => 600ms cycle
  const unsigned long BLINK_INTERVAL = 600;
  int cycleIndex = dt / BLINK_INTERVAL; // how many 600ms cycles so far
  if (cycleIndex > blinkCount) {
    blinkCount = cycleIndex;
  }

  bool ledOn = ( (dt % BLINK_INTERVAL) < (BLINK_INTERVAL / 2) );
  if (ledOn) {
    // Display some color, e.g. white
    fill_solid(leds, NUM_LEDS, CRGB::White);
  } else {
    // Off
    fill_solid(leds, NUM_LEDS, CRGB::Black);
  }

  // After 3 full blinks => 3 cycles
  if (blinkCount >= 3) {
    // Transition back to IDLE
    finishBlinkStart = 0;
    currentState     = GameState::INIT;
  }
}

// ---------------------------------------------------------------------------------
//                               DROP ROD LOGIC
// ---------------------------------------------------------------------------------
bool startRandomDropTimer(void *argument) {
  // Decide how long until the next rod drop
  unsigned long waitTime = 0;

  switch (currentDiff) {
    case Difficulty::EASY:
      // random intervals (within your min/max)
      waitTime = random(DROP_DELAY_MIN, DROP_DELAY_MAX);
      break;
    case Difficulty::MEDIUM:
      // fixed 3s
      waitTime = MEDIUM_DROP_INTERVAL;
      break;
    case Difficulty::HARD:
      // random intervals (within your min/max)
      waitTime = random(DROP_DELAY_MIN, DROP_DELAY_MAX);
      break;
  }

  timer.in(waitTime, dropRod);
  return false; // one-shot timer
}

bool dropRod(void *argument) {
  // rodsLeft is the number of rods *not yet dropped*
  // We drop them in the order in allRods[] 

 
  int indexToDrop = RODS_MAX - rodsLeft;
  Serial.print("DROP ROD: ");
  Serial.println(indexToDrop);
  rodsLeft--;
  
  rodDropped[indexToDrop] = true;

  if (rodsLeft > 0) {
    // schedule next drop
    timer.in(WAIT_BETWEEN_DROPS, startRandomDropTimer);
  } else {
    // No rods left => game ended
    currentState = GameState::FINISHED;
  }

  return false;
}

// ---------------------------------------------------------------------------------
//                              HELPER: SHUFFLE ARRAY
// ---------------------------------------------------------------------------------
void shuffleRods(RodInfo *list, int elem) {
  for (int a = elem - 1; a > 0; a--) {
    int r = random(a + 1);
    if (r != a) {
      // swap entire RodInfo struct
      RodInfo temp  = list[a];
      list[a]       = list[r];
      list[r]       = temp;
    }
  }
}


// ---------------------------------------------------------------------------------
//                       HELPER: GET DIFFICULTY COLOR
// ---------------------------------------------------------------------------------
CRGB getDifficultyColor() {
  switch (currentDiff) {
    case Difficulty::EASY:   return CRGB::Green;
    case Difficulty::MEDIUM: return CRGB::Orange;
    case Difficulty::HARD:   return CRGB::Blue;
  }
  return CRGB::White; // fallback
}
