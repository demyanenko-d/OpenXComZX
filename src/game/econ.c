// Экономика баз (банк 7): солдаты, трюмы, вместимость, содержание, постройки,
// покупка и продажа. Формулы — Base.cpp, PurchaseState.cpp, SellState.cpp,
// BaseView.cpp OpenXcom (tmp/state_model.md §3).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "text.h"
#include "state.h"
#include "game.h"

// ---------------------------------------------------------------- солдаты

#define SOLD ((soldier_t *)0xC000)          // при подключённой SOLDIER_PAGE

void soldier_get(uint8_t i, soldier_t *s) __banked
{
	far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof(soldier_t), s, sizeof *s);
}

void soldier_put(uint8_t i, const soldier_t *s) __banked
{
	far_write(FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof(soldier_t), s, sizeof *s);
}

void soldiers_clear(void) __banked
{
	uint8_t o = pg_map3(SOLDIER_PAGE);
	for (uint8_t i = 0; i < MAX_SOLDIERS; i++) SOLD[i].base = NONE8;
	pg_map3(o);
}

// Сколько солдат: на базе b (NONE8 — везде), на корабле c (0xFE — любой,
// NONE8 — без корабля), с флагами mask (0 — любые). В пути — считаются (как OpenXcom).
uint8_t soldiers_count(uint8_t b, uint8_t c, uint8_t mask) __banked
{
	uint8_t n = ST->nsoldiers, cnt = 0;
	uint8_t o = pg_map3(SOLDIER_PAGE);
	for (uint8_t i = 0; i < n; i++) {
		const soldier_t *s = &SOLD[i];
		uint8_t sb = s->base, sc = s->craft;
		if (sb == NONE8) continue;
		if (b != NONE8 && sb != b) continue;
		if (c != 0xFE && sc != c) continue;
		if (mask && !(s->flags & mask)) continue;
		cnt++;
	}
	pg_map3(o);
	return cnt;
}

// i-й солдат базы b (порядок записей) -> номер записи или NONE8.
uint8_t soldier_nth(uint8_t b, uint8_t k) __banked
{
	uint8_t n = ST->nsoldiers, r = NONE8;
	uint8_t o = pg_map3(SOLDIER_PAGE);
	for (uint8_t i = 0; i < n; i++) {
		uint8_t sb = SOLD[i].base;
		if (sb != b) continue;
		if (!k) { r = i; break; }
		k--;
	}
	pg_map3(o);
	return r;
}

// Строка k пула p из ресурса NAMES (Names.cs) -> buf.
static void pool_name(far_t base, uint16_t off, uint16_t skip, char *buf)
{
	char c[32];
	far_t p = base + off;
	for (;;) {
		far_read(p, c, sizeof c);
		uint8_t n = 0;
		while (n < sizeof c && c[n]) n++;
		if (n == sizeof c) { p += n; continue; }  // длиннее — пропустить кусок
		if (!skip) { memcpy(buf, c, n + 1); return; }
		skip--;
		p += n + 1;
	}
}

