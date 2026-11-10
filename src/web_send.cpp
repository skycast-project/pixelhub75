#include "web_send.h"
#include <LittleFS.h>
#include <memory>
#include <vector>

// Маленькая плавающая кнопка "Сохранить" на каждой странице:
// сама находит форму и отправляет её. Если формы нет — скрывается.
static const char *SAVE_FAB =
    "<style>#saveFab{position:fixed;right:10px;bottom:10px;z-index:9999;"
    "padding:5px 9px;font:600 12px/1.2 system-ui,Arial,sans-serif;border:0;border-radius:7px;"
    "background:#27ae60;color:#fff;box-shadow:0 2px 8px rgba(0,0,0,.45);cursor:pointer;"
    "opacity:.9}#saveFab:active{transform:scale(.95)}</style>"
    "<button id=\"saveFab\" type=\"button\" onclick=\"var f=document.querySelector('form');if(f)f.submit()\">"
    "Сохранить</button>"
    "<script>document.addEventListener('DOMContentLoaded',function(){var b=document.getElementById('saveFab');"
    "if(b&&!document.querySelector('form'))b.style.display='none';});</script>";

const char *saveFabHtml() { return SAVE_FAB; }

// Кусок страницы для конкретного ответа: буфер выделяется на каждый запрос,
// иначе параллельные запросы (браузер открывает несколько соединений)
// затирают друг другу содержимое.
struct PageChunk {
  String data;
  size_t pos;
};

void sendHtmlChunked(AsyncWebServerRequest *req, String html) {
  if (!req) return;

  // Пустая страница = не хватило памяти при сборке. Вместо белого экрана
  // отдаём короткое сообщение с автоперезагрузкой.
  if (html.length() == 0) {
    req->send(200, "text/html; charset=utf-8",
      "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
      "<meta http-equiv='refresh' content='2'>"
      "<title>PixelHub 75</title><link rel='stylesheet' href='/style.css'></head><body>"
      "<div class='wrap'><div class='card'><b>Не хватило памяти на страницу.</b>"
      "<p class='hint'>Страница перезагрузится автоматически через 2 секунды.</p>"
      "</div></div></body></html>");
    return;
  }

  // Кнопку вставляем не склейкой (это лишняя копия страницы в куче),
  // а при отдаче: часть до </body>, кнопка, часть после.
  int pos = html.lastIndexOf("</body>");
  if (pos < 0) pos = html.length();

  const size_t fabLen = strlen(SAVE_FAB);

  struct Stream {
    String page;
    size_t insertAt;
    size_t fabLen;
  };
  auto st = std::make_shared<Stream>();
  st->page = std::move(html);
  st->insertAt = (size_t)pos;
  st->fabLen = fabLen;

  AsyncWebServerResponse *res = req->beginChunkedResponse(
      "text/html; charset=utf-8",
      [st](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        size_t idx = index;
        if (idx < st->insertAt) {
          size_t n = st->insertAt - idx;
          if (n > maxLen) n = maxLen;
          memcpy(buffer, st->page.c_str() + idx, n);
          return n;
        }
        idx -= st->insertAt;
        if (idx < st->fabLen) {
          size_t n = st->fabLen - idx;
          if (n > maxLen) n = maxLen;
          memcpy(buffer, SAVE_FAB + idx, n);
          return n;
        }
        idx -= st->fabLen;
        size_t off = st->insertAt + idx;
        size_t n = st->page.length() - off;
        if (n > maxLen) n = maxLen;
        if (n) memcpy(buffer, st->page.c_str() + off, n);
        return n;
      });
  if (!res) { req->send(500); return; }
  req->send(res);
}

// ------------------------------------------------------------
// Потоковая отдача шаблона: файл читается кусками, подстановки
// применяются на лету. Страница целиком в куче не собирается.
// ------------------------------------------------------------
struct TplState {
  File f;
  std::vector<std::pair<String, String>> reps;
  String pend;        // ещё не отданный хвост файла
  String cur;         // остаток подстановки
  size_t curPos = 0;
  bool eof = false;
  bool fab = false;
  size_t maxKey = 1;
};

