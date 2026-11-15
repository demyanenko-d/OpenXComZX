// Разбор боя (банк 37): первая страница DebriefingState (REF/OpenXcom/src/Battlescape/
// DebriefingState.cpp): окно на фоне BACK01.SCR, заголовок по исходу (:1328-1425), шапка
// «предмет / количество / очки», список ненулевых строк очков с точками (setColumns(3, 224, 30, 64),
// количество и очки — вторым цветом), итог (_lstTotal, сразу под списком: :400-410), оценка по
// итогу (:412-433) и OK. Очки — prepareDebriefing (:1153-1210, 1650-1771): убитый отрядом пришелец
// — его value, оглушённый и подобранный — value · 2, труп и предметы пришельцев — их recoveryPoints
// (только если поле осталось за отрядом), погибший боец — минус value, отряд не вернулся — корабль
// потерян; мирный житель: убит пришельцами (или бой проигран) — минус value, отрядом — минус
// value + 2·(value/3), спасён — value. Чего нет: второй страницы (рост характеристик бойцов),
// частей НЛО на карте, пропавших без вести снаружи корабля, последствий для кампании (гибель солдат, трофеи на базу,
// судьба НЛО и места миссии), очков региону и стране.
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "str_ids.h"
#include "ui_ids.h"
#include "text.h"
#include "state.h"
#include "screens.h"
#include "scrdef.h"
#include "loader.h"
#include "units.h"
#include "bitems.h"
#include "game.h"
#include "bdeb.h"
#include "bact.h"

#define DS_MAX 10
typedef struct { uint16_t str; uint8_t qty; int16_t score; } dstat_t;
// Память банка (tools/build.ps1 ram = #B000)
static dstat_t __at(0xB000) ds[DS_MAX];  // строки в порядке _stats оригинала (до #B032)
static uint8_t __at(0xB040) ds_n;
static uint16_t __at(0xB041) d_title;
static int16_t __at(0xB043) d_total;
static uint8_t __at(0xB045) d_page;      // 1 — окно повышений (PromotionsState) вместо разбора
static uint8_t __at(0xB046) d_prom;      // повышенных в этой миссии
static const uint16_t rank_str[6] = { STR_ROOKIE, STR_SQUADDIE, STR_SERGEANT, STR_CAPTAIN, STR_COLONEL, STR_COMMANDER };

static const uint16_t ds_str[DS_MAX] = {
	STR_ALIENS_KILLED, STR_ALIEN_CORPSES_RECOVERED, STR_LIVE_ALIENS_RECOVERED,
	STR_ALIEN_ARTIFACTS_RECOVERED, STR_CIVILIANS_KILLED_BY_ALIENS, STR_CIVILIANS_KILLED_BY_XCOM_OPERATIVES,
	STR_CIVILIANS_SAVED, STR_XCOM_OPERATIVES_KILLED, STR_XCOM_OPERATIVES_MISSING_IN_ACTION,
	STR_XCOM_CRAFT_LOST,
};
#define D_KILLED 0
#define D_CORPSE 1
#define D_LIVE   2
#define D_ART    3
#define D_CIV_A  4
#define D_CIV_X  5
#define D_CIV_OK 6
#define D_XKIA   7
#define D_MIA    8
#define D_CRAFT  9

// Запись i — упавший в бою предмет (BI_DROP) и не труп: 1 — да, сама запись — в *b
static uint8_t dropped(const debreq_t *q, uint16_t i, bitem_t *b, const rtab_t *ti)
{
	far_read(FAR(q->page + 1, TL_BITEMS) + i * sizeof *b, b, sizeof *b);
	if (b->owner != BI_GROUND || b->slot != BI_DROP || b->item >= ti->n) return 0;
	return (uint8_t)rtab_word(ti, b->item, offsetof(r_items_t, battle_type)) != 11;   // BT_CORPSE
}

static void add_stat(uint8_t k, int16_t score)
{
	ds[k].qty++;
	ds[k].score += score;
}