// Mod::genSoldier / Soldier::Soldier: характеристики RNG(min, max), храбрость
// кратна 10, имя из случайного пула по полу. Возвращает запись или NONE8.
uint8_t soldier_new(uint8_t base) __banked
{
	rtab_t t;
	r_soldiers_t rs;
	soldier_t s;
	res_t r;
	uint8_t slot = NONE8, n = ST->nsoldiers;
	{
		uint8_t o = pg_map3(SOLDIER_PAGE);
		for (uint8_t i = 0; i < MAX_SOLDIERS; i++)
			if (SOLD[i].base == NONE8) { slot = i; break; }
		pg_map3(o);
	}
	if (slot == NONE8) return NONE8;
	memset(&s, 0, sizeof s);
	rtab_open(RES_RULE_SOLDIERS, &t);
	rtab_get(&t, 0, &rs);
	uint8_t *mn = (uint8_t *)&rs.min_stats, *mx = (uint8_t *)&rs.max_stats, *st = (uint8_t *)&s.init;
	for (uint8_t k = 0; k < 11; k++) st[k] = (uint8_t)rng_range(mn[k], mx[k]);
	s.init.bravery = (uint8_t)(rng_range(rs.min_stats.bravery / 10, rs.max_stats.bravery / 10) * 10);
	s.init.psi_skill = rs.min_stats.psi_skill;
	s.cur = s.init;
	s.base = base;
	s.craft = NONE8;
	s.armor = rs.armor == RNONE ? NONE8 : (uint8_t)rs.armor;
	s.id = ++ST->ids[ID_SOLDIER];
	uint8_t female = rng_range(1, 100) <= rs.female_frequency;
	s.look = (uint8_t)rng_range(0, 3) | (female ? SF_FEMALE : 0);
	strcpy(s.name, "Soldier");
	if (res_find(RES_NAMES, &r)) {
		uint8_t np = far_byte(r.phys), cnt[4];
		if (np) {
			uint8_t p = (uint8_t)rng_range(0, np - 1);
			uint16_t off = far_word(r.phys + 1 + p * 2);
			far_read(r.phys + off, cnt, 4);
			// порядок: maleFirst, femaleFirst, maleLast, femaleLast; нет женских — мужские
			uint8_t fi = female && cnt[1] ? 1 : 0, la = female && cnt[3] ? 3 : 2;
			uint16_t skipf = 0, skipl = 0;
			for (uint8_t k = 0; k < fi; k++) skipf += cnt[k];
			for (uint8_t k = 0; k < la; k++) skipl += cnt[k];
			char first[32], last[32];
			first[0] = last[0] = 0;
			if (cnt[fi]) pool_name(r.phys, off + 4, skipf + rng_range(0, cnt[fi] - 1), first);
			if (cnt[la]) pool_name(r.phys, off + 4, skipl + rng_range(0, cnt[la] - 1), last);
			uint8_t lf = (uint8_t)strlen(first), ll = (uint8_t)strlen(last);
			if (lf + 1 + ll < SOLDIER_NAME && lf) {
				strcpy(s.name, first);
				if (ll) { strcat(s.name, " "); strcat(s.name, last); }
			}
		}
	}
	soldier_put(slot, &s);
	if (slot >= n) ST->nsoldiers = slot + 1;
	return slot;
}

void soldier_delete(uint8_t i) __banked
{
	soldier_t s;
	soldier_get(i, &s);
	s.base = NONE8;
	soldier_put(i, &s);
	while (ST->nsoldiers) {
		soldier_get(ST->nsoldiers - 1, &s);
		if (s.base != NONE8) break;
		ST->nsoldiers--;
	}
}

// ---------------------------------------------------------------- трюмы

uint8_t cargo_alloc(void) __banked
{
	for (uint8_t i = 0; i < MAX_CARGO; i++) {
		uint8_t used = 0;
		for (uint8_t c = 0; c < MAX_CRAFTS; c++)
			if (ST->craft[c].type != NONE8 && ST->craft[c].cargo == i) { used = 1; break; }
		if (used) continue;
		for (uint8_t k = 0; k < CARGO_ITEMS; k++) ST->cargo[i].it[k].item = NONE8;
		for (uint8_t k = 0; k < CARGO_VEH; k++) ST->cargo[i].veh[k].type = NONE8;
		return i;
	}
	return NONE8;
}

void cargo_add(uint8_t cg, uint8_t item, uint16_t qty) __banked
{
	if (cg == NONE8) return;
	cargo_t *c = &ST->cargo[cg];
	uint8_t freek = NONE8;
	for (uint8_t k = 0; k < CARGO_ITEMS; k++) {
		uint8_t it = c->it[k].item;
		if (it == item) { uint16_t q = c->it[k].qty + qty; c->it[k].qty = q > 255 ? 255 : (uint8_t)q; return; }
		if (it == NONE8 && freek == NONE8) freek = k;
	}
	if (freek != NONE8) { c->it[freek].item = item; c->it[freek].qty = qty > 255 ? 255 : (uint8_t)qty; }
}

// ---------------------------------------------------------------- правила

static rtab_t t_fac, t_items;

static void fac_rule(uint8_t type, r_facilities_t *f)
{
	rtab_open(RES_RULE_FACILITIES, &t_fac);
	rtab_get(&t_fac, type, f);
}

