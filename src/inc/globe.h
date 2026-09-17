// Глобус (банк 24, src/geoscape/earth/globe.c; точки и клики — банк 2, globe_ui.c; план —
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
// Тень кадра (банк 25, src/geoscape/earth/globe_sh.c): уровни по строкам -> подкладка суши L (x 0..255)
// и цвет океана O (x 256..511) строк заднего буфера; флаги строк с тенью (1 байт на строку) —
// в возвращённой странице с GLOBE_SH_FLG (PG_NONE — страницы нет). Вызывает globe.c перед
// проходом строк.
#define GLOBE_SH_FLG 0x2F00
uint8_t globe_shadow(uint16_t lon, int16_t lat, uint8_t zoom, uint16_t sunlon, uint8_t ocean) __banked;

// Пары диска [pl, pr] строк зума в записи строк (6 байт на строку) — таблицей банка 25
void globe_rows_pl(uint8_t zoom, uint8_t *dst) __banked;
// sin 16-битного угла (65536 = 360°) в Q14 (банк 2, src/geoscape/earth/globe_ui.c)
int16_t globe_sin(uint16_t a) __banked;

// Предрасчитанные виды с карты (банк 25, src/geoscape/earth/globe_view.c; globe.md §12.5): на зумах 0–2
// вид берётся из GVIEW.PAK готовыми отрезками строк, геометрии в кадре нет.
uint8_t gview_open(uint8_t ep) __banked;                                        // 1 — предрасчёт есть
uint8_t gview_pick(uint8_t z, uint16_t *lon, int16_t *lat, uint16_t *iv) __banked;   // вид сетки
uint8_t gview_load(uint8_t ep, uint8_t z, uint16_t iv) __banked;                // вид -> страница ep
void globe_det(uint8_t fresh, uint8_t z, uint8_t work) __banked;                  // детали глобуса (globe_det.c)
void globe_marks(uint8_t radar) __banked;   // радары (radar) и метки GlobeMarkers поверх глобуса (после globe_draw)
uint8_t globe_project(uint16_t n) __banked; // globe.s: вершины рабочей страницы -> проекции (радары)
uint8_t globe_workpage(void) __banked;      // globe.s: рабочая страница (0 — нет)
void globe_blink(void) __banked;       // мигание меток (раз в 100 мс)
void globe_det_check(void) __banked;   // базы изменились — подписи в заднем буфере устарели: globe_invalidate
void gview_reset(void) __banked;                                                // рёберный рендер затёр
// Привязка углов вида к сетке предрасчёта (scr_geo.c): 1 — углы поправлены
uint8_t globe_snap(uint8_t zoom, uint16_t *lon, int16_t *lat) __banked;

#endif
