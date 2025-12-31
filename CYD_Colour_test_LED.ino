#include <stdint.h>

// Put struct before includes to avoid Arduino auto-prototype issues
struct Slider {
  const char* label;
  int x, y, w, h;
  uint8_t value;
};

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_arduino_version.h>

/* ========== TOUCH: match your MAIN PROJECT exactly ========== */
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_SCLK 25

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// Use your known-good raw ranges (from your main sketch)
#define RX_MIN  150
#define RX_MAX  3600
#define RY_MIN  300
#define RY_MAX  3900
/* ============================================================ */

/* ========== CYD RGB LED pins (active-LOW) ========== */
#define CYD_LED_RED   4
#define CYD_LED_GREEN 16
#define CYD_LED_BLUE  17
/* ================================================== */

TFT_eSPI tft;

// ESP32 core v3+ LEDC detection
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
  #define ESP32_CORE_V3PLUS 1
#else
  #define ESP32_CORE_V3PLUS 0
#endif

static const uint16_t LED_PWM_FREQ = 5000;
static const uint8_t  LED_PWM_BITS = 8; // 0..255

#if !ESP32_CORE_V3PLUS
static const int LED_CH_R = 4;
static const int LED_CH_G = 5;
static const int LED_CH_B = 6;
#endif

static inline uint8_t clampU8(int v) { return (v < 0) ? 0 : (v > 255 ? 255 : (uint8_t)v); }