// Итог для кампании (prepareDebriefing, DebriefingState.cpp:853-1070): корабль — домой, место миссии
// исчезает (и при прерывании), НЛО — тоже, а севшее, если отряд не удержал зону, через 5 с
// взлетает; погибшие солдаты — с базы; очки миссии — региону и стране, где стоял корабль; поле
// за отрядом — трупы и предметы пришельцев (с recover) на склад базы корабля (recoverItems:
// оружие и вставленная обойма отдельно). Гибель снаряжения и груза корабля — дальше (14_todo).
static void store(uint8_t b, const rtab_t *ti, uint16_t item)
{
	if (b >= MAX_BASES || item >= ti->n || item >= MAX_ITEMS) return;
	if (!(rtab_word(ti, item, offsetof(r_items_t, flags)) & ITEMS_F_RECOVER)) return;
	ST->base[b].items[item]++;
}

// BattleUnit::improveStat (:2268-2275)
static uint8_t improve(uint8_t e)
{
	return e > 10 ? (uint8_t)rng_range(2, 6) : e > 5 ? (uint8_t)rng_range(1, 4) : e > 2 ? (uint8_t)rng_range(1, 3)
		: e ? (uint8_t)rng_range(0, 1) : 0;
}

static void grow(uint8_t *v, uint8_t cap, uint8_t div)
{
	if (cap > *v) *v += (uint8_t)rng_range(0, (uint8_t)((cap - *v) / div + 2));
}

// Выживший боец i (BattleUnit::postMissionProcedures :2195-2261): миссии и убийства, дни лечения
// по потере здоровья (0.5..1.5), рост по опыту (BA_XP, bact.h) до потолков типа солдата: храбрость
// +10 с шансом, реакция/меткость/рукопашная/бросок — improveStat; был опыт (кроме броска) —
// новобранец в рядовые и прибавки к времени, здоровью, силе (до (потолок − значение)/10 + 2) и
// выносливости (/15 + 2). Глобальных повышений званий и второй страницы разбора пока нет
static void post_soldier(const unit_t *u, uint8_t i)
{
	soldier_t s;
	rtab_t t;
	r_soldiers_t sr;
	far_t a = FAR(SOLDIER_PAGE, 0) + (uint16_t)u->sol * sizeof s;
	far_read(a, &s, sizeof s);
	if (!rtab_open(RES_RULE_SOLDIERS, &t) || s.type >= t.n) return;
	rtab_get(&t, s.type, &sr);
	const uint8_t *x = BA_XP + (uint16_t)i * EXP_N;
	rstats_t *c = &s.cur;
	const rstats_t *cap = &sr.stat_caps;
	s.missions++;
	s.kills += x[EXP_KILLS];
	uint8_t hl = u->hp_max > u->hp ? (uint8_t)(u->hp_max - u->hp) : 0;
	s.recovery = (uint8_t)rng_range(hl / 2, hl + hl / 2);
	if (s.recovery) s.craft = NONE8;             // setWoundRecovery: раненый — из корабля
	if (x[EXP_BRAVE] && c->bravery < cap->bravery && x[EXP_BRAVE] > rng_range(0, 10)) c->bravery += 10;
	if (x[EXP_REACT] && c->reactions < cap->reactions) c->reactions += improve(x[EXP_REACT]);
	if (x[EXP_FIRE] && c->firing < cap->firing) c->firing += improve(x[EXP_FIRE]);
	if (x[EXP_MELEE] && c->melee < cap->melee) c->melee += improve(x[EXP_MELEE]);
	if (x[EXP_THROW] && c->throwing < cap->throwing) c->throwing += improve(x[EXP_THROW]);
	if (x[EXP_BRAVE] || x[EXP_REACT] || x[EXP_FIRE] || x[EXP_MELEE]) {
		if (!s.rank) { s.rank = 1; s.flags |= SF_PROMOTED; }   // RANK_ROOKIE -> SQUADDIE
		grow(&c->tu, cap->tu, 10);
		grow(&c->health, cap->health, 10);
		grow(&c->strength, cap->strength, 10);
		grow(&c->stamina, cap->stamina, 15);
	}
	far_write(a, &s, sizeof s);
}

// SavedGame::getSoldierScore (:1667-1675)
static uint16_t s_score(const soldier_t *s)
{
	const rstats_t *c = &s->cur;
	uint16_t v = 2 * c->health + 2 * c->stamina + 4 * c->reactions + 4 * c->bravery
		+ 3 * (c->tu + 2 * c->firing) + c->melee + c->throwing + c->strength;
	if (c->psi_skill) v += c->psi_strength + 2 * c->psi_skill;
	return v + 10 * (s->missions + s->kills);
}

