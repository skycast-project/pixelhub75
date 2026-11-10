#include "display_manager.h"
#include "pins_config.h"
#include <time.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include "pins.h"
#include "globals.h"
#include "fonts.h"
#include "screen_fx.h"

void showStartupTestPattern() {
  // Три цвета по очереди: красный, зелёный, синий
  uint16_t colors[3] = {
    display->color444(15, 0, 0),
    display->color444(0, 15, 0),
    display->color444(0, 0, 15)
  };

  for (int c = 0; c < 3; c++) {
    display->fillScreen(0);
    // Рамки "наплывают" от края к центру - глубина 0..15 (32/2 - предел
    // по высоте панели), каждая следующая рамка на 1px внутрь предыдущей
    for (int depth = 0; depth < PANEL_H / 2; depth++) {
      display->drawRect(depth, depth, PANEL_W - depth * 2, PANEL_H - depth * 2, colors[c]);
      display->flipDMABuffer();
      delay(40); // было 15 - слишком быстро, почти незаметно
    }
    delay(1200); // было 200 - задержаться на залитом цвете 1-2 сек, чтобы успеть разглядеть
  }

  display->fillScreen(0);
  display->flipDMABuffer();
}

// Максимум точек текста, которые участвуют во взрыве. Логотип рисуется
// крупно (шрифт 5x7 в масштабе x2), в двух строках - горит около 600 точек.
#define MAX_LOGO_PARTICLES 150   // было 900: экономия ~18 КБ RAM

struct LogoParticle {
  float x, y;
  float vx, vy;
  float brightness; // 0..15 - яркость затухания
  uint8_t r, g, b;  // базовый цвет точки на момент взрыва, 0..15
};
static LogoParticle logoParticles[MAX_LOGO_PARTICLES];

// Логотип в две строки: "Pixel" / "Hub75". Классический шрифт 5x7 в
// масштабе x2 (каждая точка шрифта = квадрат 2x2) - буквы крупные, чёткие и
// не сливаются (раньше одна строка с "псевдо-жирным" сдвигом на 1px склеивала
// соседние буквы в пятна). Ширина каждой строки ~58px из 64.
// Рисует и на матрицу, и на временный GFXcanvas1 (для карты точек взрыва) -
// оба наследуют Adafruit_GFX, поэтому одна и та же функция.
static const int LOGO_LINE1_Y = 2;   // <-- ПОДВИНУТЬ: верх первой строки
static const int LOGO_LINE2_Y = 17;  // <-- ПОДВИНУТЬ: верх второй строки

static void drawLogoText(Adafruit_GFX &g, int offsetY, uint16_t color) {
  g.setFont();        // классический шрифт 5x7
  g.setTextSize(2);
  g.setTextColor(color);
  const char *lines[2] = {"Pixel", "Hub75"};
  const int ys[2] = {LOGO_LINE1_Y, LOGO_LINE2_Y};
  for (int i = 0; i < 2; i++) {
    int16_t bx, by;
    uint16_t bw, bh;
    g.getTextBounds(lines[i], 0, 0, &bx, &by, &bw, &bh);
    g.setCursor((PANEL_W - (int)bw) / 2 - bx, ys[i] + offsetY);
    g.print(lines[i]);
  }
  g.setTextSize(1);
}

// ---- Радужная волна по логотипу (снизу вверх) ----
// Цвет каждой точки зависит от строки и времени: узор цветов бежит вверх.
static const float LOGO_WAVE_DEG_PER_ROW = 14.0f;   // <-- сколько градусов оттенка на строку (меньше - шире полосы)
static const float LOGO_WAVE_DEG_PER_MS = 0.18f;    // <-- скорость волны (больше - быстрее)

// Оттенок 0..360 -> r,g,b 0..15 (полная насыщенность и яркость)
static void hueToRgb15(float h, uint8_t &r, uint8_t &g, uint8_t &b) {
  h = fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;
  float x = h / 60.0f;
  int i = (int)x;
  float f = x - i;
  float rr = 0, gg = 0, bb = 0;
  switch (i) {
    case 0: rr = 1;     gg = f;     bb = 0;     break;
    case 1: rr = 1 - f; gg = 1;     bb = 0;     break;
    case 2: rr = 0;     gg = 1;     bb = f;     break;
    case 3: rr = 0;     gg = 1 - f; bb = 1;     break;
    case 4: rr = f;     gg = 0;     bb = 1;     break;
    default: rr = 1;    gg = 0;     bb = 1 - f; break;
  }
  r = (uint8_t)(rr * 15.0f + 0.5f);
  g = (uint8_t)(gg * 15.0f + 0.5f);
  b = (uint8_t)(bb * 15.0f + 0.5f);
}

