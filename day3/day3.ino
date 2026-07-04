#include <LedControl.h>

// Pins for MAX7219 (DIN, CLK, CS, 1 Device)
LedControl lc = LedControl(12, 11, 10, 1);

// Pins for Joystick
const int X_PIN = A0;
const int Y_PIN = A1;
const int SW_PIN = 2;

// --- MENU SYSTEM STATES ---
// 1 = Tic-Tac-Toe, 2 = Smiley Faces, 3 = Pong vs Bot, 4 = Snake, 5 = Whack-A-Pixel
int currentGame = 1; 

// --- BUTTON HOLD SYSTEM VARIABLES ---
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;
const unsigned long SWITCH_HOLD_TIME = 5000; // 5 seconds to switch games

// --- GENERAL GAME LOGIC ---
bool gameOver = false;
int winner = 0;
unsigned long lastMoveTime = 0;
unsigned long lastBlinkTime = 0;
bool cursorState = true;

// --- 1. TIC-TAC-TOE VARIABLES ---
int board[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
int cursorX = 1, cursorY = 1;
int currentPlayer = 1; // 1 = Cross, 2 = Circle

// --- 2. FACES VARIABLES & ART ---
int currentFace = 0;
int totalFaces = 5;
byte faces[5][8] = {
  { B00111100, B01000010, B10100101, B10000001, B10100101, B10011001, B01000010, B00111100 }, // Happy
  { B00111100, B01000010, B11100111, B10000001, B10011001, B10100101, B01000010, B00111100 }, // Angry
  { B00111100, B01000010, B10100101, B10000001, B10011001, B10100101, B01000010, B00111100 }, // Sad
  { B00111100, B01100110, B11100111, B10000001, B10111101, B10100101, B01000010, B00111100 }, // Crying
  { B00000000, B00000000, B01100110, B00000000, B00000000, B01111110, B00000000, B00000000 }  // Sleep
};

// --- 3. PONG VARIABLES ---
int playerPaddleX = 3; // Bottom paddle (Row 7)
int botPaddleX = 3;    // Top paddle (Row 0)
int ballX = 4, ballY = 4;
int ballDirX = 1, ballDirY = -1; // -1 Up, 1 Down
unsigned long lastBallUpdate = 0;
int pongSpeed = 250;

// --- 4. SNAKE VARIABLES ---
int snakeX[30], snakeY[30]; // Arrays to track tail positions
int snakeLength = 3;
int snakeDirX = 1, snakeDirY = 0; // Starting direction: Right
int appleX = 2, appleY = 2;
unsigned long lastSnakeUpdate = 0;

// --- 5. WHACK-A-PIXEL VARIABLES ---
int targetX = 3, targetY = 3;
int playerX = 4, playerY = 4;
unsigned long targetSpawnTime = 0;
const unsigned long TARGET_TIMEOUT = 2000; // 2 seconds to hit it

// --- GLOBAL TEXTURE ART ---
byte bigCross[8]  = { B10000001, B01000010, B00100100, B00011000, B00011000, B00100100, B01000010, B10000001 };
byte bigCircle[8] = { B00111100, B01000010, B10000001, B10000001, B10000001, B10000001, B01000010, B00111100 };
byte tieScreen[8] = { B11100001, B10000010, B11100100, B00001000, B00010000, B00100111, B01000100, B10000111 };

void setup() {
  lc.shutdown(0, false);
  lc.setIntensity(0, 4);
  lc.clearDisplay(0);
  pinMode(SW_PIN, INPUT_PULLUP);
  randomSeed(analogRead(A5)); // Randomizer seeding
  resetGame(currentGame);
}

void loop() {
  checkButtonSystem();

  if (currentGame == 1)      runTicTacToe();
  else if (currentGame == 2) runSmileyFace();
  else if (currentGame == 3) runPong();
  else if (currentGame == 4) runSnake();
  else if (currentGame == 5) runWhackAPixel();
}

// --- GLOBAL NAVIGATION SYSTEM (5-SECOND SWITCHER) ---
void checkButtonSystem() {
  int swState = digitalRead(SW_PIN);

  if (swState == LOW) {
    if (!buttonWasPressed) {
      buttonPressStartTime = millis();
      buttonWasPressed = true;
    }

    if (millis() - buttonPressStartTime >= SWITCH_HOLD_TIME) {
      buttonWasPressed = false;
      lc.clearDisplay(0);
      
      // Cycle menus 1 -> 2 -> 3 -> 4 -> 5 -> Back to 1
      currentGame++;
      if (currentGame > 5) currentGame = 1;
      
      resetGame(currentGame);
      delay(1000); 
    }
  } else {
    if (buttonWasPressed) {
      unsigned long holdDuration = millis() - buttonPressStartTime;
      buttonWasPressed = false;

      if (holdDuration < SWITCH_HOLD_TIME && !gameOver) {
        processGameClick();
      }
    }
  }
}

void processGameClick() {
  if (currentGame == 1 && board[cursorY][cursorX] == 0) {
    board[cursorY][cursorX] = currentPlayer;
    checkTicTacToeWin();
    currentPlayer = (currentPlayer == 1) ? 2 : 1;
  }
  else if (currentGame == 5) { // Whack a Pixel Click check
    if (playerX == targetX && playerY == targetY) {
      spawnWhackTarget(); // Hit successfully! Spawn new target
    }
  }
}

void resetGame(int gameNum) {
  gameOver = false;
  winner = 0;
  lc.clearDisplay(0);

  if (gameNum == 1) { // Reset TicTacToe
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) board[r][c] = 0;
    currentPlayer = 1; cursorX = 1; cursorY = 1;
  }
  else if (gameNum == 2) { // Reset Faces
    currentFace = 0;
  }
  else if (gameNum == 3) { // Reset Pong
    ballX = 4; ballY = 4; ballDirY = -1; pongSpeed = 250;
    playerPaddleX = 3; botPaddleX = 3;
  }
  else if (gameNum == 4) { // Reset Snake
    snakeLength = 3;
    snakeDirX = 1; snakeDirY = 0;
    for(int i=0; i<snakeLength; i++) { snakeX[i] = 3 - i; snakeY[i] = 4; }
    spawnApple();
  }
  else if (gameNum == 5) { // Reset Whack-a-pixel
    playerX = 4; playerY = 4;
    spawnWhackTarget();
  }
}

