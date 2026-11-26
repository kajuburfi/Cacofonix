#include <Arduino_RouterBridge.h>
#include <Arduino_LED_Matrix.h>
#include <math.h>
// ===================== MOTOR & ENCODER PINS =====================
#define ENCODER_A 2
#define ENCODER_B 13
#define DRIVER_1  8
#define DRIVER_2  3
#define PWM       5

// ===================== CALIBRATION =====================
// Counts for one full 360-degree rotation of the camera rig
#define COUNTS_PER_REV  2340.0f

// Set to true if CW physically rotates the camera toward 270 deg instead of 90 deg
#define SWAP_MOTOR_DIR  false

// false = bounded 0..360 deg (protects cables from twisting)
// true  = shortest-path wrapping across 360/0 deg (use only with a slip ring)
#define CONTINUOUS_360  true

// Tolerance window around target (25 counts ≈ 3.8° at 2340 CPR)
#define DEADBAND_COUNTS 5

enum MotorState { MOTOR_STOPPED, MOTOR_CW, MOTOR_CCW };
MotorState currentMotorState = MOTOR_STOPPED;
int lastMoveDir = 1; // +1 for CW, -1 for CCW (tracks coasting ticks after stop)

volatile unsigned long encoderCount = 0;
volatile float targetAngle = 0.0f;
float currentAngle = 0.0f;
// float netAngle = 0.0f;
bool autoTrack = true;

// ===================== AUDIO / MATRIX STATE =====================
#define BUF_SIZE 48
Arduino_LED_Matrix matrix;
uint8_t frameBuffer[104];
uint8_t i = 0, counts[] = {BUF_SIZE, 0, 0, 0}, lm = 0;
uint8_t i2 = 0, counts2[] = {BUF_SIZE, 0, 0, 0}, lm2 = 0;
uint64_t buf[] = {0, 0};
uint64_t buf2[] = {0, 0};
uint8_t stepHist[BUF_SIZE] = {0};
uint16_t stepSum = 0;
const int NF = 4;
const uint8_t MAJ  = BUF_SIZE * 3 / 5;
const uint8_t MAJ2 = BUF_SIZE * 1 / 5;
float lastangle = 0;

float sf_random[22] = {1,1,1,1,1.05,1.05,1.05,1.05,0.95,0.95,0.95,0.95,0.9,0.9,0.9,0.9,1.1,1.1,0.8,0.8,0.85,0.85};
// ===================== ENCODER & MOTOR FUNCTIONS =====================
// Exact working ISR: counts raw delta pulses on Encoder A
void readEncoder() {
  encoderCount+=2;
}

// Stop motor using HIGH HIGH (leaves PWM untouched)
void stopMotor() {
  if (currentMotorState != MOTOR_STOPPED) {
    digitalWrite(DRIVER_1, HIGH);
    digitalWrite(DRIVER_2, HIGH);
    currentMotorState = MOTOR_STOPPED;
  }
}

void driveCW() {
  if (currentMotorState != MOTOR_CW) {
    digitalWrite(DRIVER_1, SWAP_MOTOR_DIR ? HIGH : LOW);
    digitalWrite(DRIVER_2, SWAP_MOTOR_DIR ? LOW  : HIGH);
    currentMotorState = MOTOR_CW;
    lastMoveDir = 1;
  }
}

void driveCCW() {
  if (currentMotorState != MOTOR_CCW) {
    digitalWrite(DRIVER_1, SWAP_MOTOR_DIR ? LOW  : HIGH);
    digitalWrite(DRIVER_2, SWAP_MOTOR_DIR ? HIGH : LOW);
    currentMotorState = MOTOR_CCW;
    lastMoveDir = -1;
  }
}