// Рисует логотип по карте точек (mask) с радужной волной. py - строка ИСХОДНОГО
// положения (по ней считается цвет, чтобы волна шла по буквам, а не по экрану),
// offsetY - смещение при выезде.
static void drawLogoWave(GFXcanvas1 &mask, int offsetY, unsigned long tMs) {
  for (int py = 0; py < PANEL_H; py++) {
    int y = py + offsetY;
    if (y < 0 || y >= PANEL_H) continue;
    uint8_t r, g, b;
    // Чем меньше py (выше по экрану), тем дальше по волне: со временем узор едет вверх
    hueToRgb15(py * LOGO_WAVE_DEG_PER_ROW + tMs * LOGO_WAVE_DEG_PER_MS, r, g, b);
    uint16_t col = display->color444(r, g, b);
    for (int px = 0; px < PANEL_W; px++) {
      if (mask.getPixel(px, py)) display->drawPixel(px, y, col);
    }
  }
}

void showStartupLogoAnimation() {
  static const unsigned long SLIDE_DURATION_MS = 1400;
  static const unsigned long HOLD_DURATION_MS = 3000;    // сколько логотип держится на экране (~3 с)
  static const unsigned long EXPLODE_DURATION_MS = 1800;

  // Карта точек логотипа (монохромный буфер 1 бит/пиксель) - строим один раз,
  // дальше цвет назначается по строке и времени.
  GFXcanvas1 canvas(PANEL_W, PANEL_H);
  canvas.fillScreen(0);
  drawLogoText(canvas, 0, 1);

  unsigned long t0 = millis();   // общее время для волны - непрерывное по всем фазам

  // --- Фаза 1: выезд снизу вверх ---
  unsigned long slideStart = millis();
  unsigned long elapsed = 0;
  while (elapsed <= SLIDE_DURATION_MS) {
    float progress = (float)elapsed / (float)SLIDE_DURATION_MS;
    if (progress > 1.0f) progress = 1.0f;
    int offsetY = (int)((1.0f - progress) * PANEL_H);

    display->fillScreen(0);
    drawLogoWave(canvas, offsetY, millis() - t0);
    display->flipDMABuffer();
    delay(20);
    elapsed = millis() - slideStart;
  }

  // --- Пауза: логотип на месте, волна продолжает бежать ---
  unsigned long holdStart = millis();
  while (millis() - holdStart < HOLD_DURATION_MS) {
    display->fillScreen(0);
    drawLogoWave(canvas, 0, millis() - t0);
    display->flipDMABuffer();
    delay(20);
  }

  // --- Фаза 2: частицы берут цвет точки в момент взрыва ---
  unsigned long tBoom = millis() - t0;
  int particleCount = 0;
  for (int py = 0; py < PANEL_H && particleCount < MAX_LOGO_PARTICLES; py++) {
    uint8_t r, g, b;
    hueToRgb15(py * LOGO_WAVE_DEG_PER_ROW + tBoom * LOGO_WAVE_DEG_PER_MS, r, g, b);
    for (int px = 0; px < PANEL_W && particleCount < MAX_LOGO_PARTICLES; px++) {
      if (canvas.getPixel(px, py)) {
        LogoParticle &p = logoParticles[particleCount];
        p.x = px;
        p.y = py;
        float angle = random(0, 360) * (PI / 180.0f);
        float speed = 0.15f + (random(0, 45) / 100.0f);
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed;
        p.brightness = 15.0f;
        p.r = r; p.g = g; p.b = b;
        particleCount++;
      }
    }
  }

  // --- Фаза 3: разлёт + затухание (цвет сохраняется, гаснет яркость) ---
  unsigned long explodeStart = millis();
  while (millis() - explodeStart < EXPLODE_DURATION_MS) {
    display->fillScreen(0);
    bool anyVisible = false;
    for (int i = 0; i < particleCount; i++) {
      LogoParticle &p = logoParticles[i];
      if (p.brightness <= 0) continue;
      p.x += p.vx;
      p.y += p.vy;
      p.brightness -= 0.2f;
      int drawX = (int)p.x;
      int drawY = (int)p.y;
      if (p.brightness > 0 && drawX >= 0 && drawX < PANEL_W && drawY >= 0 && drawY < PANEL_H) {
        float k = p.brightness / 15.0f;
        display->drawPixel(drawX, drawY, display->color444((uint8_t)(p.r * k), (uint8_t)(p.g * k), (uint8_t)(p.b * k)));
        anyVisible = true;
      }
    }
    display->flipDMABuffer();
    delay(20);
    if (!anyVisible) break;
  }

  display->fillScreen(0);
  display->flipDMABuffer();
  display->setFont();
}