// Лучший по очкам солдат звания rank (inspectSoldiers); #FF — нет
static uint8_t s_best(uint8_t rank)
{
	uint16_t hi = 0;
	uint8_t k = 0xFF;
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {
		soldier_t s;
		far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof s, &s, sizeof s);
		if (s.base == NONE8 || s.rank != rank) continue;
		uint16_t sc = s_score(&s);
		if (sc > hi) { hi = sc; k = i; }
	}
	return k;
}

static void s_promote(uint8_t i)
{
	soldier_t s;
	far_t a = FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof s;
	far_read(a, &s, sizeof s);
	s.rank++;                                    // promoteRank: _recentlyPromoted
	s.flags |= SF_PROMOTED;
	far_write(a, &s, sizeof s);
}

// Повышения званий по всему составу (SavedGame::handlePromotions :1510-1608): коммандер — один,
// если солдат не меньше 30; полковников — 1 на 23, капитанов — 1 на 11, сержантов — 1 на 5;
// каждое — лучшему по очкам званием ниже (fieldPromotions выключен — любой, не только с миссии)
static void promotions(void)
{
	uint8_t n = 0, cnt[6];
	memset(cnt, 0, sizeof cnt);
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {
		soldier_t s;
		far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof s, &s, sizeof s);
		if (s.base == NONE8) continue;
		n++;
		if (s.rank < 6) cnt[s.rank]++;
	}
	uint8_t k;
	if (!cnt[5] && n >= 30 && (k = s_best(4)) != 0xFF) { s_promote(k); cnt[5]++; cnt[4]--; }
	while (n / 23 > cnt[4] && (k = s_best(3)) != 0xFF) { s_promote(k); cnt[4]++; cnt[3]--; }
	while (n / 11 > cnt[3] && (k = s_best(2)) != 0xFF) { s_promote(k); cnt[3]++; cnt[2]--; }
	while (n / 5 > cnt[2] && (k = s_best(1)) != 0xFF) { s_promote(k); cnt[2]++; }
}

// Поле за отрядом: трупы и упавшее в бою — на склад базы корабля (кампания)
static void loot(const debreq_t *q)
{
	if (q->craft >= MAX_CRAFTS || ST->craft[(uint8_t)q->craft].type == NONE8) return;
	uint8_t b = ST->craft[(uint8_t)q->craft].base;
	rtab_t ti, ta;
	unit_t u;
	rtab_open(RES_RULE_ITEMS, &ti);
	rtab_open(RES_RULE_ARMORS, &ta);
	for (uint8_t i = q->nsold; i < q->n; i++) {     // трупы
		far_read(UNITS_FAR + (uint16_t)i * sizeof u, &u, sizeof u);
		if (u.alive || u.hp || u.faction != FACTION_HOSTILE || u.armor_rule >= ta.n) continue;
		r_armors_t a;
		uint16_t corpse;
		rtab_get(&ta, u.armor_rule, &a);
		if (!a.corpse_battle.n) continue;
		rtab_tail(&ta, a.corpse_battle.off, &corpse, 2);
		store(b, &ti, corpse);
	}
	for (uint16_t i = 0; i < BI_MAX; i++) {          // упавшее в бою (вещи выбывших)
		bitem_t it;
		if (!dropped(q, i, &it, &ti)) continue;
		store(b, &ti, it.item);
		if (it.ammo != 0xFFFF && it.rounds) store(b, &ti, it.ammo);
	}
}

