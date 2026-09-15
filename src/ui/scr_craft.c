// Банк 10: корабль базы и его снаряжение (CraftInfoState, CraftSoldiersState,
// CraftWeaponsState, CraftEquipmentState, CraftArmorState, SoldierArmorState).
// Раскладки — tmp/screens_basescape.json. Корабль — ctx.craft, база — ctx.base.
// Без добавок OpenXcom: имя корабля не редактируется, нет сортировки и
// перестановки экипажа, правого клика «последняя броня».
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "text.h"
#include "gfx.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

static char t1[48], t2[48];
static uint8_t lst[160], nlst;          // строки открытого списка
static uint8_t sol_rec;                 // SoldierArmor: запись солдата

#define HWP_SIZE 4                      // места HWP в корабле (броня 2x2)

static uint16_t rec_name(uint16_t table, uint16_t i)
{
	rtab_t t;
	if (!rtab_open(table, &t) || i >= t.n) return 0xFFFF;
	return rtab_word(&t, i, 0);
}

static uint16_t craft_rule(uint8_t off)
{
	rtab_t t;
	rtab_open(RES_RULE_CRAFTS, &t);
	return rtab_word(&t, ST->craft[ctx.craft].type, off);
}

static void error(uint16_t str)
{
	str_copy(str, ui_msg, sizeof ui_msg);
	UI_GO(A_PUSH, SCR_ERROR);
}

// ---------------------------------------------------------------- места в корабле

static uint8_t nvehicles(void)
{
	uint8_t cg = ST->craft[ctx.craft].cargo, n = 0;
	if (cg == NONE8) return 0;
	for (uint8_t k = 0; k < CARGO_VEH; k++) if (ST->cargo[cg].veh[k].type != NONE8) n++;
	return n;
}

static uint8_t ncrew(void) { return soldiers_count(ctx.base, ctx.craft, 0); }

// Craft::getSpaceUsed: солдаты + HWP по 4 места
static int16_t space_used(void) { return ncrew() + nvehicles() * HWP_SIZE; }
static int16_t space_avail(void) { return (int16_t)(craft_rule(offsetof(r_crafts_t, soldiers)) & 0xFF) - space_used(); }

static uint16_t cargo_qty(uint8_t it)
{
	uint8_t cg = ST->craft[ctx.craft].cargo;
	if (cg == NONE8) return 0;
	for (uint8_t k = 0; k < CARGO_ITEMS; k++) if (ST->cargo[cg].it[k].item == it) return ST->cargo[cg].it[k].qty;
	return 0;
}

static uint16_t cargo_total(void)
{
	uint8_t cg = ST->craft[ctx.craft].cargo;
	uint16_t n = 0;
	if (cg == NONE8) return 0;
	for (uint8_t k = 0; k < CARGO_ITEMS; k++) if (ST->cargo[cg].it[k].item != NONE8) n += ST->cargo[cg].it[k].qty;
	return n;
}

static void cargo_sub(uint8_t it, uint16_t q)
{
	uint8_t cg = ST->craft[ctx.craft].cargo;
	for (uint8_t k = 0; k < CARGO_ITEMS; k++)
		if (ST->cargo[cg].it[k].item == it) {
			ST->cargo[cg].it[k].qty -= q;
			if (!ST->cargo[cg].it[k].qty) ST->cargo[cg].it[k].item = NONE8;
			return;
		}
}

static uint8_t veh_count(uint8_t it)
{
	uint8_t cg = ST->craft[ctx.craft].cargo, n = 0;
	if (cg == NONE8) return 0;
	for (uint8_t k = 0; k < CARGO_VEH; k++) if (ST->cargo[cg].veh[k].type == it) n++;
	return n;
}

// ---------------------------------------------------------------- CraftInfo