// Графическая анимация "идёт подключение" - пульсирующие концентрические
// кольца (радар), БЕЗ единого слова текста. Рисует РОВНО ОДИН кадр за
// вызов (не блокирует) - вызывается многократно из main.cpp в цикле
// ожидания WiFi.begin(), пока не подключимся или не истечёт таймаут.
// Полностью управляется через millis(), поэтому не нужно хранить
// состояние между вызовами - каждый кадр вычисляется заново.
void updateWifiConnectingAnimation() {
  static const int RING_COUNT = 3;
  static const int MAX_RADIUS = 15; // чуть меньше половины высоты панели (32/2=16)
  static const unsigned long CYCLE_MS = 1200; // время одного кольца от центра до края

  int cx = PANEL_W / 2;
  int cy = PANEL_H / 2;
  unsigned long t = millis();

  display->fillScreen(0);
  for (int i = 0; i < RING_COUNT; i++) {
    // Кольца идут одно за другим со сдвигом по фазе - непрерывный
    // "радар", а не одно кольцо с паузами
    unsigned long phaseOffset = (CYCLE_MS / RING_COUNT) * i;
    unsigned long tt = (t + phaseOffset) % CYCLE_MS;
    float progress = (float)tt / (float)CYCLE_MS; // 0..1: от центра к краю
    int radius = (int)(progress * MAX_RADIUS);
    float fade = 1.0f - progress; // ярче у центра, гаснет к краю
    uint8_t b = (uint8_t)(15.0f * fade);
    if (b > 0 && radius > 0) {
      display->drawCircle(cx, cy, radius, display->color444(0, b, b));
    }
  }
  // Яркая точка в центре - "источник" сигнала, не гаснет
  display->drawPixel(cx, cy, display->color444(0, 15, 15));
  display->flipDMABuffer();
}

void showWifiResultScreen(bool connected, const String &ipStr) {
  display->fillScreen(0);

  if (connected) {
    // IP показывается один раз - радугой в main.cpp. Здесь ничего не рисуем.
    (void)ipStr;
  } else {
    // Графический крестик вместо текста "Нет сети" - тот же
    // безтекстовый стиль, что и у радар-анимации подключения
    int cx = PANEL_W / 2;
    int cy = PANEL_H / 2;
    int r = 8;
    uint16_t red = display->color444(15, 0, 0);
    display->drawLine(cx - r, cy - r, cx + r, cy + r, red);
    display->drawLine(cx - r, cy + r, cx + r, cy - r, red);
  }

  display->flipDMABuffer();
  if (!connected) delay(1500);   // крестик держим, при успехе сразу к радужному IP
}

// Одна строка классического шрифта 5x7, по центру матрицы. y - верх строки.
// Рисует на любую поверхность Adafruit_GFX: на саму матрицу или на временный
// монохромный буфер (карту точек имени для радужной волны).
static void drawCenteredLineOn(Adafruit_GFX &g, const String &text, int y, uint16_t color) {
  int16_t bx, by;
  uint16_t bw, bh;
  g.getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
  g.setTextColor(color);
  g.setCursor((PANEL_W - (int)bw) / 2 - bx, y);
  g.print(text);
}

// Экран режима настройки Wi-Fi (точка доступа поднята): весь экран - текст
// по-английски, классическим шрифтом 5x7 (читается лучше мелких шрифтов), 4 строки:
//   No WiFi!        (красный)
//   Connect to      (белый)
//   PixelHub75      (РАДУЖНАЯ ВОЛНА - как на стартовой заставке, имя точки, 1-я часть)
//   -Setup          (РАДУЖНАЯ ВОЛНА - 2-я часть)
// Имя точки берётся из apSsid и делится на строки по 10 символов (10 букв
// шрифта 5x7 - это ровно 60 пикселей из 64), перенос стараемся делать перед
// дефисом. Цвета имени переливаются той же волной снизу вверх, что и логотип
// при старте (настройки волны - LOGO_WAVE_* выше), а служебный текст остаётся
// обычным - так название отделено от надписи "No WiFi!".
//
// Волна движется, поэтому нужны повторные вызовы: showApModeScreen() рисует первый
// кадр, а updateApModeScreen() надо вызывать из loop() (~каждую итерацию, сама
// ограничивает частоту кадров). Без вызова updateApModeScreen() экран просто
// останется статичным, с цветами первого кадра.
static GFXcanvas1 *s_apMask = nullptr;   // карта точек имени точки доступа (для волны)