// --- GAME 1: TIC-TAC-TOE ---
void runTicTacToe() {
  if (!gameOver) {
    int xVal = analogRead(X_PIN); int yVal = analogRead(Y_PIN);
    if (millis() - lastMoveTime > 250) {
      if (xVal < 200 && cursorX > 0) { cursorX--; lastMoveTime = millis(); }
      if (xVal > 800 && cursorX < 2) { cursorX++; lastMoveTime = millis(); }
      if (yVal < 200 && cursorY > 0) { cursorY--; lastMoveTime = millis(); }
      if (yVal > 800 && cursorY < 2) { cursorY++; lastMoveTime = millis(); }
    }
    lc.clearDisplay(0);
    // Draw Grid
    lc.setRow(0, 2, B11111111); lc.setRow(0, 5, B11111111);
    for (int r = 0; r < 8; r++) { lc.setLed(0, r, 2, true); lc.setLed(0, r, 5, true); }
    // Draw Pieces
    for (int r = 0; r < 3; r++) {
      for (int c = 0; c < 3; c++) {
        if (board[r][c] == 1) { lc.setLed(0, r*3, c*3, true); lc.setLed(0, r*3+1, c*3+1, true); }
        else if (board[r][c] == 2) { lc.setLed(0, r*3, c*c*3, true); lc.setLed(0, r*3, c*3+1, true); lc.setLed(0, r*3+1, c*3, true); lc.setLed(0, r*3+1, c*3+1, true); }
      }
    }
    // Blink Cursor
    if (millis() - lastBlinkTime > 150) { cursorState = !cursorState; lastBlinkTime = millis(); }
    if (cursorState) lc.setLed(0, cursorY * 3, cursorX * 3, true);
  } else {
    lc.clearDisplay(0);
    if (winner == 1) for (int i=0; i<8; i++) lc.setRow(0, i, bigCross[i]);
    else if (winner == 2) for (int i=0; i<8; i++) lc.setRow(0, i, bigCircle[i]);
    else if (winner == 3) for (int i=0; i<8; i++) lc.setRow(0, i, tieScreen[7 - i]);
    delay(3000); resetGame(1);
  }
}