static int16_t item_size(uint8_t it)
{
	return (int16_t)rtab_word(&t_items, it, offsetof(r_items_t, size));
}

// ---------------------------------------------------------------- вместимость

// Base::getTotalScientists / getTotalEngineers: свободные + в пути + в работе.
void base_personnel(uint8_t b, uint16_t *sci, uint16_t *eng) __banked
{
	uint16_t s = ST->base[b].scientists, e = ST->base[b].engineers;
	for (uint8_t i = 0; i < MAX_TRANSFERS; i++) {
		transfer_t *tr = &ST->transfer[i];
		if (tr->base != b) continue;
		if (tr->kind == TK_SCIENTIST) s += tr->qty;
		else if (tr->kind == TK_ENGINEER) e += tr->qty;
	}
	for (uint8_t i = 0; i < MAX_RESEARCH; i++) if (ST->research[i].base == b) s += ST->research[i].assigned;
	for (uint8_t i = 0; i < MAX_PRODS; i++) if (ST->prod[i].base == b) e += ST->prod[i].engineers;
	*sci = s;
	*eng = e;
}

// Base::getAvailable* (только готовые постройки) и getUsed*.
void base_caps(uint8_t b, caps_t *a, caps_t *u) __banked
{
	r_facilities_t f;
	base_t *bs = &ST->base[b];
	memset(a, 0, sizeof *a);
	memset(u, 0, sizeof *u);
	rtab_open(RES_RULE_FACILITIES, &t_fac);
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		uint8_t type = bs->fac[i].type;
		if (type == NONE8 || bs->fac[i].days) continue;
		rtab_get(&t_fac, type, &f);
		a->quarters += f.personnel;
		a->stores += (uint32_t)f.storage * 100;
		a->labs += f.labs;
		a->workshops += f.workshops;
		a->hangars += f.crafts;
		a->psi += f.psi_labs;
		a->aliens += f.aliens;
	}
	// персонал: солдаты (и в пути), учёные и инженеры (свободные, в пути, в работе)
	uint16_t sci, eng;
	base_personnel(b, &sci, &eng);
	rtab_t tm;
	rtab_open(RES_RULE_MANUFACTURE, &tm);
	for (uint8_t i = 0; i < MAX_RESEARCH; i++)
		if (ST->research[i].base == b) u->labs += ST->research[i].assigned;
	for (uint8_t i = 0; i < MAX_PRODS; i++)
		if (ST->prod[i].base == b)
			u->workshops += ST->prod[i].engineers + rtab_word(&tm, ST->prod[i].manuf, offsetof(r_manufacture_t, space));
	u->quarters = soldiers_count(b, 0xFE, 0) + sci + eng;
	u->psi = soldiers_count(b, 0xFE, SF_PSI);
	// склад: предметы * размер (сотые) + трюмы кораблей базы + поставки предметов
	rtab_open(RES_RULE_ITEMS, &t_items);
	uint32_t st = 0;
	for (uint8_t i = 0; i < MAX_ITEMS && i < t_items.n; i++)
		if (bs->items[i]) {
			st += (uint32_t)bs->items[i] * (uint16_t)item_size(i);
			if (rtab_word(&t_items, i, offsetof(r_items_t, flags)) & ITEMS_F_LIVE_ALIEN) u->aliens += bs->items[i];
		}
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->base != b) continue;
		u->hangars++;
		if (cr->cargo == NONE8) continue;
		cargo_t *cg = &ST->cargo[cr->cargo];
		for (uint8_t k = 0; k < CARGO_ITEMS; k++)
			if (cg->it[k].item != NONE8) st += (uint32_t)cg->it[k].qty * (uint16_t)item_size(cg->it[k].item);
		for (uint8_t k = 0; k < CARGO_VEH; k++)
			if (cg->veh[k].type != NONE8) st += (uint16_t)item_size(cg->veh[k].type);
	}
	for (uint8_t i = 0; i < MAX_TRANSFERS; i++) {
		transfer_t *tr = &ST->transfer[i];
		if (tr->base == b && tr->kind == TK_ITEM) st += (uint32_t)tr->qty * (uint16_t)item_size(tr->item);
	}
	u->stores = st;
}

