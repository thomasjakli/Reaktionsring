#include <Arduino.h>
#include <arduino-timer.h>

#include "pinAssignments.h"
#include "main.h"

auto timer = timer_create_default();
auto currentState = GameState::IDLE;
auto rodsLeft = RODS_MAX;

int allRods[] = {
  OUTPUT_ROD_01,
  OUTPUT_ROD_02,
  OUTPUT_ROD_03,
  OUTPUT_ROD_04,
  OUTPUT_ROD_05,
  OUTPUT_ROD_06,
  OUTPUT_ROD_07,
  OUTPUT_ROD_08,
  OUTPUT_ROD_09,
  OUTPUT_ROD_10,
};

void setup() {
  randomSeed(analogRead(0));

  // Pin Setup
  pinMode(INPUT_BUTTON_01, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INPUT_BUTTON_01), buttonPressed, LOW);

  pinMode(OUTPUT_ROD_01, OUTPUT);
  pinMode(OUTPUT_ROD_02, OUTPUT);
  pinMode(OUTPUT_ROD_03, OUTPUT);
  pinMode(OUTPUT_ROD_04, OUTPUT);
  pinMode(OUTPUT_ROD_05, OUTPUT);
  pinMode(OUTPUT_ROD_06, OUTPUT);
  pinMode(OUTPUT_ROD_07, OUTPUT);
  pinMode(OUTPUT_ROD_08, OUTPUT);
  pinMode(OUTPUT_ROD_09, OUTPUT);
  pinMode(OUTPUT_ROD_10, OUTPUT);

  // LED Setup
  // TODO
}

void loop() {
  timer.tick();

  if (currentState == GameState::STARTING)
    setupAndStartGame();

  if (currentState == GameState::INIT)
    setIDLEState();
  
}

void buttonPressed()
{
  if (currentState == GameState::IDLE)
  {
    currentState = GameState::STARTING;
  }
  else if (currentState == GameState::FINISHED)
  {
    currentState = GameState::INIT;
  }
}

void setIDLEState()
{
  // Activate all magnets
  for (int i = 0; i < RODS_MAX; i++)
  {
    digitalWrite(allRods[i], HIGH);
  }
  
  currentState = GameState::IDLE;
}

void setupAndStartGame()
{
  rodsLeft = RODS_MAX;
  shuffleArray(allRods, RODS_MAX);

  currentState = GameState::PLAYING;
  startRandomDropTimer(nullptr);
}

bool startRandomDropTimer(void *argument)
{
  auto waitTime = random(DROP_DELAY_MIN, DROP_DELAY_MAX);
  timer.in(waitTime, dropRod);
  return false;
}

bool dropRod(void *argument)
{
  auto rodIndex = rodsLeft--;
  digitalWrite(allRods[rodIndex], LOW);

  if (rodsLeft > 0)
  {
    timer.in(WAIT_BETWEEN_DROPS, startRandomDropTimer);
  }
  else
  {
    currentState = GameState::FINISHED;
  }

  return false;
}

void shuffleArray(int *list, int elem)
{
  for (int a=elem-1; a>0; a--)
  {
    int r = random(a+1);
    if (r != a)
    {
      int temp = list[a];
      list[a] = list[r];
      list[r] = temp;
    }
  }
}