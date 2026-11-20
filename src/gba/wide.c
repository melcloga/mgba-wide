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
#include <mgba/internal/gba/renderers/video-software.h>

mLOG_DECLARE_CATEGORY(GBA_WIDE);
mLOG_DEFINE_CATEGORY(GBA_WIDE, "GBA Widescreen", "gba.wide");

/* Star Wars: Episode III - Revenge of the Sith (USA)
 *
 * The level engine streams scenery into 32x32-tile BG maps one 8px column at a time, and keeps
 * a reference-counted cache of the tiles those maps use (VRAM is otherwise full of tiles).
 * Overwriting a map slot frees the tile it held, so the map has to be wide enough for every
 * column on screen plus the ones being loaded.
 *
 * The fix gives each scrolling layer a 64-column map (one linear 64x32 block; the renderer is
 * told to read it that way) and streams 4 extra columns on each side:
 *  - layer setup asks for 64x32 maps instead of 32x32; VRAM has no room for them (it's full of
 *    tiles), so the map allocator hands out EWRAM instead and the renderer reads maps from there
 *  - the per-layer column wrap mask goes from 31 to 63 (row mask stays 31)
 *  - the ARM map writer/eraser routines (copied to IWRAM at boot) use a 128-byte row stride
 *  - columns are streamed at camera + 34 / camera - 4 instead of camera + 30 / camera
 */