// Base::getMonthlyMaintenace: аренда кораблей + зарплата + содержание построек.
void base_costs(uint8_t b, costs_t *c) __banked
{
	rtab_t t;
	r_facilities_t f;
	memset(c, 0, sizeof *c);
	rtab_open(RES_RULE_CRAFTS, &t);
	for (uint8_t i = 0; i < MAX_CRAFTS; i++) {
		craft_t *cr = &ST->craft[i];
		if (cr->type == NONE8 || cr->base != b) continue;
		uint32_t rent;
		far_read(t.base + 8 + (uint32_t)cr->type * t.size + offsetof(r_crafts_t, cost_rent), &rent, 4);
		c->crafts += rent;
	}
	rtab_open(RES_RULE_SOLDIERS, &t);
	uint32_t salary;
	far_read(t.base + 8 + offsetof(r_soldiers_t, cost_salary), &salary, 4);
	c->soldiers = salary * soldiers_count(b, 0xFE, 0);
	uint16_t sci, eng;
	base_personnel(b, &sci, &eng);
	c->engineers = (uint32_t)eng * gv.cost_engineer;
	c->scientists = (uint32_t)sci * gv.cost_scientist;
	rtab_open(RES_RULE_FACILITIES, &t_fac);
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		uint8_t type = ST->base[b].fac[i].type;
		if (type == NONE8 || ST->base[b].fac[i].days) continue;
		rtab_get(&t_fac, type, &f);
		c->facilities += f.monthly_cost;
	}
	c->total = c->crafts + c->soldiers + c->engineers + c->scientists + c->facilities;
}

int32_t total_maintenance(void) __banked
{
	costs_t c;
	int32_t sum = 0;
	for (uint8_t b = 0; b < MAX_BASES; b++)
		if (ST->base[b].name[0]) { base_costs(b, &c); sum += c.total; }
	return sum;
}

void funds_add(int32_t v) __banked          // SavedGame::setFunds: приход/расход месяца
{
	uint8_t m = ST->hist_len - 1;
	if (v > 0) ST->fin.income[m] += v;
	else ST->fin.expenditure[m] -= v;
	ST->funds += v;
}

// ---------------------------------------------------------------- постройки

uint8_t fac_at(uint8_t b, uint8_t x, uint8_t y) __banked
{
	r_facilities_t f;
	base_t *bs = &ST->base[b];
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		uint8_t type = bs->fac[i].type;
		if (type == NONE8) continue;
		fac_rule(type, &f);
		uint8_t fx = bs->fac[i].xy & 15, fy = bs->fac[i].xy >> 4;
		if (x >= fx && x < fx + f.size && y >= fy && y < fy + f.size) return i;
	}
	return NONE8;
}

// Сетка занятости: клетка -> номер постройки или NONE8.
static uint8_t grid[BASE_SIZE * BASE_SIZE];

static void make_grid(uint8_t b)
{
	r_facilities_t f;
	memset(grid, NONE8, sizeof grid);
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		uint8_t type = ST->base[b].fac[i].type;
		if (type == NONE8) continue;
		fac_rule(type, &f);
		uint8_t fx = ST->base[b].fac[i].xy & 15, fy = ST->base[b].fac[i].xy >> 4;
		for (uint8_t y = fy; y < fy + f.size && y < BASE_SIZE; y++)
			for (uint8_t x = fx; x < fx + f.size && x < BASE_SIZE; x++) grid[y * BASE_SIZE + x] = i;
	}
}

// BaseView::isPlaceable: все клетки в сетке и свободны, хотя бы одна соседняя
// клетка — готовая постройка.
uint8_t fac_can_place(uint8_t b, uint8_t type, uint8_t x, uint8_t y) __banked
{
	r_facilities_t f;
	fac_rule(type, &f);
	if (x + f.size > BASE_SIZE || y + f.size > BASE_SIZE) return 0;
	make_grid(b);
	uint8_t touch = 0;
	for (uint8_t yy = y; yy < y + f.size; yy++)
		for (uint8_t xx = x; xx < x + f.size; xx++) {
			if (grid[yy * BASE_SIZE + xx] != NONE8) return 0;
			static const int8_t dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
			for (uint8_t d = 0; d < 4; d++) {
				int8_t nx = (int8_t)xx + dx[d], ny = (int8_t)yy + dy[d];
				if (nx < 0 || ny < 0 || nx >= BASE_SIZE || ny >= BASE_SIZE) continue;
				uint8_t g = grid[ny * BASE_SIZE + nx];
				if (g != NONE8 && !ST->base[b].fac[g].days) touch = 1;
			}
		}
	return touch;
}

