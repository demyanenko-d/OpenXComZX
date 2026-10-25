// Ходьба юнита по фазам (bwalk.c, банк 32): состояние шага — общее, вид (bview.c) рисует по нему
// идущего; API шага — bview.h (bv_walk, bv_walk_tick, bv_walk_done, bv_walking).
#ifndef BWALK_H
#define BWALK_H

#include <stdint.h>

// BattleUnit::startWalking / keepWalking: идёт один юнит за раз. Фаза растёт раз в 30 мс
// (Options::battleXcomSpeed); на середине пути позиция юнита уже клетка назначения —
// «рисовальный» переход оригинала, к концу он стоит. По диагонали фаз вдвое больше.
extern uint8_t wk_u;                     // кто идёт (#FF — никто)
extern uint8_t wk_ph, wk_mid, wk_end;    // фаза, середина и конец
extern uint8_t wk_lx, wk_ly, wk_dx, wk_dy, wk_z;   // откуда и куда (этаж тот же)
extern int16_t wk_ox, wk_oy;             // смещение спрайта от клетки, где юнит числится

#endif