static void initLedPwm() {
  pinMode(CYD_LED_RED, OUTPUT);
  pinMode(CYD_LED_GREEN, OUTPUT);
  pinMode(CYD_LED_BLUE, OUTPUT);

#if ESP32_CORE_V3PLUS
  ledcAttach(CYD_LED_RED,   LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttach(CYD_LED_GREEN, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttach(CYD_LED_BLUE,  LED_PWM_FREQ, LED_PWM_BITS);
#else
  ledcSetup(LED_CH_R, LED_PWM_FREQ, LED_PWM_BITS);
  ledcSetup(LED_CH_G, LED_PWM_FREQ, LED_PWM_BITS);
  ledcSetup(LED_CH_B, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(CYD_LED_RED,   LED_CH_R);
  ledcAttachPin(CYD_LED_GREEN, LED_CH_G);
  ledcAttachPin(CYD_LED_BLUE,  LED_CH_B);
#endif
}

// brightness 0..255 where 255 = full ON (we invert for active-LOW LED)
static void setStatusLedRgb(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t dr = 255 - r;
  uint8_t dg = 255 - g;
  uint8_t db = 255 - b;
#if ESP32_CORE_V3PLUS
  ledcWrite(CYD_LED_RED,   dr);
  ledcWrite(CYD_LED_GREEN, dg);
  ledcWrite(CYD_LED_BLUE,  db);
#else
  ledcWrite(LED_CH_R, dr);
  ledcWrite(LED_CH_G, dg);
  ledcWrite(LED_CH_B, db);
#endif
}

/* ===================== UI ===================== */
static Slider sR = {"R", 40,  55, 240, 18, 0};
static Slider sG = {"G", 40, 105, 240, 18, 0};
static Slider sB = {"B", 40, 155, 240, 18, 0};

static const int KNOB_W = 10;
static const int KNOB_H = 26;
static const int BTN_Y  = 205;
static const int BTN_H  = 28;

static bool hit(int x,int y,int rx,int ry,int rw,int rh){
  return (x>=rx && x<rx+rw && y>=ry && y<ry+rh);
}

static uint8_t sliderValueFromTouch(const Slider& s, int tx) {
  int rel = tx - s.x;
  if (rel < 0) rel = 0;
  if (rel > s.w) rel = s.w;
  return (uint8_t)((rel * 255) / s.w);
}

static void drawSlider(const Slider& s, uint16_t trackCol, uint16_t knobCol) {
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, s.y - 2);
  tft.print(s.label);

  tft.fillRoundRect(s.x, s.y, s.w, s.h, 4, TFT_DARKGREY);
  tft.drawRoundRect(s.x, s.y, s.w, s.h, 4, trackCol);

  int kx = s.x + (s.value * s.w) / 255 - (KNOB_W / 2);
  if (kx < s.x) kx = s.x;
  if (kx > s.x + s.w - KNOB_W) kx = s.x + s.w - KNOB_W;

  int ky = s.y + (s.h / 2) - (KNOB_H / 2);
  tft.fillRoundRect(kx, ky, KNOB_W, KNOB_H, 3, knobCol);
  tft.drawRoundRect(kx, ky, KNOB_W, KNOB_H, 3, TFT_WHITE);

  tft.setCursor(s.x + s.w + 10, s.y - 2);
  tft.printf("%3u", s.value);
}

static void drawButtons() {
  tft.setTextFont(2);

  tft.fillRoundRect(10, BTN_Y, 90, BTN_H, 6, TFT_BLUE);
  tft.drawRoundRect(10, BTN_Y, 90, BTN_H, 6, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setCursor(26, BTN_Y + 7);
  tft.print("PRINT");

  tft.fillRoundRect(110, BTN_Y, 90, BTN_H, 6, TFT_DARKGREY);
  tft.drawRoundRect(110, BTN_Y, 90, BTN_H, 6, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(145, BTN_Y + 7);
  tft.print("OFF");

  tft.fillRoundRect(210, BTN_Y, 90, BTN_H, 6, TFT_DARKGREY);
  tft.drawRoundRect(210, BTN_Y, 90, BTN_H, 6, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(236, BTN_Y + 7);
  tft.print("FULL");
}

static void drawPreview() {
  uint16_t c = tft.color565(sR.value, sG.value, sB.value);
  tft.fillRoundRect(10, 240, 90, 28, 6, c);
  tft.drawRoundRect(10, 240, 90, 28, 6, TFT_WHITE);

  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(110, 248);
  tft.printf("LED: R=%u G=%u B=%u", sR.value, sG.value, sB.value);
}

static void redrawAll() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 8);
  tft.print("CYD RGB LED Tuner");

  tft.setTextFont(1);
  tft.setCursor(10, 28);
  tft.print("Drag sliders. PRINT copies RGB to Serial.");

  drawSlider(sR, TFT_RED, TFT_RED);
  drawSlider(sG, TFT_GREEN, TFT_GREEN);
  drawSlider(sB, TFT_CYAN, TFT_CYAN);

  drawButtons();
  drawPreview();
}

static void applyLed() {
  setStatusLedRgb(sR.value, sG.value, sB.value);
}

static void printValues() {
  Serial.println();
  Serial.printf("setStatusLedRgb(%u, %u, %u);\n", sR.value, sG.value, sB.value);
}

static bool readTouchXY(int16_t &sx, int16_t &sy) {
  if (!ts.touched()) return false;

  TS_Point p = ts.getPoint();

  // Map raw -> screen (rotation 1)
  int x = map((int)p.x, RX_MIN, RX_MAX, 0, tft.width() - 1);
  int y = map((int)p.y, RY_MIN, RY_MAX, 0, tft.height() - 1);

  if (x < 0) x = 0; if (x >= tft.width())  x = tft.width() - 1;
  if (y < 0) y = 0; if (y >= tft.height()) y = tft.height() - 1;

  sx = (int16_t)x;
  sy = (int16_t)y;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  tft.init();
  tft.setRotation(1);

  // Touch init EXACTLY like your working project
  touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  initLedPwm();

  // Start OFF
  sR.value = 0; sG.value = 0; sB.value = 0;
  applyLed();

  redrawAll();
}

void loop() {
  int16_t tx, ty;
  if (!readTouchXY(tx, ty)) { delay(10); return; }

  bool changed = false;

  auto handleSlider = [&](Slider &s) {
    int hitY = s.y - 12;
    int hitH = s.h + 24;
    if (hit(tx, ty, s.x, hitY, s.w, hitH)) {
      s.value = sliderValueFromTouch(s, tx);
      changed = true;
    }
  };

  handleSlider(sR);
  handleSlider(sG);
  handleSlider(sB);

  if (hit(tx, ty, 10, BTN_Y, 90, BTN_H)) {
    applyLed();
    printValues();
  } else if (hit(tx, ty, 110, BTN_Y, 90, BTN_H)) {
    sR.value = 0; sG.value = 0; sB.value = 0;
    changed = true;
  } else if (hit(tx, ty, 210, BTN_Y, 90, BTN_H)) {
    sR.value = 255; sG.value = 255; sB.value = 255;
    changed = true;
  }

  if (changed) {
    applyLed();
    redrawAll();
  }

  while (ts.touched()) delay(10);
  delay(40);
}
