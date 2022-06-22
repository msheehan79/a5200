/*
 * memory.c - memory emulation
 *
 * Copyright (C) 1995-1998 David Firth
 * Copyright (C) 1998-2006 Atari800 development team (see DOC/CREDITS)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atari.h"
#include "antic.h"
#include "cpu.h"
#include "cartridge.h"
#include "gtia.h"
#include "memory.h"
#include "pbi.h"
#include "pia.h"
#include "pokeysnd.h"
#include "util.h"
#include "statesav.h"

UBYTE memory[65536 + 2] __attribute__ ((aligned (4)));
UBYTE attrib[65536];

static UBYTE under_atarixl_os[16384];
static UBYTE under_atari_basic[8192];

void MEMORY_InitialiseMachine(void) {
	memcpy(memory + 0xf800, atari_os, 0x800);
	dFillMem(0x0000, 0x00, 0xf800);
	SetRAM(0x0000, 0x3fff);
	SetROM(0x4000, 0xffff);
	SetHARDWARE(0xc000, 0xc0ff);	/* 5200 GTIA Chip */
	SetHARDWARE(0xd400, 0xd4ff);	/* 5200 ANTIC Chip */
	SetHARDWARE(0xe800, 0xe8ff);	/* 5200 POKEY Chip */
	SetHARDWARE(0xeb00, 0xebff);	/* 5200 POKEY Chip */
	Coldstart();
}

void MemStateSave(UBYTE SaveVerbose)
{
	SaveUBYTE(&memory[0], 65536);
	SaveUBYTE(&attrib[0], 65536);
}

void MemStateRead(UBYTE SaveVerbose) {
	ReadUBYTE(&memory[0], 65536);
	ReadUBYTE(&attrib[0], 65536);
}

void CopyFromMem(UWORD from, UBYTE *to, int size)
{
	while (--size >= 0) {
		*to++ = GetByte(from);
		from++;
	}
}

void CopyToMem(const UBYTE *from, UWORD to, int size)
{
	while (--size >= 0) {
		PutByte(to, *from);
		from++;
		to++;
	}
}

/*
 * Returns non-zero, if Atari BASIC is disabled by given PORTB output.
 * Normally BASIC is disabled by setting bit 1, but it's also disabled
 * when using 576K and 1088K memory expansions, where bit 1 is used
 * for selecting extended memory bank number.
 */
static int basic_disabled(UBYTE portb)
{
	return (portb & 0x02) != 0;
}

/* Note: this function is only for XL/XE! */
void MEMORY_HandlePORTB(UBYTE byte, UBYTE oldval)
{
	/* Enable/disable OS ROM in 0xc000-0xcfff and 0xd800-0xffff */
	if ((oldval ^ byte) & 0x01) {
		if (byte & 0x01) {
			memcpy(memory + 0xc000, atari_os, 0x1000);
			memcpy(memory + 0xd800, atari_os + 0x1800, 0x2800);
			Atari800_PatchOS();
		}
		else {
			dFillMem(0xc000, 0xff, 0x1000);
			dFillMem(0xd800, 0xff, 0x2800);
			/* When OS ROM is disabled we also have to disable Self Test - Jindroush */
			if (selftest_enabled) {
				dFillMem(0x5000, 0xff, 0x800);
				selftest_enabled = FALSE;
			}
		}
	}

	/* Enable/disable BASIC ROM in 0xa000-0xbfff */
	if (!cartA0BF_enabled) {
		/* BASIC is disabled if bit 1 set or accessing extended 576K or 1088K memory */
		int now_disabled = basic_disabled(byte);
		if (basic_disabled(oldval) != now_disabled) {
			if (now_disabled)
				dFillMem(0xa000, 0xff, 0x2000);
			else
				memcpy(memory + 0xa000, atari_basic, 0x2000);
		}
	}

	/* Enable/disable Self Test ROM in 0x5000-0x57ff */
	if (byte & 0x80) {
		if (selftest_enabled) {
			/* Disable Self Test ROM */
			dFillMem(0x5000, 0xff, 0x800);
			selftest_enabled = FALSE;
		}
	}
}

static int cart809F_enabled = FALSE;
int cartA0BF_enabled = FALSE;
static UBYTE under_cart809F[8192];
static UBYTE under_cartA0BF[8192];

void Cart809F_Disable(void)
{
	if (cart809F_enabled) {
		dFillMem(0x8000, 0xff, 0x2000);
		cart809F_enabled = FALSE;
	}
}

void Cart809F_Enable(void)
{
	if (!cart809F_enabled) {
		cart809F_enabled = TRUE;
	}
}

void CartA0BF_Disable(void)
{
	if (cartA0BF_enabled) {
		dFillMem(0xa000, 0xff, 0x2000);
		cartA0BF_enabled = FALSE;
	}
}

void CartA0BF_Enable(void)
{
	if (!cartA0BF_enabled)
		cartA0BF_enabled = TRUE;
}

void get_charset(UBYTE *cs)
{
	const UBYTE *p = memory + 0xf800;
	/* copy font, but change screencode order to ATASCII order */
	memcpy(cs, p + 0x200, 0x100); /* control chars */
	memcpy(cs + 0x100, p, 0x200); /* !"#$..., uppercase letters */
	memcpy(cs + 0x300, p + 0x300, 0x100); /* lowercase letters */
}