// Новая постройка: деньги списываются, дней = buildTime. 0 — нет места/денег.
uint8_t fac_build(uint8_t b, uint8_t type, uint8_t x, uint8_t y) __banked
{
	r_facilities_t f;
	if (!fac_can_place(b, type, x, y)) return 0;
	fac_rule(type, &f);
	if (ST->funds < (int32_t)f.build_cost) return 0;
	for (uint8_t i = 0; i < MAX_FACILITIES; i++)
		if (ST->base[b].fac[i].type == NONE8) {
			ST->base[b].fac[i].type = type;
			ST->base[b].fac[i].xy = x | (y << 4);
			ST->base[b].fac[i].days = f.build_time > 255 ? 255 : (uint8_t)f.build_time;
			funds_add(-(int32_t)f.build_cost);
			return 1;
		}
	return 0;
}

// BaseFacility::inUse: готовая постройка, без которой вместимость станет меньше занятого.
static uint8_t fac_in_use(uint8_t b, uint8_t fi)
{
	r_facilities_t f;
	caps_t a, u;
	if (ST->base[b].fac[fi].days) return 0;
	fac_rule(ST->base[b].fac[fi].type, &f);
	base_caps(b, &a, &u);
	return (f.personnel && a.quarters - f.personnel < u.quarters)
	    || (f.storage && a.stores - (uint32_t)f.storage * 100 < u.stores)
	    || (f.labs && a.labs - f.labs < u.labs)
	    || (f.workshops && a.workshops - f.workshops < u.workshops)
	    || (f.crafts && a.hangars - f.crafts < u.hangars)
	    || (f.psi_labs && a.psi - f.psi_labs < u.psi)
	    || (f.aliens && a.aliens - f.aliens < u.aliens);
}

// Base::getDisconnectedFacilities: заливка от лифта без fi; дальше идёт только
// из готовых построек. 1 — кто-то отрезан.
static uint8_t fac_cuts(uint8_t b, uint8_t fi)
{
	r_facilities_t f;
	uint8_t seen[BASE_SIZE * BASE_SIZE], stack[BASE_SIZE * BASE_SIZE], sp = 0, lift = NONE8;
	make_grid(b);
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		uint8_t type = ST->base[b].fac[i].type;
		if (type == NONE8) continue;
		fac_rule(type, &f);
		if (f.flags & FACILITIES_F_LIFT) { lift = i; break; }
	}
	if (lift == NONE8 || lift == fi) return 1;
	memset(seen, 0, sizeof seen);
	uint8_t c0 = (ST->base[b].fac[lift].xy >> 4) * BASE_SIZE + (ST->base[b].fac[lift].xy & 15);
	stack[sp++] = c0;
	seen[c0] = 1;
	while (sp) {
		uint8_t c = stack[--sp], g = grid[c];
		if (ST->base[b].fac[g].days) continue;     // недостроенная — лист
		uint8_t x = c % BASE_SIZE, y = c / BASE_SIZE;
		static const int8_t dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
		for (uint8_t d = 0; d < 4; d++) {
			int8_t nx = (int8_t)x + dx[d], ny = (int8_t)y + dy[d];
			if (nx < 0 || ny < 0 || nx >= BASE_SIZE || ny >= BASE_SIZE) continue;
			uint8_t n = ny * BASE_SIZE + nx, gn = grid[n];
			if (seen[n] || gn == NONE8 || gn == fi) continue;
			seen[n] = 1;
			stack[sp++] = n;
		}
	}
	for (uint8_t c = 0; c < BASE_SIZE * BASE_SIZE; c++)
		if (grid[c] != NONE8 && grid[c] != fi && !seen[c]) return 1;
	return 0;
}

