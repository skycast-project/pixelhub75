#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Отправка больших HTML-страниц: обычный send(200, "text/html", html)
// обрывается на страницах больше ~5 КБ (буфер AsyncWebServer), поэтому
// отдаём тело кусками через chunked-ответ.
void sendHtmlChunked(AsyncWebServerRequest *req, String html);

// Потоковая отдача HTML-файла из LittleFS с подстановками: страница не
// собирается целиком в куче, поэтому не упирается в фрагментацию памяти.
#include <vector>
// Отдаёт уже готовый буфер (например, статический) потоково, без копии в кучу.
// busy сбрасывается, когда ответ дочитан.
void sendBufferChunked(AsyncWebServerRequest *req, const char *data, size_t len, bool *busy);

void sendTemplateChunked(AsyncWebServerRequest *req, const char *path,
                         std::vector<std::pair<String, String>> reps);

// HTML маленькой кнопки «Сохранить» — для страниц, которые собираются сразу в поток.
const char *saveFabHtml();

// Резерв памяти 16 КБ, который освобождается перед сборкой страницы:
// даёт большой непрерывный блок и убирает сбои из-за фрагментации.
void pageMemInit();
void pageMemBegin();
void pageMemEnd();
