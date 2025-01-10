#pragma once
#include <Arduino.h>

// Keep a single set of definitions for these:
static const unsigned long RODS_MAX             = 10;
static const unsigned long DROP_DELAY_MIN       = 2000;   // or 1000 if you like
static const unsigned long DROP_DELAY_MAX       = 5000;
static const unsigned long WAIT_BETWEEN_DROPS   = 2000;  // or 1000 if you like
static const unsigned long MEDIUM_DROP_INTERVAL = 3000;

enum GameState {
  INIT,
  IDLE,
  STARTING,
  COUNTDOWN, 
  PLAYING,
  FINISHED
};

enum class Difficulty {
  EASY,
  MEDIUM,
  HARD
};

// Put this above setup(), for instance:
struct RodInfo {
    int magnetPin;
    int sensorPin;
    int ledNumber;
};


// Declare any functions you call in multiple files:
void buttonPressed();
void setupAndStartGame();
void setIDLEState();
bool dropRod(void *argument);
bool startRandomDropTimer(void *argument);
void shuffleRods(RodInfo *list, int elem);