// Можно ли снести: FAC_OK / FAC_IN_USE / FAC_CUTS (BasescapeState::viewClick).
uint8_t fac_can_dismantle(uint8_t b, uint8_t fi) __banked
{
	if (fac_in_use(b, fi)) return FAC_IN_USE;
	if (fac_cuts(b, fi)) return FAC_CUTS;
	return FAC_OK;
}

void fac_dismantle(uint8_t b, uint8_t fi) __banked     // без возврата денег
{
	ST->base[b].fac[fi].type = NONE8;
}

// ---------------------------------------------------------------- покупка, продажа

void transfer_add(uint8_t b, uint8_t kind, uint8_t item, uint16_t qty, uint8_t hours) __banked
{
	uint8_t freei = NONE8;
	for (uint8_t i = 0; i < MAX_TRANSFERS; i++) {
		transfer_t *t = &ST->transfer[i];
		if (t->base == NONE8) { if (freei == NONE8) freei = i; continue; }
		if (t->base == b && t->kind == kind && t->item == item && t->hours == hours) { t->qty += qty; return; }
	}
	if (freei == NONE8) return;
	transfer_t *t = &ST->transfer[freei];
	t->base = b; t->kind = kind; t->item = item; t->qty = qty; t->hours = hours;
}

// Цена одной штуки (PurchaseState / SellState).
int32_t price(uint8_t kind, uint8_t what, uint8_t sell) __banked
{
	rtab_t t;
	uint32_t v = 0;
	switch (kind) {
	case BUY_SOLDIER:
		if (sell) return 0;
		rtab_open(RES_RULE_SOLDIERS, &t);
		far_read(t.base + 8 + offsetof(r_soldiers_t, cost_buy), &v, 4);
		return (int32_t)v;
	case BUY_SCIENTIST: return sell ? 0 : (int32_t)gv.cost_scientist * 2;
	case BUY_ENGINEER: return sell ? 0 : (int32_t)gv.cost_engineer * 2;
	case BUY_CRAFT:
		rtab_open(RES_RULE_CRAFTS, &t);
		far_read(t.base + 8 + (uint32_t)what * t.size + (sell ? offsetof(r_crafts_t, cost_sell) : offsetof(r_crafts_t, cost_buy)), &v, 4);
		return (int32_t)v;
	case BUY_ITEM:
		rtab_open(RES_RULE_ITEMS, &t);
		far_read(t.base + 8 + (uint32_t)what * t.size + (sell ? offsetof(r_items_t, cost_sell) : offsetof(r_items_t, cost_buy)), &v, 4);
		return (int32_t)v;
	}
	return 0;
}

// Новый корабль на базе b (покупка — в пути transit часов, производство — 0):
// статус «заправка», без оружия, трюм для транспортов. Номер записи или NONE8.
uint8_t craft_add(uint8_t b, uint8_t type, uint8_t transit) __banked
{
	rtab_t t;
	rtab_open(RES_RULE_CRAFTS, &t);
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type != NONE8) continue;
		memset(cr, 0, sizeof *cr);
		cr->type = type;
		cr->base = b;
		cr->num = ++ST->ids[ID_CRAFT + type];
		cr->status = CS_REFUEL;
		cr->weap[0].type = cr->weap[1].type = NONE8;
		cr->transit = transit;
		cr->cargo = rtab_word(&t, type, offsetof(r_crafts_t, soldiers)) & 0xFF ? cargo_alloc() : NONE8;
		cr->dest_kind = DK_NONE;
		return c;
	}
	return NONE8;
}

