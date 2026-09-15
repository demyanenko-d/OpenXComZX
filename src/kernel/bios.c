// Файловые сервисы «BIOS» (общий код). Блок запроса лежит в Win1; эмулятор
// (unreal/Unreal/app/oxz_test.cpp, bios_call) читает его по адресу CPU,
// выполняет команду с физической памятью и пишет статус обратно (сбросив кэш).
#include <stdint.h>
#include "tsconf.h"
#include "res.h"
#include "bios.h"

typedef struct {
	uint8_t cmd, status, slot, game;
	uint8_t page, pad;
	uint16_t offs;
	uint16_t len;
	uint16_t result;
} bios_req_t;

static volatile bios_req_t req;

uint8_t bios_file(uint8_t cmd, uint8_t slot, far_t mem, uint16_t len, uint16_t *result)
{
	uint16_t a = (uint16_t)&req;
	req.cmd = cmd;
	req.status = BIOS_NONE;
	req.slot = slot;
	req.game = res_game();
	req.page = FAR_PAGE(mem);
	req.offs = FAR_OFFS(mem);
	req.len = len;
	req.result = 0;
	OXZ_BIOS_LO = (uint8_t)a;
	OXZ_BIOS_HI = (uint8_t)(a >> 8);
	OXZ_BIOS_CMD = cmd;
	if (result) *result = req.result;
	return req.status;
}