static void drawApFrame(unsigned long tMs) {
  display->fillScreen(0);
  display->setFont();        // классический шрифт 5x7
  display->setTextSize(1);
  drawCenteredLineOn(*display, "No WiFi!", 0, display->color444(15, 0, 0));
  drawCenteredLineOn(*display, "Connect to", 8, display->color444(15, 15, 15));
  if (s_apMask) drawLogoWave(*s_apMask, 0, tMs);   // имя точки - радужной волной
  display->flipDMABuffer();
}

void showApModeScreen(const String &apSsid) {
  // Имя точки доступа -> максимум 2 строки по 10 символов
  String l1, l2;
  if (apSsid.length() <= 10) {
    l1 = apSsid;
  } else {
    int cut = 10;
    int dash = apSsid.lastIndexOf('-', 10);   // последний дефис не дальше 10-го символа
    if (dash > 0) cut = dash;                 // переносим ПЕРЕД дефисом
    l1 = apSsid.substring(0, cut);
    l2 = apSsid.substring(cut);
    if (l2.length() > 10) l2 = l2.substring(0, 10);
  }

  if (!s_apMask) s_apMask = new GFXcanvas1(PANEL_W, PANEL_H);
  if (s_apMask) {
    s_apMask->fillScreen(0);
    s_apMask->setFont();
    s_apMask->setTextSize(1);
    drawCenteredLineOn(*s_apMask, l1, 16, 1);
    if (l2.length()) drawCenteredLineOn(*s_apMask, l2, 24, 1);
  }
  drawApFrame(millis());
}

// Вызывать из loop() пока поднята точка доступа - обновляет кадр волны.
void updateApModeScreen() {
  static unsigned long lastFrame = 0;
  unsigned long now = millis();
  if (now - lastFrame < 40) return;   // ~25 кадров в секунду
  lastFrame = now;
  drawApFrame(now);
}

bool initDisplay() {
  // Пины берём из NVS (их можно менять в веб-интерфейсе /settings/pins),
  // а если там пусто — из config_pins.h.
  MatrixPins mp = pinsLoad();
  Serial.printf("Пины матрицы: R1=%d G1=%d B1=%d R2=%d G2=%d B2=%d A=%d B=%d C=%d D=%d E=%d LAT=%d OE=%d CLK=%d\n",
                mp.r1, mp.g1, mp.b1, mp.r2, mp.g2, mp.b2, mp.a, mp.b, mp.c, mp.d, mp.e, mp.lat, mp.oe, mp.clk);
  HUB75_I2S_CFG::i2s_pins _pins = {
    (int8_t)mp.r1, (int8_t)mp.g1, (int8_t)mp.b1,
    (int8_t)mp.r2, (int8_t)mp.g2, (int8_t)mp.b2,
    (int8_t)mp.a, (int8_t)mp.b, (int8_t)mp.c, (int8_t)mp.d, (int8_t)mp.e,
    (int8_t)mp.lat, (int8_t)mp.oe, (int8_t)mp.clk
  };
  HUB75_I2S_CFG mxconfig(PANEL_W, PANEL_H, 1, _pins);
  mxconfig.clkphase = false;
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
  mxconfig.latch_blanking = 4;
  mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
  mxconfig.min_refresh_rate = 150;
  mxconfig.double_buff = true;
  display = new FxPanel(mxconfig);   // дочерний класс: умеет перехватывать рисование для эффектов переходов (screen_fx.*)
  if (!display->begin()) return false;
  display->setBrightness8(displayBrightness);
  display->fillScreen(0);
  return true;
}

bool initTime() {
  configTime(timezoneOffset * 3600, 0, "pool.ntp.org");
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 8 * 3600 && attempts < 20) {
    delay(500);
    now = time(nullptr);
    attempts++;
  }
  return (now > 8 * 3600);
}

void updateTime() {
  time_t now = time(nullptr);
  struct tm *p_tm = localtime(&now);
  if (p_tm) {
    hours = p_tm->tm_hour;
    minutes = p_tm->tm_min;
    seconds = p_tm->tm_sec;
    dayNum = p_tm->tm_mday;
    monthNum = p_tm->tm_mon;
    wdayNum = p_tm->tm_wday;
    year = p_tm->tm_year + 1900;
  }
}

void applyAutoBrightness() {
  if (!autoBrightnessEnabled || display == nullptr) return;

  int nowMinutes = hours * 60 + minutes;
  int sunriseMinutes = sunriseHour * 60 + sunriseMinute;
  int sunsetMinutes = sunsetHour * 60 + sunsetMinute;

  bool isDay = (nowMinutes >= sunriseMinutes && nowMinutes < sunsetMinutes);
  display->setBrightness8(isDay ? displayBrightness : nightBrightness);
}