// PurchaseState::btnOkClick: деньги, поставки (солдаты и корабли — сразу с transit).
void econ_buy(uint8_t b, uint8_t kind, uint8_t what, uint16_t qty) __banked
{
	rtab_t t;
	if (!qty) return;
	funds_add(-price(kind, what, 0) * (int32_t)qty);
	switch (kind) {
	case BUY_SOLDIER: {
		rtab_open(RES_RULE_SOLDIERS, &t);
		uint8_t h = (uint8_t)rtab_word(&t, 0, offsetof(r_soldiers_t, transfer_time));
		if (!h) h = (uint8_t)gv.time_personnel;
		for (uint16_t i = 0; i < qty; i++) {
			uint8_t s = soldier_new(b);
			if (s == NONE8) break;
			soldier_t so;
			soldier_get(s, &so);
			so.transit = h;
			soldier_put(s, &so);
		}
		break;
	}
	case BUY_SCIENTIST: transfer_add(b, TK_SCIENTIST, 0, qty, (uint8_t)gv.time_personnel); break;
	case BUY_ENGINEER: transfer_add(b, TK_ENGINEER, 0, qty, (uint8_t)gv.time_personnel); break;
	case BUY_CRAFT:
		rtab_open(RES_RULE_CRAFTS, &t);
		for (uint16_t i = 0; i < qty; i++)
			craft_add(b, what, (uint8_t)rtab_word(&t, what, offsetof(r_crafts_t, transfer_time)));
		break;
	case BUY_ITEM: {
		rtab_open(RES_RULE_ITEMS, &t);
		uint8_t h = (uint8_t)rtab_word(&t, what, offsetof(r_items_t, transfer_time));
		transfer_add(b, TK_ITEM, what, qty, h ? h : 24);
		break;
	}
	}
}

// Craft::unload: оружие (пусковые + обоймы), трюм, техника — на склад базы.
static void craft_unload(uint8_t c)
{
	rtab_t tw, ti;
	craft_t *cr = &ST->craft[c];
	base_t *bs = &ST->base[cr->base];
	rtab_open(RES_RULE_CRAFTWEAPONS, &tw);
	rtab_open(RES_RULE_ITEMS, &ti);
	for (uint8_t k = 0; k < 2; k++) {
		uint8_t w = cr->weap[k].type;
		if (w == NONE8) continue;
		uint16_t launcher = rtab_word(&tw, w, offsetof(r_craftWeapons_t, launcher));
		uint16_t clip = rtab_word(&tw, w, offsetof(r_craftWeapons_t, clip));
		if (launcher < MAX_ITEMS) bs->items[launcher]++;
		if (clip < MAX_ITEMS) {
			int16_t cs = (int16_t)rtab_word(&ti, clip, offsetof(r_items_t, clip_size));
			if (cs > 0) bs->items[clip] += cr->weap[k].ammo / cs;
		}
	}
	if (cr->cargo != NONE8) {
		cargo_t *cg = &ST->cargo[cr->cargo];
		for (uint8_t k = 0; k < CARGO_ITEMS; k++)
			if (cg->it[k].item != NONE8) { bs->items[cg->it[k].item] += cg->it[k].qty; cg->it[k].item = NONE8; }
		for (uint8_t k = 0; k < CARGO_VEH; k++)
			if (cg->veh[k].type != NONE8) { bs->items[cg->veh[k].type]++; cg->veh[k].type = NONE8; }
	}
}

// Base::removeCraft(craft, true): груз и оружие — на склад, экипаж — с корабля.
void craft_remove(uint8_t c) __banked
{
	craft_unload(c);
	uint8_t n = ST->nsoldiers, o = pg_map3(SOLDIER_PAGE);
	for (uint8_t i = 0; i < n; i++) if (SOLD[i].craft == c) SOLD[i].craft = NONE8;
	pg_map3(o);
	ST->craft[c].type = NONE8;
}

// Base::removeCraft(craft, false) после гибели в бою: экипаж погиб (killSoldier),
// груз, техника и оружие потеряны.
void craft_lost(uint8_t c) __banked
{
	uint8_t n = ST->nsoldiers;
	for (uint8_t i = 0; i < n; i++) {
		soldier_t s;
		soldier_get(i, &s);
		if (s.base != NONE8 && s.craft == c) soldier_delete(i);
	}
	ST->craft[c].type = NONE8;
}

