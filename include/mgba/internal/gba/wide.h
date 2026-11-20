/* mgba-wide: per-game widescreen fixes
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_WIDE_H
#define GBA_WIDE_H

#include <mgba-util/common.h>

CXX_GUARD_START

struct GBA;
struct GBAVideoSoftwareRenderer;

struct GBAWidePatch {
	uint32_t address;  // ROM address (0x08xxxxxx)
	uint16_t original; // expected halfword before patching; the whole set is skipped on mismatch
	uint16_t patched;
	bool marginImm;    // OR the widescreen margin (pixels per side) into the patched value's low byte
};

struct GBAWideGame {
	char code[4];      // game code from the ROM header, e.g. "BE3E"
	uint8_t version;   // ROM version byte (header 0xBC)
	const char* title;
	const struct GBAWidePatch* patches;
	size_t nPatches;
	bool shadowMaps;   // keep 64-column shadow copies of 256px-wide BG maps
	// BGs (bit per BG) whose size-1 text maps the patched game lays out as linear 64x32
	uint8_t linearMaps;
	// optional: called once per frame while the fix is on, to sync renderer settings with game state
	void (*frame)(struct GBA* gba, struct GBAVideoSoftwareRenderer* renderer);
};

const struct GBAWideGame* GBAWideFindGame(const struct GBA* gba);

// Apply (enable == true) or revert this game's widescreen fix. Returns the game entry, or NULL
// if the loaded ROM has no fix. Safe to call repeatedly.
const struct GBAWideGame* GBAWideApply(struct GBA* gba, bool enable, int margin);

CXX_GUARD_END

#endif
