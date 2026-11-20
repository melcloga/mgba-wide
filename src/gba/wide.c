/* mgba-wide: per-game widescreen fixes
 *
 * Each supported game gets a small set of ROM patches that make it draw extra scenery past
 * the original 240px screen, plus emulator-side helpers (like shadow maps) it needs.
 * Patches are applied in memory only when widescreen is on; the ROM file is never changed.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/wide.h>

#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/memory.h>

mLOG_DECLARE_CATEGORY(GBA_WIDE);
mLOG_DEFINE_CATEGORY(GBA_WIDE, "GBA Widescreen", "gba.wide");

/* Star Wars: Episode III - Revenge of the Sith (USA)
 *
 * Levels use 256px-wide BG maps streamed one 8px column at a time by the routine at
 * 0x080A9D98: moving right it draws camera column + 30, moving left it draws the camera
 * column. Push those out by 4 columns each way so the 22px side margins get real scenery.
 * The extra columns land in the 32-column VRAM ring on top of visible ones, so this fix
 * relies on shadow maps to keep 64 columns.
 */
#ifdef MGBA_WIDE_EXPERIMENTAL
static const struct GBAWidePatch _sw3Patches[] = {
	// right: add r0, #30  ->  add r0, #34
	{ 0x080A9DB4, 0x301E, 0x3022 },
	// left: ldrh r1, [r5, #8]; ldr r0, =0xFFFF0000; and r3, r0
	//   ->  ldrh r0, [r5, #8]; sub r0, #4; b 0x080A9DB6 (shared mask/merge code of the right path)
	{ 0x080A9DC8, 0x8929, 0x8928 },
	{ 0x080A9DCA, 0x4841, 0x3804 },
	{ 0x080A9DCC, 0x4003, 0xE7F3 },
};
#endif

static const struct GBAWideGame _games[] = {
#ifdef MGBA_WIDE_EXPERIMENTAL
	/* Disabled: scrolling margins look right, but the game's tile cache frees the tiles of
	 * whatever map slot it overwrites, so the extra columns end up freeing tiles that are
	 * still on screen and the scenery corrupts after a while. Needs a bigger engine patch. */
	{ { 'B', 'E', '3', 'E' }, 0, "Star Wars: Episode III - Revenge of the Sith (USA)", _sw3Patches, sizeof(_sw3Patches) / sizeof(*_sw3Patches), true },
#endif
	{ { 0, 0, 0, 0 }, 0, NULL, NULL, 0, false }
};

const struct GBAWideGame* GBAWideFindGame(const struct GBA* gba) {
	if (!gba->memory.rom || gba->memory.romSize < 0xC0) {
		return NULL;
	}
	const struct GBACartridge* cart = (const struct GBACartridge*) gba->memory.rom;
	const uint8_t* header = (const uint8_t*) gba->memory.rom;
	size_t i;
	for (i = 0; i < sizeof(_games) / sizeof(*_games); ++i) {
		if (_games[i].title && memcmp(&cart->id, _games[i].code, 4) == 0 && header[0xBC] == _games[i].version) {
			return &_games[i];
		}
	}
	return NULL;
}

static uint16_t _romRead16(const struct GBA* gba, uint32_t address) {
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	uint32_t offset = address & 0x01FFFFFE;
	if (offset + 1 >= gba->memory.romSize) {
		return 0;
	}
	return rom[offset] | (rom[offset + 1] << 8);
}

const struct GBAWideGame* GBAWideApply(struct GBA* gba, bool enable) {
	const struct GBAWideGame* game = GBAWideFindGame(gba);
	if (!game) {
		return NULL;
	}
	size_t i;
	uint16_t want;
	// Only touch the ROM if every patch site holds either the original or the patched value
	for (i = 0; i < game->nPatches; ++i) {
		uint16_t value = _romRead16(gba, game->patches[i].address);
		if (value != game->patches[i].original && value != game->patches[i].patched) {
			mLOG(GBA_WIDE, WARN, "%s: unexpected data at 0x%08X, widescreen fix skipped", game->title, game->patches[i].address);
			return NULL;
		}
	}
	for (i = 0; i < game->nPatches; ++i) {
		want = enable ? game->patches[i].patched : game->patches[i].original;
		if (_romRead16(gba, game->patches[i].address) != want) {
			int16_t old;
			GBAPatch16(gba->cpu, game->patches[i].address, want, &old);
		}
	}
	mLOG(GBA_WIDE, INFO, "%s: widescreen fix %s", game->title, enable ? "on" : "off");
	return game;
}