// SellState::btnOkClick. what — предмет / тип корабля / номер записи (солдат, корабль).
void econ_sell(uint8_t b, uint8_t kind, uint8_t what, uint16_t qty) __banked
{
	if (!qty) return;
	base_t *bs = &ST->base[b];
	switch (kind) {
	case SELL_SOLDIER: {                              // what — запись солдата
		soldier_t s;
		soldier_get(what, &s);
		if (s.armor != NONE8) {
			rtab_t ta;
			rtab_open(RES_RULE_ARMORS, &ta);
			uint16_t si = rtab_word(&ta, s.armor, offsetof(r_armors_t, store_item));
			if (si < MAX_ITEMS) bs->items[si]++;
		}
		soldier_delete(what);
		break;
	}
	case SELL_CRAFT:                                  // what — запись корабля
		funds_add(price(BUY_CRAFT, ST->craft[what].type, 1));
		craft_remove(what);
		break;
	case BUY_SCIENTIST: bs->scientists -= qty > bs->scientists ? bs->scientists : qty; break;
	case BUY_ENGINEER: bs->engineers -= qty > bs->engineers ? bs->engineers : qty; break;
	case BUY_ITEM:
		if (qty > bs->items[what]) qty = bs->items[what];
		bs->items[what] -= qty;
		funds_add(price(BUY_ITEM, what, 1) * (int32_t)qty);
		break;
	}
}

// ---------------------------------------------------------------- база

// Новая база: запись, место и имя; для первой — стартовая уже заполнена.
uint8_t base_alloc(void) __banked
{
	for (uint8_t b = 0; b < MAX_BASES; b++)
		if (!ST->base[b].name[0]) {
			base_t *bs = &ST->base[b];
			if (b || ST->months >= 0) {       // не стартовая: пустая база
				memset(bs, 0, sizeof *bs);
				for (uint8_t f = 0; f < MAX_FACILITIES; f++) bs->fac[f].type = NONE8;
			}
			return b;
		}
	return NONE8;
}

// Base::~Base (BaseDestroyedState): база со всем, что к ней относится — корабли (и в
// полёте), солдаты, поставки, исследования, производство, склад
void base_remove(uint8_t b) __banked
{
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->base != b) continue;
		if (cr->status == CS_OUT && ST->ncraft_out) ST->ncraft_out--;
		cr->type = NONE8;
	}
	uint8_t n = ST->nsoldiers;
	for (uint8_t i = 0; i < n; i++) {
		soldier_t s;
		soldier_get(i, &s);
		if (s.base == b) soldier_delete(i);
	}
	for (uint8_t i = 0; i < MAX_TRANSFERS; i++) if (ST->transfer[i].base == b) ST->transfer[i].base = NONE8;
	for (uint8_t i = 0; i < MAX_RESEARCH; i++) if (ST->research[i].base == b) ST->research[i].base = NONE8;
	for (uint8_t i = 0; i < MAX_PRODS; i++) if (ST->prod[i].base == b) ST->prod[i].base = NONE8;
	ST->base[b].name[0] = 0;
	if (ST->base[ST->sel_base].name[0]) return;  // выбранная база осталась
	for (uint8_t i = 0; i < MAX_BASES; i++) if (ST->base[i].name[0]) { ST->sel_base = i; break; }
}

// GeoscapeState::handleBaseDefense: Base::getAvailableSoldiers(true) или техника —
// солдат на базе без ран или на корабле не в полёте; техника на складе или в трюме
uint8_t base_defenders(uint8_t b) __banked
{
	uint8_t n = ST->nsoldiers;
	for (uint8_t i = 0; i < n; i++) {
		soldier_t s;
		soldier_get(i, &s);
		if (s.base != b || s.transit) continue;
		if (s.craft != NONE8 ? ST->craft[s.craft].status != CS_OUT : !s.recovery) return 1;
	}
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	for (uint8_t i = 0; i < MAX_ITEMS && i < t.n; i++)
		if (ST->base[b].items[i] && (rtab_word(&t, i, offsetof(r_items_t, flags)) & ITEMS_F_FIXED_WEAPON)) return 1;
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->base != b || cr->status == CS_OUT || cr->transit || cr->cargo == NONE8) continue;
		for (uint8_t k = 0; k < CARGO_VEH; k++) if (ST->cargo[cr->cargo].veh[k].type != NONE8) return 1;
	}
	return 0;
}

uint8_t bases_count(void) __banked
{
	uint8_t n = 0;
	for (uint8_t b = 0; b < MAX_BASES; b++) if (ST->base[b].name[0]) n++;
	return n;
}