static const wdef_t w_info[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(128, 168, 64, 24, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(80, 8, 140, 16, UI_EL_TEXT1, DYN(0), BIG | TC),
	TXT(14, 24, 100, 17, UI_EL_TEXT1, DYN(1), 0),
	TXT(228, 24, 82, 17, UI_EL_TEXT1, DYN(2), 0),
	CUS(144, 52, 32, 40, NOSTR, A_NONE, 0),
};
static const wdef_t w_info_w1[] = {
	BTN(14, 48, 24, 32, UI_EL_BUTTON, DYN(10), A_CUSTOM, 1, 0),
	TXT(46, 48, 95, 16, UI_EL_TEXT2, DYN(3), TW),
	TXT(46, 64, 75, 24, UI_EL_TEXT3, DYN(4), 0),
	CUS(121, 63, 15, 17, NOSTR, A_NONE, 0),
};
static const wdef_t w_info_w2[] = {
	BTN(282, 48, 24, 32, UI_EL_BUTTON, DYN(11), A_CUSTOM, 2, 0),
	TXT(184, 48, 95, 16, UI_EL_TEXT2, DYN(5), TW),
	TXT(204, 64, 75, 24, UI_EL_TEXT3, DYN(6), 0),
	CUS(184, 63, 15, 17, NOSTR, A_NONE, 0),
};
static const wdef_t w_info_crew[] = {
	BTN(14, 96, 64, 16, UI_EL_BUTTON, STR_CREW, A_PUSH, SCR_CRAFT_SOLDIERS, 0),
	BTN(14, 120, 64, 16, UI_EL_BUTTON, STR_EQUIPMENT_UC, A_PUSH, SCR_CRAFT_EQUIP, 0),
	BTN(14, 144, 64, 16, UI_EL_BUTTON, STR_ARMOR, A_PUSH, SCR_CRAFT_ARMOR, 0),
	CUS(85, 96, 220, 18, NOSTR, A_NONE, 0),
	CUS(85, 121, 220, 18, NOSTR, A_NONE, 0),
};

// formatTime: "\n(" + [N дней/] + [N часов] + ")"
static void fmt_time(char *buf, uint16_t hours)
{
	uint16_t d = hours / 24, h = hours % 24;
	strcat(buf, "\n(");
	if (d) { str_plural(buf + strlen(buf), STR_DAY_one, d); strcat(buf, "/"); }
	if (h) str_plural(buf + strlen(buf), STR_HOUR_one, h);
	strcat(buf, ")");
}

static void weapon_text(uint8_t k, uint8_t ammo, char *buf)
{
	craft_t *cr = &ST->craft[ctx.craft];
	cweapon_t *w = &cr->weap[k];
	if (w->type == NONE8) return;
	rtab_t t;
	rtab_open(RES_RULE_CRAFTWEAPONS, &t);
	if (!ammo) { strcpy(buf, "\x01"); str_copy(rtab_word(&t, w->type, 0), buf + 1, 48); return; }
	uint16_t mx = rtab_word(&t, w->type, offsetof(r_craftWeapons_t, ammo_max));
	uint8_t rate = (uint8_t)rtab_word(&t, w->type, offsetof(r_craftWeapons_t, rearm_rate));
	fmt_num(t1, w->ammo, 0);
	str_fmt(buf, str_get(STR_AMMO_), t1, "");
	strcat(buf, "\n\x01");
	fmt_num(t1, mx, 0);
	str_fmt(buf + strlen(buf), str_get(STR_MAX), t1, "");
	if (cr->status == CS_REARM && w->ammo < mx && rate) fmt_time(buf, (mx - w->ammo + rate - 1) / rate);
}

static void info_text(uint8_t slot, char *buf)
{
	craft_t *cr = &ST->craft[ctx.craft];
	rtab_t t;
	rtab_open(RES_RULE_CRAFTS, &t);
	switch (slot) {
	case 0: craft_name(ctx.craft, buf); break;
	case 1: {
		uint16_t dm = rtab_word(&t, cr->type, offsetof(r_crafts_t, damage_max));
		uint8_t rr = (uint8_t)rtab_word(&t, cr->type, offsetof(r_crafts_t, repair_rate));
		fmt_num(t1, dm ? (int32_t)cr->damage * 100 / dm : 0, 0); strcat(t1, "%");
		str_fmt(buf, str_get(STR_DAMAGE_UC_), t1, "");
		if (cr->status == CS_REPAIR && rr) fmt_time(buf, (cr->damage + rr - 1) / rr);
		break;
	}
	case 2: {
		uint16_t fm = rtab_word(&t, cr->type, offsetof(r_crafts_t, fuel_max));
		uint8_t rf = (uint8_t)rtab_word(&t, cr->type, offsetof(r_crafts_t, refuel_rate));
		fmt_num(t1, fm ? (int32_t)cr->fuel * 100 / fm : 0, 0); strcat(t1, "%");
		str_fmt(buf, str_get(STR_FUEL), t1, "");
		if (cr->status == CS_REFUEL && rf) fmt_time(buf, ((fm - cr->fuel + rf - 1) / rf + 1) / 2);
		break;
	}
	case 3: weapon_text(0, 0, buf); break;
	case 4: weapon_text(0, 1, buf); break;
	case 5: weapon_text(1, 0, buf); break;
	case 6: weapon_text(1, 1, buf); break;
	case 10: strcpy(buf, "1"); break;
	case 11: strcpy(buf, "2"); break;
	}
}

// Картинки: корабль (BASEBITS craftSprite+33), оружие (+48), экипаж (кадр 38 на
// солдата), снаряжение (40 на HWP, затем 39 на каждые 4 предмета). i — номер
// виджета W_CUSTOM в порядке ship_get: 5 — корабль, в блоке оружия — 4-й,
// в блоке экипажа — 4-й (экипаж) и 5-й (снаряжение).
static void info_draw(uint8_t i)
{
	craft_t *cr = &ST->craft[ctx.craft];
	uint8_t nw = (uint8_t)craft_rule(offsetof(r_crafts_t, weapons));
	if (nw > 2) nw = 2;
	uint8_t crew0 = 6 + 4 * nw;
	if (i == 5) {
		gfx_sprite(RES_BASEBITS_PCK, (int16_t)craft_rule(offsetof(r_crafts_t, sprite)) + 33, 144, 52);
	} else if (i < crew0) {
		uint8_t k = i >= 10;
		if (cr->weap[k].type == NONE8) return;
		rtab_t t;
		rtab_open(RES_RULE_CRAFTWEAPONS, &t);
		gfx_sprite(RES_BASEBITS_PCK, (int16_t)rtab_word(&t, cr->weap[k].type, offsetof(r_craftWeapons_t, sprite)) + 48, k ? 184 : 121, 63);
	} else if (i == crew0 + 3) {
		uint8_t n = ncrew();
		for (uint8_t k = 0; k < n && k < 22; k++) gfx_sprite(RES_BASEBITS_PCK, 38, 85 + k * 10, 96);
	} else {
		int16_t x = 85;
		uint8_t nv = nvehicles();
		for (uint8_t k = 0; k < nv; k++, x += 10) gfx_sprite(RES_BASEBITS_PCK, 40, x, 121);
		uint16_t ni = cargo_total();
		for (uint16_t k = 0; k < ni && x < 305; k += 4, x += 10) gfx_sprite(RES_BASEBITS_PCK, 39, x, 121);
	}
}

// ---------------------------------------------------------------- экипаж

static const wdef_t w_soldiers[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(164, 176, 148, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(16, 7, 300, 17, UI_EL_TEXT, DYN(0), BIG),
	TXT(16, 32, 114, 9, UI_EL_TEXT, STR_NAME_UC, 0),
	TXT(122, 32, 102, 9, UI_EL_TEXT, STR_RANK, 0),
	TXT(224, 32, 84, 9, UI_EL_TEXT, STR_CRAFT, 0),
	TXT(16, 24, 110, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(122, 24, 110, 9, UI_EL_TEXT, DYN(2), 0),
	LST(16, 40, 280, 128, UI_EL_LIST, 3, WF_SEL),
};

static const uint16_t rank_str[6] = { STR_ROOKIE, STR_SQUADDIE, STR_SERGEANT, STR_CAPTAIN, STR_COLONEL, STR_COMMANDER };

// Корабль солдата: STR_WOUNDED / STR_NONE_UC / имя корабля
static void craft_cell(const soldier_t *s, char *buf)
{
	if (s->recovery) str_copy(STR_WOUNDED, buf, 40);
	else if (s->craft != NONE8) craft_name(s->craft, buf);
	else str_copy(STR_NONE_UC, buf, 40);
}

// Начало строки солдата: в этом корабле — color2, в другом — otherCraft
static char *soldier_row_start(const soldier_t *s, char *buf, uint8_t *alt)
{
	*alt = 0;
	if (s->craft == ctx.craft) { *alt = 1; strcpy(buf, "\x01"); return buf + 1; }
	if (s->craft != NONE8) { buf[0] = 3; buf[1] = (char)ui_color(UI_EL_OTHERCRAFT, 0); buf[2] = 0; return buf + 2; }
	buf[0] = 0;
	return buf;
}

// ---------------------------------------------------------------- оружие

static const wdef_t w_weapons[] = {
	WIN(50, 20, 220, 160, UI_EL_WINDOW),
	BTN(90, 156, 140, 16, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(56, 28, 208, 17, UI_EL_TEXT, STR_SELECT_ARMAMENT, BIG | TC),
	TXT(66, 52, 76, 9, UI_EL_TEXT, STR_ARMAMENT, 0),
	TXT(140, 52, 50, 9, UI_EL_TEXT, STR_QUANTITY_UC, 0),
	TXT(200, 44, 68, 17, UI_EL_TEXT, STR_AMMUNITION_AVAILABLE, TW | TX_BOTTOM),
	LST(66, 68, 180, 80, UI_EL_LIST, 0, WF_SEL),
};
static uint8_t weap_slot;               // CraftWeapons: слот 0/1

// CraftWeaponsState::lstWeaponsClick: старое оружие (пусковая + обоймы) — на склад,
// новое: пусковая со склада, патронов 0, статус «перевооружение»
static void weapon_set(uint8_t row)
{
	craft_t *cr = &ST->craft[ctx.craft];
	base_t *bs = &ST->base[ctx.base];
	cweapon_t *w = &cr->weap[weap_slot];
	rtab_t tw, ti;
	rtab_open(RES_RULE_CRAFTWEAPONS, &tw);
	rtab_open(RES_RULE_ITEMS, &ti);
	if (w->type != NONE8) {
		uint16_t l = rtab_word(&tw, w->type, offsetof(r_craftWeapons_t, launcher));
		uint16_t c = rtab_word(&tw, w->type, offsetof(r_craftWeapons_t, clip));
		if (l < MAX_ITEMS) bs->items[l]++;
		if (c < MAX_ITEMS) {
			int16_t cs = (int16_t)rtab_word(&ti, c, offsetof(r_items_t, clip_size));
			if (cs > 0) bs->items[c] += w->ammo / cs;
		}
		w->type = NONE8;
		w->ammo = 0;
	}
	if (row) {
		uint8_t wt = lst[row - 1];
		uint16_t l = rtab_word(&tw, wt, offsetof(r_craftWeapons_t, launcher));
		w->type = wt;
		w->ammo = 0;
		w->rearming = 1;
		if (l < MAX_ITEMS && bs->items[l]) bs->items[l]--;
		if (cr->status == CS_READY) cr->status = CS_REARM;
	}
}

// ---------------------------------------------------------------- снаряжение

static const wdef_t w_equip[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	TXT(16, 7, 300, 17, UI_EL_TEXT, DYN(0), BIG),
	TXT(16, 32, 144, 9, UI_EL_TEXT, STR_ITEM, 0),
	TXT(160, 32, 150, 9, UI_EL_TEXT, STR_STORES, 0),
	TXT(16, 24, 110, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(130, 24, 110, 9, UI_EL_TEXT, DYN(2), 0),
	TXT(244, 24, 71, 9, UI_EL_TEXT, DYN(5), 0),
	LST(16, 40, 280, 128, UI_EL_LIST, 3, 0),
};
static const wdef_t w_equip_crew[] = {          // есть экипаж: инвентарь + OK справа
	BTN(8, 176, 148, 16, UI_EL_BUTTON, STR_INVENTORY, A_CUSTOM, 1, 0),
	BTN(164, 176, 148, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
};
static const wdef_t w_equip_nocrew[] = {
	BTN(16, 176, 288, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
};

#define BT_NONE   0
#define BT_AMMO   2
#define BT_CORPSE 11

// Строки: предметы боя (bigSprite есть, не NONE/CORPSE), исследованные, есть на
// складе или в корабле (CraftEquipmentState::CraftEquipmentState)
static void equip_fill(void)
{
	rtab_t t;
	r_items_t it;
	nlst = 0;
	rtab_open(RES_RULE_ITEMS, &t);
	for (uint8_t i = 0; i < t.n && i < MAX_ITEMS && nlst < sizeof lst; i++) {
		rtab_get(&t, i, &it);
		if (it.big_sprite < 0 || it.battle_type == BT_NONE || it.battle_type == BT_CORPSE) continue;
		if (!res_list_done(RES_RULE_ITEMS, i, offsetof(r_items_t, requires))) continue;
		uint16_t cq = (it.flags & ITEMS_F_FIXED_WEAPON) ? veh_count(i) : cargo_qty(i);
		if (!ST->base[ctx.base].items[i] && !cq) continue;
		lst[nlst++] = i;
	}
}

// Боеприпас HWP: предмет и сколько штук на машину (ammoPerVehicle)
static uint8_t hwp_ammo(uint8_t it, uint16_t *per)
{
	rtab_t t;
	r_items_t a;
	rlist_t am;
	rtab_open(RES_RULE_ITEMS, &t);
	far_read(t.base + 8 + (uint32_t)it * t.size + offsetof(r_items_t, compatible_ammo), &am, sizeof am);
	if (!am.n) return NONE8;
	uint16_t ammo;
	rtab_tail(&t, am.off, &ammo, 2);
	if (ammo >= MAX_ITEMS) return NONE8;
	rtab_get(&t, (uint8_t)ammo, &a);
	int16_t cs = (int16_t)rtab_word(&t, it, offsetof(r_items_t, clip_size));
	*per = cs > 0 && a.clip_size > 0 ? cs / a.clip_size : (a.clip_size > 0 ? a.clip_size : 1);
	return (uint8_t)ammo;
}

// CraftEquipmentState::moveLeftByValue — в склад
static void equip_left(uint8_t it, uint16_t change)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	uint16_t fl = rtab_word(&t, it, offsetof(r_items_t, flags));
	base_t *bs = &ST->base[ctx.base];
	if (fl & ITEMS_F_FIXED_WEAPON) {
		uint8_t cg = ST->craft[ctx.craft].cargo;
		uint16_t per = 0;
		uint8_t ammo = hwp_ammo(it, &per);
		for (uint8_t k = 0; k < CARGO_VEH && change; k++) {
			if (ST->cargo[cg].veh[k].type != it) continue;
			ST->cargo[cg].veh[k].type = NONE8;
			bs->items[it]++;
			if (ammo != NONE8) bs->items[ammo] += per;
			change--;
		}
		return;
	}
	uint16_t cq = cargo_qty(it);
	if (!cq) return;
	if (change > cq) change = cq;
	cargo_sub(it, change);
	bs->items[it] += change;
}

// CraftEquipmentState::moveRightByValue — в корабль
static void equip_right(uint8_t it, uint16_t change)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	uint16_t fl = rtab_word(&t, it, offsetof(r_items_t, flags));
	base_t *bs = &ST->base[ctx.base];
	craft_t *cr = &ST->craft[ctx.craft];
	uint16_t bq = bs->items[it];
	if (!bq || cr->cargo == NONE8) return;
	if (change > bq) change = bq;
	if (fl & ITEMS_F_FIXED_WEAPON) {
		int16_t room = (int16_t)(craft_rule(offsetof(r_crafts_t, vehicles)) & 0xFF) - nvehicles();
		int16_t bys = space_avail() / HWP_SIZE;
		if (bys < room) room = bys;
		if (room <= 0) return;
		if (change > (uint16_t)room) change = room;
		uint16_t per = 0;
		uint8_t ammo = hwp_ammo(it, &per);
		if (ammo != NONE8) {
			uint16_t can = per ? bs->items[ammo] / per : 0;
			if (change > can) change = can;
			if (!change) {
				fmt_num(t1, per, 0);
				str_copy(rec_name(RES_RULE_ITEMS, ammo), t2, sizeof t2);
				str_fmt(ui_msg, str_get(STR_NOT_ENOUGH_AMMO_TO_ARM_HWP), t1, t2);
				UI_GO(A_PUSH, SCR_ERROR);
				return;
			}
		}
		int16_t cs = (int16_t)rtab_word(&t, it, offsetof(r_items_t, clip_size));
		cargo_t *cg = &ST->cargo[cr->cargo];
		for (uint8_t k = 0; k < CARGO_VEH && change; k++) {
			if (cg->veh[k].type != NONE8) continue;
			cg->veh[k].type = it;
			cg->veh[k].ammo = cs;
			bs->items[it]--;
			if (ammo != NONE8) bs->items[ammo] -= per;
			change--;
		}
		return;
	}
	uint16_t mx = craft_rule(offsetof(r_crafts_t, max_items)), tot = cargo_total();
	if (mx && tot + change > mx) {
		str_plural(ui_msg, STR_NO_MORE_EQUIPMENT_ALLOWED_one, mx);
		UI_GO(A_PUSH, SCR_ERROR);
		change = mx > tot ? mx - tot : 0;
	}
	if (!change) return;
	cargo_add(cr->cargo, it, change);
	bs->items[it] -= change;
}

// ---------------------------------------------------------------- броня

static const wdef_t w_carmor[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(16, 176, 288, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(16, 7, 300, 17, UI_EL_TEXT, STR_SELECT_ARMOR, BIG),
	TXT(16, 32, 114, 9, UI_EL_TEXT, STR_NAME_UC, 0),
	TXT(130, 32, 76, 9, UI_EL_TEXT, STR_CRAFT, 0),
	TXT(199, 32, 100, 9, UI_EL_TEXT, STR_ARMOR, 0),
	LST(16, 40, 284, 128, UI_EL_LIST, 3, WF_SEL),
};

static const wdef_t w_sarmor[] = {
	WIN(64, 20, 192, 160, UI_EL_WINDOW),
	BTN(90, 156, 140, 16, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(69, 28, 182, 16, UI_EL_TEXT, DYN(0), TC),
	TXT(80, 52, 90, 9, UI_EL_TEXT, STR_TYPE, 0),
	TXT(190, 52, 70, 9, UI_EL_TEXT, STR_QUANTITY_UC, 0),
	LST(81, 68, 152, 80, UI_EL_LIST, 0, WF_SEL),
};

// SoldierArmorState: броня со складом (есть на складе) или бесконечная (STR_NONE)
static void armor_fill(void)
{
	rtab_t t;
	nlst = 0;
	rtab_open(RES_RULE_ARMORS, &t);
	for (uint8_t i = 0; i < t.n && nlst < sizeof lst; i++) {
		uint16_t si = rtab_word(&t, i, offsetof(r_armors_t, store_item));
		if (si == 0xFFFE || (si < MAX_ITEMS && ST->base[ctx.base].items[si])) lst[nlst++] = i;
	}
}

// ---------------------------------------------------------------- таблица

static const scr_t tab[] = {
	SCR(SCR_CRAFT_INFO, UI_SCR_CRAFTINFO, NOUI, RES_BACK14_SCR, 0, w_info),
	SCR(SCR_CRAFT_SOLDIERS, UI_SCR_CRAFTSOLDIERS, NOUI, RES_BACK02_SCR, 0, w_soldiers),
	SCR(SCR_CRAFT_WEAPONS, UI_SCR_CRAFTWEAPONS, NOUI, RES_BACK14_SCR, SF_POPUP, w_weapons),
	SCR(SCR_CRAFT_EQUIP, UI_SCR_CRAFTEQUIPMENT, NOUI, RES_BACK04_SCR, 0, w_equip),
	SCR(SCR_CRAFT_ARMOR, UI_SCR_CRAFTARMOR, NOUI, RES_BACK14_SCR, 0, w_carmor),
	SCR(SCR_SOLDIER_ARMOR, UI_SCR_SOLDIERARMOR, NOUI, RES_BACK14_SCR, SF_POPUP, w_sarmor),
};

static void add(sdef_t *s, wdef_t *w, const wdef_t *src, uint8_t n)
{
	memcpy(&w[s->n], src, n * sizeof(wdef_t));
	s->n += n;
}

uint8_t ship_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	if (id == SCR_CRAFT_INFO) {
		uint8_t nw = (uint8_t)craft_rule(offsetof(r_crafts_t, weapons));
		if (nw >= 1) add(s, w, w_info_w1, 4);
		if (nw >= 2) add(s, w, w_info_w2, 4);
		if (craft_rule(offsetof(r_crafts_t, soldiers)) & 0xFF) add(s, w, w_info_crew, 5);
	} else if (id == SCR_CRAFT_EQUIP) {
		if (ncrew()) add(s, w, w_equip_crew, 2);
		else add(s, w, w_equip_nocrew, 1);
	}
	return 1;
}

void ship_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	rtab_t t;
	base_t *bs = &ST->base[ctx.base];
	switch (id) {
	case SCR_CRAFT_INFO:
		info_text(slot, buf);
		break;
	case SCR_CRAFT_SOLDIERS:
	case SCR_CRAFT_ARMOR:
		if (slot == 0) { craft_name(ctx.craft, t1); str_fmt(buf, str_get(STR_SELECT_SQUAD_FOR_CRAFT), t1, ""); break; }
		if (slot == 1) { fmt_num(t1, space_avail(), 0); str_fmt(buf, str_get(STR_SPACE_AVAILABLE), t1, ""); break; }
		if (slot == 2) { fmt_num(t1, space_used(), 0); str_fmt(buf, str_get(STR_SPACE_USED), t1, ""); break; }
		if (row == LIST_COLS) {
			static const uint8_t cs[] = { 106, 102, 72 }, ca[] = { 114, 69, 101 };
			list_cols(buf, 3, id == SCR_CRAFT_SOLDIERS ? cs : ca, 0);
		} else {
			uint8_t r = soldier_nth(ctx.base, row);
			if (r == NONE8) break;
			soldier_t so;
			uint8_t alt;
			soldier_get(r, &so);
			char *p = soldier_row_start(&so, buf, &alt);
			strcpy(p, so.name);
			strcat(buf, alt ? "\t\x01" : "\t");
			if (id == SCR_CRAFT_SOLDIERS) {
				str_copy(rank_str[so.rank < 6 ? so.rank : 0], buf + strlen(buf), 30);
				strcat(buf, alt ? "\t\x01" : "\t");
				craft_cell(&so, buf + strlen(buf));
			} else {
				craft_cell(&so, buf + strlen(buf));
				strcat(buf, alt ? "\t\x01" : "\t");
				str_copy(rec_name(RES_RULE_ARMORS, so.armor), buf + strlen(buf), 40);
			}
		}
		break;
	case SCR_CRAFT_WEAPONS:
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 94, 50, 36 };
			list_cols(buf, 3, cw, 0);
		} else if (!row) str_copy(STR_NONE_UC, buf, 40);
		else if (row <= nlst) {
			uint8_t wt = lst[row - 1];
			rtab_open(RES_RULE_CRAFTWEAPONS, &t);
			uint16_t l = rtab_word(&t, wt, offsetof(r_craftWeapons_t, launcher)), c = rtab_word(&t, wt, offsetof(r_craftWeapons_t, clip));
			str_copy(rtab_word(&t, wt, 0), buf, 40);
			strcat(buf, "\t");
			fmt_num(buf + strlen(buf), l < MAX_ITEMS ? bs->items[l] : 0, 0);
			strcat(buf, "\t");
			if (c < MAX_ITEMS) fmt_num(buf + strlen(buf), bs->items[c], 0);
			else str_copy(STR_NOT_AVAILABLE, buf + strlen(buf), 30);
		}
		break;
	case SCR_CRAFT_EQUIP:
		if (slot == 0) { craft_name(ctx.craft, t1); str_fmt(buf, str_get(STR_EQUIPMENT_FOR_CRAFT), t1, ""); break; }
		if (slot == 1) { fmt_num(t1, space_avail(), 0); str_fmt(buf, str_get(STR_SPACE_AVAILABLE), t1, ""); break; }
		if (slot == 2) { fmt_num(t1, space_used(), 0); str_fmt(buf, str_get(STR_SPACE_USED), t1, ""); break; }
		if (slot == 5) { str_copy(STR_SOLDIERS_UC, buf, 40); strcat(buf, ">\x01"); fmt_num(buf + strlen(buf), ncrew(), 0); break; }
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 156, 83, 41 };
			list_cols_harrows(buf, 3, cw, 0, 195);
		} else if (row < nlst) {
			uint8_t it = lst[row];
			r_items_t ri;
			rtab_open(RES_RULE_ITEMS, &t);
			rtab_get(&t, it, &ri);
			uint16_t cq = (ri.flags & ITEMS_F_FIXED_WEAPON) ? veh_count(it) : cargo_qty(it);
			uint8_t ammo = ri.battle_type == BT_AMMO;
			char *p = buf;
			if (cq) { strcpy(p, "\x01"); p++; }
			else if (ammo) { p[0] = 3; p[1] = (char)ui_color(UI_EL_AMMOCOLOR, 0); p += 2; }
			if (ammo) { strcpy(p, "  "); p += 2; }
			str_copy(ri.name, p, 40);
			strcat(buf, cq ? "\t\x01" : "\t");
			fmt_num(buf + strlen(buf), bs->items[it], 0);
			strcat(buf, cq ? "\t\x01" : "\t");
			fmt_num(buf + strlen(buf), cq, 0);
		}
		break;
	case SCR_SOLDIER_ARMOR:
		if (slot == 0) {
			soldier_t so;
			soldier_get(sol_rec, &so);
			str_fmt(buf, str_get(STR_SELECT_ARMOR_FOR_SOLDIER), so.name, "");
			break;
		}
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 132, 21 };
			list_cols(buf, 2, cw, 0);
		} else if (row < nlst) {
			rtab_open(RES_RULE_ARMORS, &t);
			uint16_t si = rtab_word(&t, lst[row], offsetof(r_armors_t, store_item));
			str_copy(rtab_word(&t, lst[row], 0), buf, 64);
			if (si < MAX_ITEMS) { strcat(buf, "\t"); fmt_num(buf + strlen(buf), bs->items[si], 0); }
		}
		break;
	}
}