static const struct GBAWidePatch _sw3Patches[] = {
	// Map allocator (0x080A8AA0) rewritten from 0x080A8AB0: maps still reserve space from the
	// game's VRAM allocator (below its usual floor, so it never touches the tile cache), but the
	// map data lives in EWRAM at 0x02000000 + VRAM offset * 2 (0x02018000-0x0201FFFF for the
	// usual 4 maps), and BGCNT gets size 1 for 64x32 maps so the renderer knows to look there.
	{ 0x080A8AB0, 0x039B, 0x00C3 }, // lsl r3, r0, #3 (BGCNT size bits from w*h: 64x32 -> size 1)
	{ 0x080A8AB2, 0x81AB, 0x2C00 }, // cmp r4, #0
	{ 0x080A8AB4, 0x2C00, 0xD104 }, // bne 0x080A8AC0
	{ 0x080A8AB6, 0xD104, 0x4909 }, // ldr r1, =0x030015D0 (VRAM allocator)
	{ 0x080A8AB8, 0x4908, 0x6808 }, // ldr r0, [r1, #0]
	{ 0x080A8ABA, 0x6808, 0x1A80 }, // sub r0, r0, r2
	{ 0x080A8ABC, 0x1A80, 0x6008 }, // str r0, [r1, #0]
	{ 0x080A8ABE, 0x6008, 0x1C04 }, // mov r4, r0
	{ 0x080A8AC0, 0x1C04, 0x0420 }, // lsl r0, r4, #16
	{ 0x080A8AC2, 0x0AE0, 0x0EC0 }, // lsr r0, r0, #27 (screen block)
	{ 0x080A8AC4, 0x0200, 0x0200 }, // lsl r0, r0, #8
	{ 0x080A8AC6, 0x22F8, 0x4303 }, // orr r3, r0
	{ 0x080A8AC8, 0x0152, 0x81AB }, // strh r3, [r5, #12] (BGCNT shadow)
	{ 0x080A8ACA, 0x1C11, 0x0420 }, // lsl r0, r4, #16
	{ 0x080A8ACC, 0x4008, 0x0BC0 }, // lsr r0, r0, #15 (VRAM offset * 2)
	{ 0x080A8ACE, 0x89A9, 0x2180 }, // mov r1, #128
	{ 0x080A8AD0, 0x4308, 0x0489 }, // lsl r1, r1, #18 (0x02000000)
	{ 0x080A8AD2, 0x81A8, 0x4308 }, // orr r0, r1 (return EWRAM address)
	{ 0x080A8AD4, 0x1C20, 0x46C0 }, // nop

	// Layer setup (3 call sites): map width mov r1, #32 -> mov r1, #64
	{ 0x080AB616, 0x2120, 0x2140 },
	{ 0x080AC680, 0x2120, 0x2140 },
	{ 0x080AC8C2, 0x2120, 0x2140 },

	// Scroll layer init at 0x080A997E: zero +8..+14 with four strh (instead of seven
	// instructions), freeing room to store column mask 63 / row mask 31 separately and a
	// horizontal scroll mask of 511 (the HOFS value written to hardware) instead of 255
	{ 0x080A997E, 0x1C18, 0x811A }, // strh r2, [r3, #8]
	{ 0x080A9980, 0x3008, 0x815A }, // strh r2, [r3, #10]
	{ 0x080A9982, 0x811A, 0x819A }, // strh r2, [r3, #12]
	{ 0x080A9984, 0x8042, 0x81DA }, // strh r2, [r3, #14]
	{ 0x080A9986, 0x3004, 0x203F }, // mov r0, #63
	{ 0x080A9988, 0x819A, 0x8318 }, // strh r0, [r3, #24] (column mask)
	{ 0x080A998A, 0x8042, 0x201F }, // mov r0, #31
	{ 0x080A998C, 0x201F, 0x8358 }, // strh r0, [r3, #26] (row mask)
	{ 0x080A998E, 0x8318, 0x20FF }, // mov r0, #255
	{ 0x080A9990, 0x8358, 0x83D8 }, // strh r0, [r3, #30] (vertical scroll mask, 255 as before)
	{ 0x080A9992, 0x20FF, 0x0040 }, // lsl r0, r0, #1
	{ 0x080A9994, 0x8398, 0x3001 }, // add r0, #1
	{ 0x080A9996, 0x83D8, 0x8398 }, // strh r0, [r3, #28] (horizontal scroll mask 511, was 255)

	// ARM map routines (ROM sources of the IWRAM copies): 128-byte map rows instead of 64
	{ 0x080A6EC4, 0xA305, 0xA385 }, // 4bpp writer: add r10, r10, r5, lsl #6 -> lsl #7
	{ 0x080A7064, 0xA305, 0xA385 }, // 8bpp writer
	{ 0x080A7518, 0xA305, 0xA385 }, // writer (third variant)
	{ 0x080A70BC, 0x5304, 0x5384 }, // eraser: add r5, r5, r4, lsl #6 -> lsl #7
	{ 0x080A72A0, 0x5304, 0x5384 }, // eraser (4bpp)
	{ 0x080A75A0, 0x5304, 0x5384 }, // eraser (third variant)
	{ 0x080A7280, 0x5040, 0x5080 }, // eraser, column walk: add r5, r5, #64 -> #128
	{ 0x080A7380, 0x5040, 0x5080 }, // eraser (4bpp), column walk

	// Rewritten layer routines (assembled from tools/wide/sw3_routines.py with tools/wide/tasm.py)
	// 0x80AA4C4: full layer redraw: 39 columns from camera - 4 (both layer kinds)
	{ 0x080AA4C4, 0xB5F0, 0xB5F0 },
	{ 0x080AA4C6, 0x4657, 0xB086 },
	{ 0x080AA4C8, 0x464E, 0x1C07 },
	{ 0x080AA4CA, 0x4645, 0x9104 },
	{ 0x080AA4CC, 0xB4E0, 0x893C },
	{ 0x080AA4CE, 0xB088, 0x3C04 },
	{ 0x080AA4D0, 0x1C07, 0x2627 },
	{ 0x080AA4D2, 0x4689, 0x8B38 },
	{ 0x080AA4D4, 0x893A, 0x1C25 },
	{ 0x080AA4D6, 0x8B3B, 0x4005 },
	{ 0x080AA4D8, 0x8978, 0x1C43 },
	{ 0x080AA4DA, 0x8B79, 0x1B5B },
	{ 0x080AA4DC, 0x4001, 0x42B3 },
	{ 0x080AA4DE, 0x0409, 0xDD00 },
	{ 0x080AA4E0, 0x1C18, 0x1C33 },
	{ 0x080AA4E2, 0x4010, 0xAA03 },
	{ 0x080AA4E4, 0x4308, 0x8013 },
	{ 0x080AA4E6, 0x9006, 0x2115 },
	{ 0x080AA4E8, 0xAC06, 0x8051 },
	{ 0x080AA4EA, 0x8821, 0xAA02 },
	{ 0x080AA4EC, 0x1C08, 0x8015 },
	{ 0x080AA4EE, 0x301F, 0x8978 },
	{ 0x080AA4F0, 0x4018, 0x8B79 },
	{ 0x080AA4F2, 0x0400, 0x4001 },
	{ 0x080AA4F4, 0x0C00, 0x8051 },
	{ 0x080AA4F6, 0x4684, 0xAA01 },
	{ 0x080AA4F8, 0x0409, 0x8014 },
	{ 0x080AA4FA, 0x1409, 0x8050 },
	{ 0x080AA4FC, 0x46A2, 0x1C38 },
	{ 0x080AA4FE, 0x458C, 0x3039 },
	{ 0x080AA500, 0xDA34, 0x7800 },
	{ 0x080AA502, 0x2008, 0x2800 },
	{ 0x080AA504, 0x5E3A, 0xD015 },
	{ 0x080AA506, 0x210A, 0x1C20 },
	{ 0x080AA508, 0x5E7B, 0x2100 },
	{ 0x080AA50A, 0xA901, 0x5E79 },
	{ 0x080AA50C, 0x2500, 0xF009 },
	{ 0x080AA50E, 0x46A8, 0xFC48 },
	{ 0x080AA510, 0x1C08, 0xAA01 },
	{ 0x080AA512, 0x8002, 0x8010 },
	{ 0x080AA514, 0x804B, 0x2100 },
	{ 0x080AA516, 0x201F, 0x5E79 },
	{ 0x080AA518, 0x4661, 0x1A09 },
	{ 0x080AA51A, 0x1A40, 0xAA03 },
	{ 0x080AA51C, 0xAB02, 0x8813 },
	{ 0x080AA51E, 0x2415, 0x428B },
	{ 0x080AA520, 0x8018, 0xDD00 },
	{ 0x080AA522, 0x805C, 0x8011 },
	{ 0x080AA524, 0x2208, 0x8978 },
	{ 0x080AA526, 0x5EB8, 0x2102 },
	{ 0x080AA528, 0x2500, 0x5E79 },
	{ 0x080AA52A, 0x5F59, 0xF009 },
	{ 0x080AA52C, 0x250A, 0xFC39 },
	{ 0x080AA52E, 0x5F7A, 0xAA01 },
	{ 0x080AA530, 0xAE03, 0x8050 },
	{ 0x080AA532, 0x1840, 0x9804 },
	{ 0x080AA534, 0x8030, 0x9000 },
	{ 0x080AA536, 0x8072, 0x1C38 },
	{ 0x080AA538, 0xAD04, 0xA902 },
	{ 0x080AA53A, 0x4660, 0xAA01 },
	{ 0x080AA53C, 0x8028, 0xAB03 },
	{ 0x080AA53E, 0x806C, 0xF7FF },
	{ 0x080AA540, 0x9806, 0xFF6B },
	{ 0x080AA542, 0x1400, 0xAA03 },
	{ 0x080AA544, 0xAC05, 0x8813 },
	{ 0x080AA546, 0x4641, 0x18E4 },
	{ 0x080AA548, 0x8021, 0x1AF6 },
	{ 0x080AA54A, 0x8060, 0x2E00 },
	{ 0x080AA54C, 0x464A, 0xDCC1 },
	{ 0x080AA54E, 0x9200, 0xB006 },
	{ 0x080AA550, 0x1C38, 0xBCF0 },
	{ 0x080AA552, 0x4651, 0xBC01 },
	{ 0x080AA554, 0xAA01, 0x4700 },
	// 0x80AA598: wrapping-layer full redraw: use the routine above
	{ 0x080AA598, 0xB5F0, 0xE794 },
	// 0x80AA15C: row streaming: 39 columns from camera - 4
	{ 0x080AA15C, 0xB5F0, 0xB5F0 },
	{ 0x080AA15E, 0xB084, 0xB084 },
	{ 0x080AA160, 0x1C0D, 0x1C04 },
	{ 0x080AA162, 0x1C16, 0x1C0D },
	{ 0x080AA164, 0x1C03, 0x1C16 },
	{ 0x080AA166, 0x271F, 0x2727 },
	{ 0x080AA168, 0xAA02, 0x8920 },
	{ 0x080AA16A, 0x6898, 0x3804 },
	{ 0x080AA16C, 0x9002, 0x8961 },
	{ 0x080AA16E, 0x2E00, 0x2E00 },
	{ 0x080AA170, 0xDD03, 0xDD00 },
	{ 0x080AA172, 0x2102, 0x3114 },
	{ 0x080AA174, 0x5E50, 0x2D00 },
	{ 0x080AA176, 0x3014, 0xD003 },
	{ 0x080AA178, 0x8050, 0x2726 },
	{ 0x080AA17A, 0xA802, 0x2D00 },
	{ 0x080AA17C, 0x8801, 0xDA00 },
	{ 0x080AA17E, 0x8B18, 0x3001 },
	{ 0x080AA180, 0x4008, 0xAA02 },
	{ 0x080AA182, 0x0400, 0x8010 },
	{ 0x080AA184, 0x1404, 0x8051 },
	{ 0x080AA186, 0x8851, 0x8B23 },
	{ 0x080AA188, 0x8B58, 0x4018 },
	{ 0x080AA18A, 0x4008, 0x8B63 },
	{ 0x080AA18C, 0x0400, 0x4019 },
	{ 0x080AA18E, 0x1400, 0xAA03 },
	{ 0x080AA190, 0xAA03, 0x8010 },
	{ 0x080AA192, 0x8014, 0x8051 },
	{ 0x080AA194, 0x8050, 0x9902 },
	{ 0x080AA196, 0x2D00, 0x9A03 },
	{ 0x080AA198, 0xD00F, 0x9500 },
	{ 0x080AA19A, 0x271E, 0x9601 },
	{ 0x080AA19C, 0x2D00, 0x1C20 },
	{ 0x080AA19E, 0xDA0C, 0x1C3B },
	{ 0x080AA1A0, 0x1C60, 0xF7FF },
	{ 0x080AA1A2, 0x8010, 0xFE9A },
	{ 0x080AA1A4, 0xA802, 0xB004 },
	{ 0x080AA1A6, 0x8800, 0xBCF0 },
	{ 0x080AA1A8, 0x3001, 0xBC01 },
	{ 0x080AA1AA, 0xA902, 0x4700 },
	// 0x80AA1D4: row streaming, wrapping layers: 39 columns from camera - 4
	{ 0x080AA1D4, 0xB5F0, 0xB5F0 },
	{ 0x080AA1D6, 0x4657, 0xB084 },
	{ 0x080AA1D8, 0x464E, 0x1C04 },
	{ 0x080AA1DA, 0x4645, 0x1C0D },
	{ 0x080AA1DC, 0xB4E0, 0x1C16 },
	{ 0x080AA1DE, 0xB088, 0x2727 },
	{ 0x080AA1E0, 0x9104, 0x8920 },
	{ 0x080AA1E2, 0x9205, 0x3804 },
	{ 0x080AA1E4, 0x1C07, 0x8961 },
	{ 0x080AA1E6, 0x201F, 0x2E00 },
	{ 0x080AA1E8, 0x4681, 0xDD00 },
	{ 0x080AA1EA, 0x2100, 0x3114 },
	{ 0x080AA1EC, 0x9106, 0x2D00 },
	{ 0x080AA1EE, 0xAB02, 0xD003 },
	{ 0x080AA1F0, 0x68B8, 0x2726 },
	{ 0x080AA1F2, 0x9002, 0x2D00 },
	{ 0x080AA1F4, 0x1C10, 0xDA00 },
	{ 0x080AA1F6, 0x2800, 0x3001 },
	{ 0x080AA1F8, 0xDD03, 0xAA02 },
	{ 0x080AA1FA, 0x2102, 0x8010 },
	{ 0x080AA1FC, 0x5E58, 0x8051 },
	{ 0x080AA1FE, 0x3014, 0x8B23 },
	{ 0x080AA200, 0x8058, 0x4018 },
	{ 0x080AA202, 0xA802, 0x8B63 },
	{ 0x080AA204, 0x8800, 0x4019 },
	{ 0x080AA206, 0x8B39, 0xAA03 },
	{ 0x080AA208, 0x4001, 0x8010 },
	{ 0x080AA20A, 0x885A, 0x8051 },
	{ 0x080AA20C, 0x8B78, 0xAA02 },
	{ 0x080AA20E, 0x4010, 0x8810 },
	{ 0x080AA210, 0x0400, 0x2100 },
	{ 0x080AA212, 0x1400, 0x5E61 },
	{ 0x080AA214, 0xAC03, 0xF009 },
	{ 0x080AA216, 0x8021, 0xFDC4 },
	{ 0x080AA218, 0x8060, 0xAA02 },
	{ 0x080AA21A, 0x887D, 0x8010 },
	{ 0x080AA21C, 0x8838, 0x8850 },
	{ 0x080AA21E, 0x4680, 0x2102 },
	{ 0x080AA220, 0xA802, 0x5E61 },
	{ 0x080AA222, 0x2100, 0xF009 },
	{ 0x080AA224, 0x5E40, 0xFDBD },
	{ 0x080AA226, 0x4641, 0xAA02 },
	{ 0x080AA228, 0x9307, 0x8050 },
	{ 0x080AA22A, 0xF009, 0x9902 },
	{ 0x080AA22C, 0xFDB9, 0x9A03 },
	{ 0x080AA22E, 0x0400, 0x9500 },
	{ 0x080AA230, 0x0C00, 0x9601 },
	{ 0x080AA232, 0x4946, 0x1C20 },
	{ 0x080AA234, 0x468A, 0x1C3B },
	{ 0x080AA236, 0x400E, 0xF7FF },
	{ 0x080AA238, 0x4306, 0xFE4F },
	{ 0x080AA23A, 0x9B07, 0xB004 },
	{ 0x080AA23C, 0x2102, 0xBCF0 },
	{ 0x080AA23E, 0x5E58, 0xBC01 },
	{ 0x080AA240, 0x1C29, 0x4700 },

	// Tile release (0x080A99B4): when a column is drawn, the tiles of the column that just left
	// the far side are released. With 39 columns in view that's draw position -/+ 39, not 31
	{ 0x080A99D6, 0x301F, 0x3027 }, // moving left: add r0, #31 -> #39
	{ 0x080A99F6, 0x381F, 0x3827 }, // moving right: sub r0, #31 -> #39
	{ 0x080A9A0A, 0x3020, 0x3040 }, // ...and wrap negatives by 64 instead of 32

	// Column streaming, inline path for layers without the wrap flag (0x080AA968):
	// right: col + 30 -> col + 34, left: col -> col - 4
	{ 0x080AA96E, 0x2D00, 0x8808 }, // ldrh r0, [r1, #0]
	{ 0x080AA970, 0xDD03, 0x3804 }, // sub r0, #4
	{ 0x080AA972, 0x2200, 0x2D00 }, // cmp r5, #0
	{ 0x080AA974, 0x5E88, 0xDD00 }, // ble 0x080AA978
	{ 0x080AA976, 0x301E, 0x3026 }, // add r0, #38
	// (0x080AA978 strh r0, [r1, #0] unchanged)

	// Column streaming (0x080A9D98)
	// right: add r0, #30  ->  add r0, #34
	{ 0x080A9DB4, 0x301E, 0x3022 },
	// left: ldrh r1, [r5, #8]; ldr r0, =0xFFFF0000; and r3, r0
	//   ->  ldrh r0, [r5, #8]; sub r0, #4; b 0x080A9DB6 (shared mask/merge code of the right path)
	{ 0x080A9DC8, 0x8929, 0x8928 },
	{ 0x080A9DCA, 0x4841, 0x3804 },
	{ 0x080A9DCC, 0x4003, 0xE7F3 },
};