static void campaign(const debreq_t *q, uint8_t alive_x, uint8_t success)
{
	uint8_t c = (uint8_t)q->craft;
	craft_t *cr = &ST->craft[c];
	if (cr->type == NONE8) return;
	uint8_t dk = cr->dest_kind, di = cr->dest;
	geo_t pos = cr->pos;
	if (success) loot(q);
	craft_return(c);                             // returnToBase: цель кораблю меняет — взята раньше
	if (dk == DK_SITE && di < MAX_SITES && ST->site[di].id) { ST->site[di].id = 0; ST->nsites--; }
	if (dk == DK_UFO && di < MAX_UFOS && ST->ufo[di].type != NONE8) {
		if (ST->ufo[di].status == US_LANDED && (q->aborted || !alive_x)) ST->ufo[di].secs = 5;
		else ufo_free(di);
	}
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {   // прежние повышения забыты (_recentlyPromoted)
		far_t a = FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof(soldier_t) + offsetof(soldier_t, flags);
		uint8_t f = (uint8_t)(far_byte(a) & ~SF_PROMOTED);
		far_write(a, &f, 1);
	}
	for (uint8_t i = 0; i < q->nsold; i++) {        // погибшие — с базы (killSoldier), выжившие — опыт
		unit_t u;
		far_read(UNITS_FAR + (uint16_t)i * sizeof u, &u, sizeof u);
		if (u.sol == 0xFF) continue;
		if (!u.alive && !u.hp) soldier_delete(u.sol);
		else post_soldier(&u, i);
	}
	promotions();
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {   // кого показать в окне повышений
		soldier_t s;
		far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof s, &s, sizeof s);
		if (s.base != NONE8 && (s.flags & SF_PROMOTED)) d_prom++;
	}
	add_activity(&pos, d_total, 1);
}

// Строки очков — с нуля, если не копятся с прошлых фаз миссии (DEB_ACC)
static void ds_reset(void)
{
	if (DEB_ACC) return;
	for (uint8_t k = 0; k < DS_MAX; k++) { ds[k].str = ds_str[k]; ds[k].qty = 0; ds[k].score = 0; }
}

// Очки по юнитам с i0 и упавшим предметам (prepareDebriefing); success — поле за отрядом
static void score(const debreq_t *q, uint8_t success, uint8_t i0)
{
	rtab_t tu, ti, ta;
	unit_t u;
	rtab_open(RES_RULE_UNITS, &tu);
	rtab_open(RES_RULE_ITEMS, &ti);
	rtab_open(RES_RULE_ARMORS, &ta);
	for (uint8_t i = i0; i < q->n; i++) {
		far_read(UNITS_FAR + (uint16_t)i * sizeof u, &u, sizeof u);
		uint8_t f = u.faction;                   // сравнения — с копиями (SDCC 4.5)
		if (f == FACTION_NEUTRAL) {              // мирный (:1199-1205, :1291-1302)
			int16_t value = u.type < tu.n ? (int16_t)rtab_word(&tu, u.type, offsetof(r_units_t, value)) : 0;
			if (u.alive || u.hp) {               // жив (оглушённый — тоже): бой проигран — погиб
				if (success) add_stat(D_CIV_OK, value);
				else add_stat(D_CIV_A, -value);
			} else if (u.done) add_stat(D_CIV_X, -value - 2 * (value / 3));   // убит отрядом
			else add_stat(D_CIV_A, -value);
			continue;
		}
		if (u.alive) continue;
		if (f == FACTION_HOSTILE) {
			int16_t value = u.type < tu.n ? (int16_t)rtab_word(&tu, u.type, offsetof(r_units_t, value)) : 0;
			if (!u.hp) {                             // убит (killedBy FACTION_PLAYER)
				add_stat(D_KILLED, value);
				if (success && u.armor_rule < ta.n) {   // его труп с поля (recoverItems: BT_CORPSE)
					r_armors_t a;
					uint16_t c;
					rtab_get(&ta, u.armor_rule, &a);
					if (a.corpse_battle.n) {
						rtab_tail(&ta, a.corpse_battle.off, &c, 2);
						if (c < ti.n) add_stat(D_CORPSE, (int16_t)rtab_word(&ti, c, offsetof(r_items_t, recovery_points)));
					}
				}
			} else if (success) add_stat(D_LIVE, value * 2);   // оглушён и подобран
		} else if (u.faction == FACTION_PLAYER && !u.hp) {    // погиб боец
			uint8_t st = 0;
			rtab_t ts;
			if (u.sol != 0xFF) far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)u.sol * sizeof(soldier_t) + offsetof(soldier_t, type), &st, 1);
			int16_t value = rtab_open(RES_RULE_SOLDIERS, &ts) && st < ts.n ? (uint8_t)rtab_word(&ts, st, offsetof(r_soldiers_t, value)) : 20;
			add_stat(D_XKIA, -value);
		}
	}
	if (success)                                 // упавшее в бою (вещи выбывших; не груз корабля и не
		for (uint16_t i = 0; i < BI_MAX; i++) {  //   трупы — те по убитым): их recoveryPoints
			bitem_t b;
			if (!dropped(q, i, &b, &ti)) continue;
			int16_t rp = (int16_t)rtab_word(&ti, b.item, offsetof(r_items_t, recovery_points));
			if (rp) add_stat(D_ART, rp);
		}
}

