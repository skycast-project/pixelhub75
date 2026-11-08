#include "wind.h"
#include <math.h>
#include "globals.h"
#include "icons.h"
#include "icon_boost.h"

// ============================================================
// 16-точечный компас. Секторы по 22.5°, центрированные на каждом
// направлении (т.е. N занимает 348.75°..11.25°, и т.д.)
// ============================================================
String windDirectionFromDegrees(int deg) {
  deg = ((deg % 360) + 360) % 360; // нормализуем на всякий случай

  // 8 направлений (по 45°) - в бегущей строке и на экранах короткие
  // привычные названия: северный, северо-восточный, юго-восточный и т.д.
  static const char* names[8] = {
    "Северный", "Северо-Восточный", "Восточный", "Юго-Восточный",
    "Южный", "Юго-Западный", "Западный", "Северо-Западный"
  };

  // сдвигаем на пол-сектора, чтобы 0° попадал в середину сектора "Северный"
  int index = (int)floor((deg + 22.5) / 45.0);
  index = index % 8;
  return String(names[index]);
}

// ============================================================
// Маппинг направления на иконку (или пару иконок для мигания)
// ============================================================
WindIconPlan getWindIconPlan(const String &dir) {
  if (dir == "Северный")         return {WindIcon::N,  WindIcon::N,  false};
  if (dir == "Южный")            return {WindIcon::S,  WindIcon::S,  false};
  if (dir == "Западный")         return {WindIcon::W,  WindIcon::W,  false};
  if (dir == "Восточный")        return {WindIcon::E,  WindIcon::E,  false};
  if (dir == "Северо-Западный")  return {WindIcon::NW, WindIcon::NW, false};
  if (dir == "Северо-Восточный") return {WindIcon::NE, WindIcon::NE, false};
  if (dir == "Юго-Западный")     return {WindIcon::SW, WindIcon::SW, false};
  if (dir == "Юго-Восточный")    return {WindIcon::SE, WindIcon::SE, false};

  if (dir == "Западный-Юго-Западный")      return {WindIcon::W, WindIcon::SW, true};
  if (dir == "Западный-Северо-Западный")   return {WindIcon::W, WindIcon::NW, true};
  if (dir == "Восточный-Северо-Восточный") return {WindIcon::E, WindIcon::NE, true};
  if (dir == "Восточный-Юго-Восточный")    return {WindIcon::E, WindIcon::SE, true};
  if (dir == "Юго-Юго-Западный")           return {WindIcon::S, WindIcon::SW, true};
  if (dir == "Юго-Юго-Восточный")          return {WindIcon::S, WindIcon::SE, true};
  if (dir == "Северо-Северо-Западный")     return {WindIcon::N, WindIcon::NW, true};
  if (dir == "Северо-Северо-Восточный")    return {WindIcon::N, WindIcon::NE, true};

  return {WindIcon::NONE, WindIcon::NONE, false};
}

// Выбор реального 9x9 битмапа по WindIcon.
// ВАЖНО: сохраняем перепутанный маппинг имён из исходного проекта -
// image_data_W физически рисуется для "Восточный", image_data_E - для
// "Западный" (см. подробный комментарий в icons.h).
static const uint16_t* bitmapForWindIcon(WindIcon icon) {
  switch (icon) {
    case WindIcon::N:  return image_data_N;
    case WindIcon::S:  return image_data_S;
    case WindIcon::E:  return image_data_W; // да, так и должно быть - см. комментарий выше
    case WindIcon::W:  return image_data_E; // да, так и должно быть - см. комментарий выше
    case WindIcon::NE: return image_data_NE;
    case WindIcon::NW: return image_data_NW;
    case WindIcon::SE: return image_data_SE;
    case WindIcon::SW: return image_data_SW;
    default:           return image_data_NO;
  }
}

// Рисует иконку компаса ветра (9x9) в (cx,cy) - центр иконки.
// Для промежуточных направлений мигает между двумя соседними иконками
// раз в 500мс через millis() - БЕЗ delay(), не блокирует loop().
void drawWindCompassIcon(int cx, int cy, const String &dirRus) {
  WindIconPlan plan = getWindIconPlan(dirRus);

  if (plan.primary == WindIcon::NONE) {
    // штиль / не распознано - рисуем отдельную иконку "безветренно"
    drawRGBBitmapBoost(cx - 4, cy - 4, image_data_NO, 9, 9);
    return;
  }

  WindIcon toDraw = plan.primary;
  if (plan.blink) {
    static unsigned long lastToggle = 0;
    static bool showSecond = false;
    unsigned long now = millis();
    if (now - lastToggle >= 500) {
      lastToggle = now;
      showSecond = !showSecond;
    }
    toDraw = showSecond ? plan.secondary : plan.primary;
  }

  drawRGBBitmapBoost(cx - 4, cy - 4, bitmapForWindIcon(toDraw), 9, 9);
}