void checkTicTacToeWin() {
  for (int i = 0; i < 3; i++) {
    if (board[i][0] != 0 && board[i][0] == board[i][1] && board[i][1] == board[i][2]) { winner = board[i][0]; gameOver = true; return; }
    if (board[0][i] != 0 && board[0][i] == board[1][i] && board[1][i] == board[2][i]) { winner = board[0][i]; gameOver = true; return; }
  }
  if (board[0][0] != 0 && board[0][0] == board[1][1] && board[1][1] == board[2][2]) { winner = board[0][0]; gameOver = true; return; }
  if (board[0][2] != 0 && board[0][2] == board[1][1] && board[1][1] == board[2][0]) { winner = board[0][2]; gameOver = true; return; }
  bool empty = false;
  for (int r=0; r<3; r++) for(int c=0; c<3; c++) if(board[r][c] == 0) empty = true;
  if (!empty) { winner = 3; gameOver = true; }
}

// --- GAME 2: SMILEY FACES CONTROLLER ---
void runSmileyFace() {
  int xVal = analogRead(X_PIN);
  static unsigned long lastFaceShift = 0;

  if (millis() - lastFaceShift > 300) {
    if (xVal < 200) { currentFace--; if (currentFace < 0) currentFace = totalFaces - 1; lastFaceShift = millis(); }
    else if (xVal > 800) { currentFace++; if (currentFace > totalFaces - 1) currentFace = 0; lastFaceShift = millis(); }
  }
  for (int i = 0; i < 8; i++) lc.setRow(0, i, faces[currentFace][i]);
}

// --- GAME 3: PONG (VS INTELLIGENT BOT) ---
void runPong() {
  if (!gameOver) {
    int xVal = analogRead(X_PIN);
    // Move Player Paddle (Row 7)
    if (xVal < 200 && playerPaddleX > 0) playerPaddleX--;
    if (xVal > 800 && playerPaddleX < 6) playerPaddleX++;

    // Bot AI Logic (Row 0): Tracks ball position
    if (ballX < botPaddleX && botPaddleX > 0) botPaddleX--;
    if (ballX > botPaddleX + 1 && botPaddleX < 6) botPaddleX++;

    // Move Ball Physics
    if (millis() - lastBallUpdate > pongSpeed) {
      ballX += ballDirX; ballY += ballDirY;

      // Side Wall Bounces
      if (ballX <= 0 || ballX >= 7) ballDirX = -ballDirX;

      // Top Bot Paddle Check
      if (ballY == 1 && (ballX == botPaddleX || ballX == botPaddleX + 1)) {
        ballDirY = 1; if(pongSpeed > 80) pongSpeed -= 10;
      }
      // Bottom Player Paddle Check
      else if (ballY == 6 && (ballX == playerPaddleX || ballX == playerPaddleX + 1)) {
        ballDirY = -1; if(pongSpeed > 80) pongSpeed -= 10;
      }
      // Out of bounds (Score detection)
      else if (ballY < 0 || ballY > 7) { gameOver = true; }
      lastBallUpdate = millis();
    }

    // Render Assets
    lc.clearDisplay(0);
    lc.setLed(0, 7, playerPaddleX, true); lc.setLed(0, 7, playerPaddleX + 1, true); // Player
    lc.setLed(0, 0, botPaddleX, true);    lc.setLed(0, 0, botPaddleX + 1, true);    // Bot
    lc.setLed(0, ballY, ballX, true); // Ball
    delay(60);
  } else {
    // Flash Screen on Game Over
    for(int i=0; i<3; i++) { lc.clearDisplay(0); delay(150); for(int r=0; r<8; r++) lc.setRow(0,r,0xFF); delay(150); }
    resetGame(3);
  }
}