// Конец фазы (BattlescapeGenerator::nextStage): пришельцы и мирные фазы, упавшее — в итог (бойцы —
// в конце миссии); won — пришельцы перебиты: трофеи — на склад (takeHomeGuaranteed)
void deb_stage(const debreq_t *q, uint8_t won) __banked
{
	ds_reset();
	score(q, won, q->nsold);
	if (won) loot(q);
	DEB_ACC = 1;
}

void deb_start(const debreq_t *q) __banked
{
	unit_t u;
	uint8_t alive_x = 0;
	ds_reset();
	DEB_ACC = 0;
	for (uint8_t i = 0; i < q->nsold; i++) {
		far_read(UNITS_FAR + (uint16_t)i * sizeof u, &u, sizeof u);
		if (u.alive) alive_x++;
	}
	uint8_t success = !q->aborted && alive_x;
	score(q, success, 0);
	if (!alive_x) add_stat(D_CRAFT, 0);          // отряд не вернулся (очки корабля — когда будет кампания)
	if (success) d_title = q->ufo ? STR_UFO_IS_RECOVERED : STR_ALIENS_DEFEATED;
	else if (!alive_x) d_title = STR_CRAFT_IS_LOST;
	else d_title = q->ufo ? STR_UFO_IS_NOT_RECOVERED : STR_TERROR_CONTINUES;
	ds_n = 0;                                    // ненулевые строки — подряд в начало
	d_total = 0;
	for (uint8_t k = 0; k < DS_MAX; k++) {
		if (!ds[k].qty) continue;
		d_total += ds[k].score;
		ds[ds_n++] = ds[k];
	}
	d_page = d_prom = 0;
	if (q->craft < MAX_CRAFTS) campaign(q, alive_x, success);
}

// k-й повышенный солдат (порядок записей), #FF — нет
static uint8_t prom_nth(uint8_t k, soldier_t *s)
{
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {
		far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof *s, s, sizeof *s);
		if (s->base == NONE8 || !(s->flags & SF_PROMOTED)) continue;
		if (!k--) return i;
	}
	return 0xFF;
}

