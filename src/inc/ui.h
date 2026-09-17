// Интерфейс: стек экранов и виджеты (как State и Interface/* в OpenXcom).
// Экран описывается таблицей (sdef_t + wdef_t[]) в банке экранов; ядро (ui.c,
// банк 1) получает копию описания в Win1 и рисует. Обработчики экранов — в их
// банках, вызываются через scr_event() по номеру экрана (без указателей на функции).
// Правила рисования виджетов — project_docs/08_ui_port_plan.md §6.
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "text.h"

// Типы виджетов
#define W_WINDOW  1     // окно: рамка + фон экрана (Window)
#define W_TEXT    2     // текст (Text)
#define W_BUTTON  3     // кнопка с текстом (TextButton)
#define W_LIST    4     // список строк (TextList), строки даёт scr_text()
#define W_FRAME   5     // рамка (Frame): кольца цвета color, внутри color2
#define W_TOGGLE  6     // переключатель (ToggleTextButton / группа TextButton):
                        // act — группа (0 — одиночный), arg — значение для событий
#define W_FILL    7     // заливка: цвет элемента или arg при el = #FF
#define W_IMAGE   8     // картинка str (номер ресурса) в ту же позицию экрана
#define W_CUSTOM  9     // рисует экран (EVT_DRAW), клик — действие виджета
#define W_HOTSPOT 10    // невидимая область (кнопки поверх картинки)
#define W_EDIT    11    // поле ввода (TextEdit): текст ui_edit + курсор, arg — длина;
                        // пока оно на экране, буквы/цифры/пробел идут в поле,
                        // CS+0 — удалить, ENTER/BREAK — виджетам (OK/отмена)
#define W_BAR     12    // полоса (Bar): значения — scr_text(id, DYN-слот, 0, buf):
                        // u16 value, u16 value2, u16 max в buf[0..5]
#define W_ARROW   13    // кнопка-стрелка 13x14 (ArrowButton ARROW_BIG_UP/DOWN): flags 1 — вниз;
                        // клик — действие act/arg (правая кнопка: ui_arrow_max = 1),
                        // удержание левой — повтор (таймер 250/50 мс, как в OpenXcom)
#define W_COMBO   14    // ComboBox: кнопка с текстом (DYN — выбранный пункт) и стрелкой;
                        // клик — act/arg, экран открывает список combo_open() (screens.c)

// flags виджета: TX_* (0x1F: выравнивание, перенос) и
#define WF_SEL    0x20  // список: строки выбираются (клик -> EVT_LIST)
#define WF_THIN   0x01  // окно: тонкая рамка (Window::setThinBorder, список ComboBox)
#define WF_POPH   0x02  // окно: при открытии растёт по горизонтали (POPUP_HORIZONTAL) —
#define WF_POPV   0x04  // … по вертикали (POPUP_VERTICAL); оба — BOTH (gfx_popup)
#define WF_GEO    0x40  // кнопка панели геоскейпа (шрифты GEO, setGeoscapeButton)
#define WF_RSEL   0x40  // список: и правая кнопка -> EVT_LIST; W_CUSTOM: и правая кнопка -> EVT_BUTTON
                        //   (ui_arrow_max = 1)
#define WF_BIG    0x80  // крупный шрифт (setBig)

// Текст виджета: номер строки STR_* или динамический (scr_text), номер слота
#define DYN(n)    (0x8000u | (n))
#define NOSTR     0xFFFFu

// Действия виджета (и запросы ui_req_op)
#define A_NONE    0
#define A_POP     1     // закрыть экран
#define A_PUSH    2     // открыть экран arg поверх
#define A_SET     3     // заменить весь стек экраном arg
#define A_CUSTOM  4     // scr_event(экран, EVT_BUTTON, arg)
#define A_POP_PUSH 5    // закрыть и открыть arg
#define A_POP2    6     // закрыть два экрана
#define A_REDRAW  7     // перерисовать всё (описание экрана запрашивается заново)
#define A_DYN     8     // перерисовать DYN-виджеты верхнего экрана
#define A_POPN    9     // закрыть arg экранов

typedef struct {
	uint8_t type;
	int16_t x, y, w, h;
	uint8_t el;          // элемент interfaces.rul (UI_EL_*), #FF — нет
	uint16_t str;        // STR_* или DYN(n); NOSTR — нет; W_IMAGE — номер ресурса
	uint8_t flags;
	uint8_t act, arg;
	uint8_t key;         // клавиша: KEY_ESC / KEY_ENTER / символ / 0
} wdef_t;