// ===================== AUDIO LOCALIZATION =====================
void micSelect(int *mics) {
  uint8_t w = 0;
  int m = 0;
  for (int j = 0; j < 4; j++) {
    if (mics[j] > m) { m = mics[j]; w = j; }
  }
  if (m >= NF) {
    uint8_t wo = i >> 5;
    uint8_t s = (i & 31) << 1;
    uint8_t ov = (buf[wo] >> s) & 0x03ULL;
    if (counts[ov] > 0) counts[ov]--;
    buf[wo] = (buf[wo] & ~(0x03ULL << s)) | ((uint64_t)w << s);
    counts[w]++;
    i = (i + 1) % (BUF_SIZE);
    if (counts[w] >= MAJ)
      lm = w;

    m = 0;
    uint8_t w1 = w;
    for (int j = 0; j < 4; j++) {
      if (j != w1 && mics[j] > m) { m = mics[j]; w = j; }
    }
    wo = i2 >> 5;
    s = (i2 & 31) << 1;
    ov = (buf2[wo] >> s) & 0x03ULL;
    if (counts2[ov] > 0) counts2[ov]--;
    buf2[wo] = (buf2[wo] & ~(0x03ULL << s)) | ((uint64_t)w << s);
    counts2[w]++;
    i2 = (i2 + 1) % (BUF_SIZE);
    if (counts2[w] >= MAJ2)
      lm2 = w;
    int st = (mics[lm] > 0 && lm != lm2) ? (mics[lm2] * 28) / mics[lm] : 0;
    if (st > 28) st = 28;
    stepSum -= stepHist[i];
    stepHist[i] = st;
    stepSum += st;
  }
}
void renderHorizontalBar(int channelIdx, int level) {
  int startRow = channelIdx * 2;
  float scaledLevel = (level / 56.0) * 13.0;
  int fullyLitCols = (int)scaledLevel;
  int fractionalBrightness = (int)((scaledLevel - fullyLitCols) * 4.0);
  for (int rOffset = 0; rOffset < 2; rOffset++) {
    int currentRow = startRow + rOffset;
    int rowStartIndex = currentRow * 13;
    for (int col = 0; col < 13; col++) {
      uint8_t outputBrightness = 0;
      if (col < fullyLitCols) {
        int maxPossibleGlow = 4 - (col * 2 / 12);
        if (maxPossibleGlow < 1) maxPossibleGlow = 1;
        outputBrightness = maxPossibleGlow;
      } else if (col == fullyLitCols) {
        int maxPossibleGlow = 4 - (col * 2 / 12);
        if (maxPossibleGlow < 1) maxPossibleGlow = 1;
        outputBrightness = (fractionalBrightness * maxPossibleGlow) / 4;
      }
      frameBuffer[rowStartIndex + col] = outputBrightness;
    }
  }
}
float update_matrix_audio(long long mic1, long long mic2, long long mic3, long long mic4) {
  int mics[4] = {
    constrain((int)abs(mic1), 0, 56),
    constrain((int)abs(mic2), 0, 56),
    constrain((int)abs(mic3), 0, 56),
    constrain((int)abs(mic4), 0, 56)
  };
  micSelect(mics);
  int dir = ((lm + 1) % 4 == lm2) ? 1 : (((lm + 3) % 4 == lm2) ? -1 : 0);
  int steps = stepSum / BUF_SIZE;
  float angle = lm * 90.0f + dir * steps * (90.0f / 56.0f);
  if (angle < 0.0f)    angle += 360.0f;
  if (angle >= 360.0f) angle -= 360.0f;
  renderHorizontalBar(0, (int)((90 - fabsf(angle)) / 90.0f * 56.0f) * (angle < 90.0f));
  renderHorizontalBar(1, (int)((90 - fabsf(angle - 90.0f)) / 90.0f * 56.0f) * (angle < 180.0f && angle > 0.0f));
  renderHorizontalBar(2, (int)((90 - fabsf(angle - 180.0f)) / 90.0f * 56.0f) * (angle < 270.0f && angle > 90.0f));
  renderHorizontalBar(3, (int)((90 - fabsf(angle - 270.0f)) / 90.0f * 56.0f) * (angle >= 270.0f));
  matrix.draw(frameBuffer);
  if (lastangle >= 0.0f) {
    float diff = fabsf(angle - lastangle);
    if (diff < 20.0f || (360.0f - diff) < 20.0f) {
      return lastangle;
    }
  }
  lastangle = angle;
  noInterrupts();
  targetAngle = lastangle;
  interrupts();

  return lastangle;
}

// ===================== SETUP & LOOP =====================
void setup() {
  //Serial.begin(115200);
  //Serial.setTimeout(50);
  pinMode(ENCODER_A, INPUT);
  pinMode(ENCODER_B, INPUT);
  pinMode(DRIVER_1, OUTPUT);
  pinMode(DRIVER_2, OUTPUT);
  analogWrite(PWM, 255);//change
  digitalWrite(DRIVER_1, HIGH);
  digitalWrite(DRIVER_2, HIGH);
  currentMotorState = MOTOR_STOPPED;
  attachInterrupt(digitalPinToInterrupt(ENCODER_A), readEncoder, RISING);
  matrix.begin();
  matrix.setGrayscaleBits(3);
  memset(frameBuffer, 4, sizeof(frameBuffer));
  matrix.draw(frameBuffer);
  delay(200);
  matrix.clear();
  memset(frameBuffer, 0, sizeof(frameBuffer));
  Bridge.begin();
  Bridge.provide_safe("update_matrix_audio", update_matrix_audio);
}
void loop() {
  // 1. Consume delta encoder pulses since last loop and update stored currentAngle
  noInterrupts();
  unsigned long deltaPulses = encoderCount;
  encoderCount = 0;
  float target = targetAngle;
  interrupts();
  if (deltaPulses > 0) {
    float movedDeg = ((float)deltaPulses / COUNTS_PER_REV) * 360.0f;
    currentAngle += movedDeg * lastMoveDir;
    if (CONTINUOUS_360) {
      while (currentAngle >= 360.0f) currentAngle -= 360.0f;
      while (currentAngle < 0.0f)    currentAngle += 360.0f;
    }
  }
  // 2. Compute remaining delta from currentAngle -> targetAngle
  if (autoTrack) {
    float deltaAngle = target - currentAngle;
    if (CONTINUOUS_360) {
      if (deltaAngle > 180.0f)  deltaAngle -= 360.0f;
      if (deltaAngle < -180.0f) deltaAngle += 360.0f;
    }
    // deltaAngle *= 0.9;
    long deltaCounts = (long)((deltaAngle / 360.0f) * COUNTS_PER_REV);
    if (labs(deltaCounts) <= DEADBAND_COUNTS) {
      stopMotor();
    } else if (deltaCounts > 50) {
      //analogWrite(PWM, 127);
      if (deltaCounts < 200) { deltaCounts *= sf_random[random(8)]; analogWrite(PWM, 64); }
      else if (deltaCounts < 800) {  deltaCounts *= sf_random[random(12)]; analogWrite(PWM, 128); }
      else { deltaCounts *= sf_random[random(22)]; analogWrite(PWM, 255); }
      driveCW();
    } else if (deltaCounts < -50) {
      //analogWrite(PWM, 127);
      if (deltaCounts > -400) { deltaCounts *= sf_random[random(8)]; analogWrite(PWM, 64); }
      else if (deltaCounts > -800) { deltaCounts *= sf_random[random(12)]; analogWrite(PWM, 128); }
      else { deltaCounts *= sf_random[random(22)]; analogWrite(PWM, 255); }
      driveCCW();
    } else {
      stopMotor();
    }
  }
  delay(10);
}