uint8_t ship_rows(uint8_t id, uint8_t slot) __banked
{
	(void)slot;
	switch (id) {
	case SCR_CRAFT_SOLDIERS: case SCR_CRAFT_ARMOR: return soldiers_count(ctx.base, 0xFE, 0);
	case SCR_CRAFT_WEAPONS: return nlst + 1;
	case SCR_CRAFT_EQUIP: case SCR_SOLDIER_ARMOR: return nlst;
	}
	return 0;
}

uint8_t ship_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	craft_t *cr = &ST->craft[ctx.craft];
	switch (id) {
	case SCR_CRAFT_INFO:
		if (ev == EVT_DRAW) info_draw(arg);
		else if (ev == EVT_BUTTON && (arg == 1 || arg == 2)) { weap_slot = arg - 1; UI_GO(A_PUSH, SCR_CRAFT_WEAPONS); }
		break;
	case SCR_CRAFT_SOLDIERS:
		if (ev == EVT_LIST) {                        // CraftSoldiersState::lstSoldiersClick (LEFT)
			uint8_t r = soldier_nth(ctx.base, arg);
			if (r == NONE8) break;
			soldier_t so;
			soldier_get(r, &so);
			if (so.craft == ctx.craft) so.craft = NONE8;
			else if (so.craft != NONE8 && ST->craft[so.craft].status == CS_OUT) break;
			else if (space_avail() > 0 && !so.recovery) so.craft = ctx.craft;
			else break;
			soldier_put(r, &so);
			ui_dirty(1);                             // места и вес
			ui_dirty(2);
			ui_dirty_row(3, arg);
		}
		break;
	case SCR_CRAFT_WEAPONS:
		if (ev == EVT_OPEN) {                        // оружие, пусковая которого есть на складе
			rtab_t t;
			rtab_open(RES_RULE_CRAFTWEAPONS, &t);
			nlst = 0;
			for (uint8_t i = 0; i < t.n; i++) {
				uint16_t l = rtab_word(&t, i, offsetof(r_craftWeapons_t, launcher));
				if (l < MAX_ITEMS && ST->base[ctx.base].items[l]) lst[nlst++] = i;
			}
		} else if (ev == EVT_LIST && arg <= nlst) { weapon_set(arg); UI_GO(A_POP, 0); }
		break;
	case SCR_CRAFT_EQUIP:
		if (ev == EVT_OPEN) equip_fill();
		else if (ev == EVT_BUTTON && arg == 1) {
			strcpy(ui_msg, "INVENTORY\x02\nnot ported yet");
			UI_GO(A_PUSH, SCR_ERROR);
		} else if (ev == EVT_ARROW && arg < nlst) {
			uint16_t ch = ui_arrow_max ? 0x7FFF : 1;
			if (ui_arrow_dir > 0) equip_left(lst[arg], ch); else equip_right(lst[arg], ch);
			// строка и счётчики места; техника тянет за собой строку боеприпасов — весь список
			rtab_t t;
			rtab_open(RES_RULE_ITEMS, &t);
			if (rtab_word(&t, lst[arg], offsetof(r_items_t, flags)) & ITEMS_F_FIXED_WEAPON) ui_dirty(3);
			else ui_dirty_row(3, arg);
			ui_dirty(1); ui_dirty(2); ui_dirty(5);
			return 0;
		}
		break;
	case SCR_CRAFT_ARMOR:
		if (ev == EVT_LIST) {
			uint8_t r = soldier_nth(ctx.base, arg);
			if (r == NONE8) break;
			soldier_t so;
			soldier_get(r, &so);
			if (so.craft != NONE8 && ST->craft[so.craft].status == CS_OUT) break;
			sol_rec = r;
			UI_GO(A_PUSH, SCR_SOLDIER_ARMOR);
		}
		break;
	case SCR_SOLDIER_ARMOR:
		if (ev == EVT_OPEN) armor_fill();
		else if (ev == EVT_LIST && arg < nlst) {     // SoldierArmorState::lstArmorClick
			rtab_t t;
			soldier_t so;
			rtab_open(RES_RULE_ARMORS, &t);
			soldier_get(sol_rec, &so);
			if (so.armor != NONE8) {
				uint16_t old = rtab_word(&t, so.armor, offsetof(r_armors_t, store_item));
				if (old < MAX_ITEMS) ST->base[ctx.base].items[old]++;
			}
			uint16_t si = rtab_word(&t, lst[arg], offsetof(r_armors_t, store_item));
			if (si < MAX_ITEMS && ST->base[ctx.base].items[si]) ST->base[ctx.base].items[si]--;
			so.armor = lst[arg];
			soldier_put(sol_rec, &so);
			UI_GO(A_POP, 0);
		}
		break;
	}
	(void)cr;
	return 0;
}