/* Scroll layer structs live in EWRAM at addresses that change between stages; find them by
 * the masks our patch writes into them (+24 column mask 63, +26 row mask 31, +28 x mask 511,
 * +30 y mask 255). Layout: +0 width (tiles), +4 max scroll x (pixels), +8 column, +12 fine x.
 */
#define SW3_MAX_LAYERS 4
static uint32_t _sw3Layers[SW3_MAX_LAYERS];
static int _sw3NLayers;
static int _sw3RescanTimer;

static uint16_t _ew16(const uint8_t* ewram, uint32_t offset) {
	return ewram[offset] | (ewram[offset + 1] << 8);
}

static bool _sw3IsLayer(const uint8_t* ewram, uint32_t offset) {
	if (offset + 32 > GBA_SIZE_EWRAM) {
		return false;
	}
	if (_ew16(ewram, offset + 24) != 63 || _ew16(ewram, offset + 26) != 31 || _ew16(ewram, offset + 28) != 511 || _ew16(ewram, offset + 30) != 255) {
		return false;
	}
	uint16_t width = _ew16(ewram, offset);
	uint16_t maxX = _ew16(ewram, offset + 4);
	return width >= 1 && width <= 4096 && maxX + 240 <= width * 8 + 8;
}

static void _sw3Frame(struct GBA* gba, struct GBAVideoSoftwareRenderer* renderer) {
	const uint8_t* ewram = (const uint8_t*) gba->memory.wram;
	int i;
	bool valid = _sw3NLayers > 0;
	for (i = 0; i < _sw3NLayers; ++i) {
		if (!_sw3IsLayer(ewram, _sw3Layers[i])) {
			valid = false;
		}
	}
	if (!valid) {
		_sw3NLayers = 0;
		if (_sw3RescanTimer > 0) {
			--_sw3RescanTimer;
		} else {
			uint32_t offset;
			for (offset = 0; offset < GBA_SIZE_EWRAM - 32 && _sw3NLayers < SW3_MAX_LAYERS; offset += 4) {
				if (_sw3IsLayer(ewram, offset)) {
					_sw3Layers[_sw3NLayers++] = offset;
				}
			}
			_sw3RescanTimer = 15;
		}
	}
	if (!_sw3NLayers) {
		// Menus, text screens: they're drawn for 240px, so show black side bars
		GBAVideoSoftwareRendererSetWideClip(renderer, renderer->margin, renderer->margin);
		return;
	}
	// Use the widest layer (the foreground) to find the edges of the stage
	uint32_t best = _sw3Layers[0];
	for (i = 1; i < _sw3NLayers; ++i) {
		if (_ew16(ewram, _sw3Layers[i]) > _ew16(ewram, best)) {
			best = _sw3Layers[i];
		}
	}
	int x = (int16_t) _ew16(ewram, best + 8) * 8 + (int16_t) _ew16(ewram, best + 12);
	int maxX = _ew16(ewram, best + 4);
	if (maxX + GBA_VIDEO_HORIZONTAL_PIXELS <= 256) {
		// A single-screen layout (menus, title cards): keep it centered between black bars
		GBAVideoSoftwareRendererSetWideClip(renderer, renderer->margin, renderer->margin);
		return;
	}
	GBAVideoSoftwareRendererSetWideClip(renderer, renderer->margin - x, renderer->margin - (maxX - x));
}

static const struct GBAWideGame _games[] = {
	{ { 'B', 'E', '3', 'E' }, 0, "Star Wars: Episode III - Revenge of the Sith (USA)", _sw3Patches, sizeof(_sw3Patches) / sizeof(*_sw3Patches), false, 0xF, _sw3Frame },
	{ { 0, 0, 0, 0 }, 0, NULL, NULL, 0, false, 0, NULL }
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