static size_t tplFill(TplState *st, uint8_t *buf, size_t maxLen) {
  size_t w = 0;
  while (w < maxLen) {
    if (st->curPos < st->cur.length()) {
      size_t n = st->cur.length() - st->curPos;
      if (n > maxLen - w) n = maxLen - w;
      memcpy(buf + w, st->cur.c_str() + st->curPos, n);
      w += n; st->curPos += n;
      if (st->curPos >= st->cur.length()) { st->cur = ""; st->curPos = 0; }
      continue;
    }

    if (st->pend.length() == 0 && !st->eof) {
      char tmp[512];
      size_t n = st->f.read((uint8_t *)tmp, sizeof(tmp));
      if (n == 0) st->eof = true; else st->pend.concat(tmp, (unsigned)n);
      continue;
    }

    if (st->eof && st->pend.length() == 0) {
      if (!st->fab) { st->cur = SAVE_FAB; st->curPos = 0; st->fab = true; continue; }
      break;
    }

    int best = -1;
    size_t bestLen = 0;
    String *bestVal = nullptr;
    for (auto &r : st->reps) {
      int p = st->pend.indexOf(r.first);
      if (p >= 0 && (best < 0 || p < best)) { best = p; bestLen = (size_t)r.first.length(); bestVal = &r.second; }
    }

    if (best > 0) {
      size_t n = (size_t)best;
      if (n > maxLen - w) n = maxLen - w;
      memcpy(buf + w, st->pend.c_str(), n);
      w += n; st->pend.remove(0, n);
      continue;
    }
    if (best == 0) {
      st->cur = *bestVal; st->curPos = 0;
      st->pend.remove(0, bestLen);
      continue;
    }

    size_t keep = st->eof ? 0 : st->maxKey;
    if (st->pend.length() <= keep) {
      if (!st->eof) {
        char tmp[512];
        size_t n = st->f.read((uint8_t *)tmp, sizeof(tmp));
        if (n == 0) st->eof = true; else st->pend.concat(tmp, (unsigned)n);
        continue;
      }
      size_t n = st->pend.length();
      if (n > maxLen - w) n = maxLen - w;
      memcpy(buf + w, st->pend.c_str(), n);
      w += n; st->pend.remove(0, n);
      continue;
    }

    size_t avail = st->pend.length() - keep;
    if (avail > maxLen - w) avail = maxLen - w;
    memcpy(buf + w, st->pend.c_str(), avail);
    w += avail; st->pend.remove(0, avail);
  }
  return w;
}

void sendTemplateChunked(AsyncWebServerRequest *req, const char *path,
                         std::vector<std::pair<String, String>> reps) {
  if (!req) return;
  auto st = std::make_shared<TplState>();
  st->f = LittleFS.open(path, "r");
  if (!st->f) {
    req->send(404, "text/html; charset=utf-8",
              "<!DOCTYPE html><html><body><h1>Файл не найден</h1></body></html>");
    return;
  }
  st->reps = std::move(reps);
  for (auto &r : st->reps) if (r.first.length() > st->maxKey) st->maxKey = r.first.length();
  AsyncWebServerResponse *res = req->beginChunkedResponse(
      "text/html; charset=utf-8",
      [st](uint8_t *buffer, size_t maxLen, size_t) -> size_t { return tplFill(st.get(), buffer, maxLen); });
  if (res) req->send(res);
}

static void *s_pageReserve = nullptr;

void pageMemInit() {
  // Резерв не нужен: большие страницы отдаются потоково, а маленькие
  // помещаются и без него. Раньше резерв сам провоцировал bad_alloc.
  s_pageReserve = nullptr;
}

void pageMemBegin() {
}

void pageMemEnd() {
  // ничего: резерв больше не выделяем
}

// ------------------------------------------------------------
// Отдача готового буфера без копирования: буфер остаётся валидным,
// пока клиент читает ответ. busy снимается по завершении.
// ------------------------------------------------------------
struct BufState {
  const char *data;
  size_t len;
  bool *busy;
};

void sendBufferChunked(AsyncWebServerRequest *req, const char *data, size_t len, bool *busy) {
  if (!req) return;
  if (!data || len == 0) {
    if (busy) *busy = false;
    req->send(500, "text/plain", "empty");
    return;
  }
  auto st = std::make_shared<BufState>();
  st->data = data;
  st->len = len;
  st->busy = busy;
  AsyncWebServerResponse *res = req->beginChunkedResponse(
      "text/html; charset=utf-8",
      [st](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        if (index >= st->len) {
          if (st->busy) { *st->busy = false; st->busy = nullptr; }
          return 0;
        }
        size_t n = st->len - index;
        if (n > maxLen) n = maxLen;
        memcpy(buffer, st->data + index, n);
        return n;
      });
  if (!res) {
    if (busy) *busy = false;
    req->send(500);
    return;
  }
  req->send(res);
}