// flags экрана
#define SF_POPUP  0x01  // рисуется поверх предыдущего (иначе стек под ним не виден)
#define SF_ALTPAL 0x02  // блок BACKPALS из color2 элемента palette (alterPal)
#define SF_RAWPAL 0x04  // палитра — ресурс palui без BACKPALS (статьи Уфопедии)

// Явные цвета виджетов (статьи Уфопедии задают цвета числами): элемент EL_RAW + i
// берёт color/color2 из ui_raw[i] (заполняет экран в *_get).
#define EL_RAW    0xF0
extern uint8_t ui_raw[16][2];

#define SDEF_MAXW 48

typedef struct {
	uint8_t ui;          // категория interfaces.rul (UI_SCR_*): цвета элементов
	uint8_t palui;       // категория для палитры (#FF — та же)
	uint16_t bg;         // фон окна (RES_*), 0 — нет
	uint8_t flags;
	uint8_t n;           // число виджетов
} sdef_t;

// События экрана (scr_event)
#define EVT_OPEN   1    // экран открыт (до рисования) — подготовить данные
#define EVT_BUTTON 2    // A_CUSTOM или переключатель: arg виджета
#define EVT_LIST   3    // клик по строке списка: номер строки
#define EVT_CLOSE  4
#define EVT_TICK   5    // каждый кадр, пока экран сверху; 1 — перерисовать DYN-тексты
#define EVT_DRAW   6    // нарисовать W_CUSTOM: номер виджета
#define EVT_QUERY  7    // переключатель с arg нажат? (1 — да)
#define EVT_KEY    8    // клавиша без виджета (ввод текста); 1 — перерисовать DYN
#define EVT_ARROW  9    // стрелка у строки списка: arg — строка; ui_arrow_dir +1/-1,
                        // ui_arrow_max 1 — правая кнопка (до предела); 1 — перерисовать DYN

void ui_run(uint8_t first_screen) __banked;       // главный цикл
// Цвет элемента верхнего экрана (для W_CUSTOM): which 0 — color, 1 — color2, 2 — border.
uint8_t ui_color(uint8_t el, uint8_t which) __banked;
uint8_t ui_color_in(uint8_t ui, uint8_t el, uint8_t which) __banked;   // элемент категории ui
// Частичная перерисовка верхнего экрана после текущего события / тика: только
// виджеты DYN-слота (0..63) или одна строка списка. Вместо «вернуть 1» из EVT_*.
void ui_dirty(uint8_t slot) __banked;
void ui_dirty_row(uint8_t slot, uint8_t row) __banked;

// Запрос к ядру из обработчика экрана: выполняется после возврата из обработчика.
extern uint8_t ui_req_op, ui_req_arg;
#define UI_GO(op, a) do { ui_req_op = (op); ui_req_arg = (a); } while (0)

// Открыть экран снаружи (сценарий теста: poke _ui_request <номер>), 0 — нет.
extern uint8_t ui_request;
// Координаты последнего клика (для W_CUSTOM/W_HOTSPOT).
extern int16_t ui_click_x, ui_click_y;
// Текст для экрана ошибки и подобных (Win1).
extern char ui_msg[128];
// Буфер поля ввода W_EDIT (экран заполняет в EVT_OPEN и читает по OK).
#define UI_EDIT_MAX 32
extern char ui_edit[UI_EDIT_MAX];

// --- диспетчер экранов (общий код, src/common/screens.c): по номеру экрана — в банк
uint8_t scr_get(uint8_t id, sdef_t *s, wdef_t *w);                // описание -> Win1; 0 — нет экрана
void scr_text(uint8_t id, uint8_t slot, uint8_t row, char *buf);   // динамический текст/строка списка (buf >= 256)
uint8_t scr_rows(uint8_t id, uint8_t slot);                        // число строк списка
uint8_t scr_event(uint8_t id, uint8_t ev, uint8_t arg);            // 1 — обработано

// Строка списка: колонки через '\t'; в начале может быть #03 и номер цвета — цвет
// всей строки (TextList::setRowColor). Раскладка колонок — scr_text(id, slot, LIST_COLS, buf):
// buf[0] = число колонок (0 — одна на всю ширину), buf[1..n] — ширины, buf[n+1..2n] — TX_*,
// buf[2n+1] — колонка стрелок ± (TextList::setArrowColumn: x от левого края списка, 0 — нет).
#define LIST_COLS 0xFF
#define LC_DOT    0x80  // в TX_* колонки: точки после текста до ширины колонки (TextList::setDot),
                        // если в строке за ней есть ещё колонка; только для выравнивания влево
extern int8_t ui_arrow_dir;
extern uint8_t ui_arrow_max;

#endif
