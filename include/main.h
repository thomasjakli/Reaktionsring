#pragma once
#include <Arduino.h>

const auto RODS_MAX = 10;
const long DROP_DELAY_MIN = 300;
const long DROP_DELAY_MAX = 5000;
const long WAIT_BETWEEN_DROPS = 2000;

enum GameState {
  INIT,
  IDLE,
  STARTING,
  PLAYING,
  FINISHED,
};

void buttonPressed();
void setupAndStartGame();
void setIDLEState();
bool dropRod(void *argument);
bool startRandomDropTimer(void *argument);

void shuffleArray(int * array, int size);