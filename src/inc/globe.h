// Глобус (банк 24, src/ui/globe.c; точки и клики — банк 2, globe_ui.c; план —
// project_docs/globe.md, метод М4). Окно
// 256x200 в (0, 0), центр (128, 100); вид — ctx.globe_lon/lat и ST->zoom (0..5).
// Рисует в задний буфер (строки экрана 280..479) и копирует на экран; перерисовка —
// только когда вид сменился, иначе — копия. Точки и клики — настоящая ортографическая
// проекция OpenXcom (Globe::polarToCart / cartToPolar).
#ifndef GLOBE_H_INC
#define GLOBE_H_INC

#include <stdint.h>
#include "state.h"

#define GLOBE_CX   128
#define GLOBE_CY   100
#define GLOBE_W    256
#define GLOBE_H    200
#define GLOBE_ZOOMS  6

// Нарисовать глобус (вид сменился — рендер, иначе копия заднего буфера). Вызывать при
// подключённом ST (экран геоскейпа); Win3 восстанавливается.
void globe_draw(void) __banked;
// Точка (углы geo_t) -> пиксель окна глобуса; 0 — на задней стороне или вне окна.
// Win3 не трогает: p может указывать в ST.
uint8_t globe_xy(const geo_t *p, int16_t *x, int16_t *y) __banked;
// Пиксель окна -> точка (за краем диска — ближайшая точка края). Win3 не трогает.
void globe_lonlat(int16_t x, int16_t y, geo_t *p) __banked;
// Забыть нарисованное (палитра, фон окна): следующий globe_draw рисует заново.
void globe_invalidate(void) __banked;
// Долгота подсолнечной точки (16-битный угол) по времени ST: OpenXcom GameTime::getDaylight,
// Globe::getSunDirection без сезонов — λs = 90° − 360° · ((час + 18) % 24 … ) / сутки.
// «Эпоха» (λs >> 7, 0.7°) сменилась — глобус перерисовывается (тень). Банк 25.
uint16_t globe_sunlon(void) __banked;
// Тень кадра (банк 25, src/ui/globe_sh.c): уровни по строкам -> подкладка суши L (x 0..255)
// и цвет океана O (x 256..511) строк заднего буфера; флаги строк с тенью (1 байт на строку) —
// в возвращённой странице с GLOBE_SH_FLG (PG_NONE — страницы нет). Вызывает globe.c перед
// проходом строк.
#define GLOBE_SH_FLG 0x2F00
uint8_t globe_shadow(uint16_t lon, int16_t lat, uint8_t zoom, uint16_t sunlon, uint8_t ocean) __banked;
// sin 16-битного угла (65536 = 360°) в Q14 (банк 2, src/ui/globe_ui.c)
int16_t globe_sin(uint16_t a) __banked;

#endif