uint8_t deb_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	(void)id;
	if (d_page) {                                   // PromotionsState (PromotionsState.cpp:39-96)
		static const wdef_t prom[] = {
			WIN(0, 0, 320, 200, UI_EL_WINDOW),
			BTN(16, 176, 288, 16, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
			TXT(10, 8, 300, 17, UI_EL_HEADING, STR_PROMOTIONS, TC | BIG),
			TXT(16, 32, 114, 9, UI_EL_TEXT, STR_NAME, 0),
			TXT(130, 32, 90, 9, UI_EL_TEXT, STR_NEW_RANK, 0),
			TXT(220, 32, 80, 9, UI_EL_TEXT, STR_BASE, 0),
			LST(8, 40, 288, 128, UI_EL_LIST, 4, 0),
			HOT(0, 0, 0, 0, A_CUSTOM, 1, ESC),          // keyCancel — тоже OK
		};
		memcpy(w, prom, sizeof prom);
		s->ui = UI_SCR_PROMOTIONS;
		s->palui = 0xFF;
		s->bg = RES_BACK01_SCR;
		s->flags = 0;
		s->n = sizeof prom / sizeof prom[0];
		return 1;
	}
	static const wdef_t fixed[] = {
		WIN(0, 0, 320, 200, UI_EL_WINDOW),
		TXT(16, 8, 300, 17, UI_EL_HEADING, DYN(0), BIG),
		TXT(16, 24, 180, 9, UI_EL_TEXT, STR_LIST_ITEM, 0),
		TXT(200, 24, 60, 9, UI_EL_TEXT, STR_QUANTITY_UC, TR),
		TXT(270, 24, 55, 9, UI_EL_TEXT, STR_SCORE, 0),
		LST(16, 32, 290, 80, UI_EL_LIST, 1, 0),
		LST(16, 37, 290, 9, UI_EL_TOTALS, 2, 0),
		TXT(64, 180, 200, 9, UI_EL_TEXT, DYN(3), 0),
		BTN(16, 180, 40, 12, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
		HOT(0, 0, 0, 0, A_CUSTOM, 1, ESC),          // keyCancel — тоже OK
	};
	memcpy(w, fixed, sizeof fixed);
	w[5].h = ds_n ? ds_n * 8 : 8;
	w[6].y = 32 + ds_n * 8 + 5;                     // _lstTotal — под списком
	s->ui = UI_SCR_DEBRIEFING;
	s->palui = 0xFF;
	s->bg = RES_BACK01_SCR;
	s->flags = 0;
	s->n = sizeof fixed / sizeof fixed[0];
	return 1;
}

void deb_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	(void)id;
	buf[0] = 0;
	if (slot == 4) {                                // повышенные: имя, новое звание, база
		if (row == LIST_COLS) {
			static const uint8_t cs[] = { 114, 90, 84 }, ca[] = { 0, 0, 0 };
			list_cols(buf, 3, cs, ca);
			return;
		}
		soldier_t s;
		if (prom_nth(row, &s) == 0xFF) return;
		memcpy(buf, s.name, SOLDIER_NAME);
		buf[SOLDIER_NAME] = 0;
		strcat(buf, "\t");
		str_copy(rank_str[s.rank < 6 ? s.rank : 0], buf + strlen(buf), 30);
		strcat(buf, "\t");
		if (s.base < MAX_BASES) {
			uint8_t l = (uint8_t)strlen(buf);
			memcpy(buf + l, ST->base[s.base].name, NAME_LEN);
			buf[l + NAME_LEN] = 0;
		}
		return;
	}
	if (slot == 0) { str_copy(d_title, buf, 64); return; }
	if (slot == 3) {                                // STR_RATING {0}
		uint16_t r = d_total <= -200 ? STR_RATING_TERRIBLE : d_total <= 0 ? STR_RATING_POOR
			: d_total <= 200 ? STR_RATING_OK : d_total <= 500 ? STR_RATING_GOOD : STR_RATING_EXCELLENT;
		char a[24];
		str_copy(r, a, sizeof a);
		str_fmt(buf, str_get(STR_RATING), a, "");
		return;
	}
	if (row == LIST_COLS) {
		static const uint8_t cs[] = { 224, 30, 64 }, ct[] = { 254, 64 }, ca[] = { LC_DOT, LC_DOT, 0 };
		if (slot == 1) list_cols(buf, 3, cs, ca);
		else list_cols(buf, 2, ct, ca);
		return;
	}
	if (slot == 2) {                                // STR_TOTAL_UC .... итог
		str_copy(STR_TOTAL_UC, buf, 48);
		strcat(buf, "\t");
		fmt_num(buf + strlen(buf), d_total, 0);
		return;
	}
	if (row >= ds_n) return;
	str_copy(ds[row].str, buf, 64);                 // количество и очки — вторым цветом (TOK_COLOR_FLIP)
	strcat(buf, "\t\x01");
	fmt_num(buf + strlen(buf), ds[row].qty, 0);
	strcat(buf, "\x01\t\x01");
	fmt_num(buf + strlen(buf), ds[row].score, 0);
}

uint8_t deb_rows(uint8_t id, uint8_t slot) __banked
{
	(void)id;
	return slot == 1 ? ds_n : slot == 2 ? 1 : slot == 4 ? d_prom : 0;
}

uint8_t deb_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	(void)id;
	if (ev == EVT_BUTTON && arg == 1) {
		// btnOkClick: были повышения — окно PromotionsState, потом — назад на геоскейп (бой закрывается)
		if (!d_page && d_prom) { d_page = 1; UI_GO(A_REDRAW, 0); }
		else UI_GO(A_POP2, 0);
	}
	return 0;
}