// --- GAME 4: RETRO SNAKE ---
void runSnake() {
  if (!gameOver) {
    int xVal = analogRead(X_PIN); int yVal = analogRead(Y_PIN);
    // Change directions cleanly
    if (xVal < 200 && snakeDirX == 0) { snakeDirX = -1; snakeDirY = 0; }
    if (xVal > 800 && snakeDirX == 0) { snakeDirX = 1; snakeDirY = 0; }
    if (yVal < 200 && snakeDirY == 0) { snakeDirX = 0; snakeDirY = -1; }
    if (yVal > 800 && snakeDirY == 0) { snakeDirX = 0; snakeDirY = 1; }

    if (millis() - lastSnakeUpdate > 300) {
      // Shift entire tail configuration array forward
      for (int i = snakeLength - 1; i > 0; i--) {
        snakeX[i] = snakeX[i - 1]; snakeY[i] = snakeY[i - 1];
      }
      // Step Head Forward
      snakeX[0] += snakeDirX; snakeY[0] += snakeDirY;

      // Border Collisions
      if (snakeX[0] < 0 || snakeX[0] > 7 || snakeY[0] < 0 || snakeY[0] > 7) gameOver = true;

      // Self Tail Collisions
      for (int i = 1; i < snakeLength; i++) {
        if (snakeX[0] == snakeX[i] && snakeY[0] == snakeY[i]) gameOver = true;
      }

      // Check Apple Consumption
      if (snakeX[0] == appleX && snakeY[0] == appleY) {
        if (snakeLength < 30) snakeLength++;
        spawnApple();
      }
      lastSnakeUpdate = millis();
    }

    // Render Matrix Frame
    lc.clearDisplay(0);
    lc.setLed(0, appleY, appleX, true); // Draw Apple
    for (int i = 0; i < snakeLength; i++) lc.setLed(0, snakeY[i], snakeX[i], true); // Draw Snake
    delay(30);
  } else {
    lc.setChar(0, 0, 'F', false); delay(2000); // Fail screen indicator
    resetGame(4);
  }
}

void spawnApple() {
  appleX = random(0, 8); appleY = random(0, 8);
}

// --- GAME 5: WHACK-A-PIXEL ---
void runWhackAPixel() {
  int xVal = analogRead(X_PIN); int yVal = analogRead(Y_PIN);

  // Navigate Player Reticle/Cursor Around
  if (millis() - lastMoveTime > 150) {
    if (xVal < 200 && playerX > 0) { playerX--; lastMoveTime = millis(); }
    if (xVal > 800 && playerX < 7) { playerX++; lastMoveTime = millis(); }
    if (yVal < 200 && playerY > 0) { playerY--; lastMoveTime = millis(); }
    if (yVal > 800 && playerY < 7) { playerY++; lastMoveTime = millis(); }
  }

  // Check if target timed out before player whacked it
  if (millis() - targetSpawnTime > TARGET_TIMEOUT) {
    spawnWhackTarget(); // Moves target somewhere else if you're too slow
  }

  lc.clearDisplay(0);
  lc.setLed(0, targetY, targetX, true); // Target dot

  // Blink Player Cursor Dot
  if (millis() - lastBlinkTime > 100) { cursorState = !cursorState; lastBlinkTime = millis(); }
  if (cursorState) lc.setLed(0, playerY, playerX, true);
  delay(20);
}

void spawnWhackTarget() {
  targetX = random(0, 8); targetY = random(0, 8);
  targetSpawnTime = millis();
}