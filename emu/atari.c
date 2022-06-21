/*
 * atari.c - main high-level routines
 *
 * Copyright (c) 1995-1998 David Firth
 * Copyright (c) 1998-2006 Atari800 development team (see DOC/CREDITS)
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# elif defined(HAVE_TIME_H)
#  include <time.h>
# endif
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef WIN32
#include <windows.h>
#endif
#ifdef __EMX__
#define INCL_DOS
#include <os2.h>
#endif
#ifdef __BEOS__
#include <OS.h>
#endif
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

//ALEK #include "main.h"

#include "antic.h"
#include "atari.h"
#include "binload.h"
#include "cartridge.h"
#include "cassette.h"
#include "cpu.h"
#include "devices.h"
#include "gtia.h"

#include "input.h"

#include "memory.h"
#include "pia.h"
#include "platform.h"
#include "pokeysnd.h"
#include "rtime8.h"
#include "sio.h"
#include "util.h"
#if !defined(BASIC)
#include "screen.h"
#endif
#ifndef BASIC
#include "statesav.h"
#endif /* BASIC */
#if defined(SOUND) && !defined(__PLUS)
#include "pokeysnd.h"
#include "sound.h"
#endif

#ifdef __PLUS
#include "macros.h"
#include "display_win.h"
#include "misc_win.h"
#include "registry.h"
#include "timing.h"
#include "FileService.h"
#include "Helpers.h"
#endif /* __PLUS */

int ram_size = 16;
int tv_mode = TV_NTSC;
int disable_basic = TRUE;
int enable_sio_patch = TRUE;

int sprite_collisions_in_skipped_frames = FALSE;

//int emuos_mode = 1;	/* 0 = never use EmuOS, 1 = use EmuOS if real OS not available, 2 = always use EmuOS */

/* Now we check address of every escape code, to make sure that the patch
   has been set by the emulator and is not a CIM in Atari program.
   Also switch() for escape codes has been changed to array of pointers
   to functions. This allows adding port-specific patches (e.g. modem device)
   using Atari800_AddEsc, Device_UpdateHATABSEntry etc. without modifying
   atari.c/devices.c. Unfortunately it can't be done for patches in Atari OS,
   because the OS in XL/XE can be disabled.
*/
static UWORD esc_address[256];
static EscFunctionType esc_function[256];

void Atari800_ClearAllEsc(void) {
	int i;
	for (i = 0; i < 256; i++)
		esc_function[i] = NULL;
}

void Atari800_AddEsc(UWORD address, UBYTE esc_code, EscFunctionType function) {
	esc_address[esc_code] = address;
	esc_function[esc_code] = function;
	dPutByte(address, 0xf2);			/* ESC */
	dPutByte(address + 1, esc_code);	/* ESC CODE */
}

void Atari800_AddEscRts(UWORD address, UBYTE esc_code, EscFunctionType function) {
	esc_address[esc_code] = address;
	esc_function[esc_code] = function;
	dPutByte(address, 0xf2);			/* ESC */
	dPutByte(address + 1, esc_code);	/* ESC CODE */
	dPutByte(address + 2, 0x60);		/* RTS */
}

/* 0xd2 is ESCRTS, which works same as pair of ESC and RTS (I think so...).
   So this function does effectively the same as Atari800_AddEscRts,
   except that it modifies 2, not 3 bytes in Atari memory.
   I don't know why it is done that way, so I simply leave it
   unchanged (0xf2/0xd2 are used as in previous versions).
*/
void Atari800_AddEscRts2(UWORD address, UBYTE esc_code, EscFunctionType function)
{
	esc_address[esc_code] = address;
	esc_function[esc_code] = function;
	dPutByte(address, 0xd2);			/* ESCRTS */
	dPutByte(address + 1, esc_code);	/* ESC CODE */
}

void Atari800_RemoveEsc(UBYTE esc_code)
{
	esc_function[esc_code] = NULL;
}

void Atari800_RunEsc(UBYTE esc_code)
{
	if (esc_address[esc_code] == regPC - 2 && esc_function[esc_code] != NULL) {
		esc_function[esc_code]();
		return;
	}
#ifdef CRASH_MENU
	regPC -= 2;
	crash_address = regPC;
	crash_afterCIM = regPC + 2;
	crash_code = dGetByte(crash_address);
	ui();
#else /* CRASH_MENU */
	cim_encountered = 1;
#ifndef __PLUS
	if (!Atari800_Exit())
		exit(0);
#else /* __PLUS */
	Atari800_Exit();
#endif /* __PLUS */
#endif /* CRASH_MENU */
}

void Atari800_PatchOS(void) {
	int patched = Device_PatchOS();

	if (enable_sio_patch) {
		UWORD addr_l;
		UWORD addr_s;
		UBYTE check_s_0;
		UBYTE check_s_1;
		return;
	}

	Atari800_RemoveEsc(ESC_COPENLOAD);
	Atari800_RemoveEsc(ESC_COPENSAVE);
	Atari800_RemoveEsc(ESC_SIOV);
}

void Warmstart(void) {
	{
		PIA_Reset();
		ANTIC_Reset();
		/* CPU_Reset() must be after PIA_Reset(),
		   because Reset routine vector must be read from OS ROM */
		CPU_Reset();
		/* note: POKEY and GTIA have no Reset pin */
	}
#ifdef __PLUS
	HandleResetEvent();
#endif
}

void Coldstart(void) {
	PIA_Reset();
	ANTIC_Reset();
	/* CPU_Reset() must be after PIA_Reset(),
	   because Reset routine vector must be read from OS ROM */
	CPU_Reset();
	/* note: POKEY and GTIA have no Reset pin */
#ifdef __PLUS
	HandleResetEvent();
#endif
	/* reset cartridge to power-up state */
	CART_Start();

	/* set Atari OS Coldstart flag */
	dPutByte(0x244, 1);
	/* handle Option key (disable BASIC in XL/XE)
	   and Start key (boot from cassette) */
	consol_index = 2;
	consol_table[2] = 0x0f;
	if (disable_basic && !loading_basic) {
		/* hold Option during reboot */
		consol_table[2] &= ~CONSOL_OPTION;
	}
	if (hold_start) {
		/* hold Start during reboot */
		consol_table[2] &= ~CONSOL_START;
	}
	consol_table[1] = consol_table[2];
}

int Atari800_InitialiseMachine(void) {
	Atari800_ClearAllEsc();
	MEMORY_InitialiseMachine();
	Device_UpdatePatches();
	return TRUE;
}

static int Atari800_DetectFileType(const uint8_t *data, size_t size) {
	UBYTE header[4];
	if (data == NULL || size < 4)
		return AFILE_ERROR;
	memcpy(header, data, 4);
	switch (header[0]) {
	case 0:
		if (header[1] == 0 && (header[2] != 0 || header[3] != 0) /* && size < 37 * 1024 */) {
			return AFILE_BAS;
		}
		break;
	case 0x1f:
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		if ((header[1] >= '0' && header[1] <= '9') || header[1] == ' ') {
			return AFILE_LST;
		}
		break;
	case 'A':
		if (header[1] == 'T' && header[2] == 'A' && header[3] == 'R') {
			return AFILE_STATE;
		}
		break;
	case 'C':
		if (header[1] == 'A' && header[2] == 'R' && header[3] == 'T') {
			return AFILE_CART;
		}
		break;
	case 'F':
		if (header[1] == 'U' && header[2] == 'J' && header[3] == 'I') {
			return AFILE_CAS;
		}
		break;
	case 0x96:
		if (header[1] == 0x02) {
			return AFILE_ATR;
		}
		break;
	case 0xf9:
	case 0xfa:
		return AFILE_DCM;
	case 0xff:
		if (header[1] == 0xff && (header[2] != 0xff || header[3] != 0xff)) {
			return AFILE_XEX;
		}
		break;
	default:
		break;
	}
	/* 40K or a-power-of-two between 4K and CART_MAX_SIZE */
	if (size >= 4 * 1024 && size <= CART_MAX_SIZE
	 && ((size & (size - 1)) == 0 || size == 40 * 1024))
		return AFILE_ROM;
	/* BOOT_TAPE is a raw file containing a program booted from a tape */
	if ((header[1] << 7) == size)
		return AFILE_BOOT_TAPE;
	if ((size & 0x7f) == 0)
		return AFILE_XFD;
	return AFILE_ERROR;
}

int Atari800_OpenFile(const uint8_t *data, size_t size, int reboot, int diskno, int readonly) {
	// Remove cart if exist
	CART_Remove();

	int type = Atari800_DetectFileType(data, size);

	switch (type) {
		case AFILE_CART:
		case AFILE_ROM:
			if (CART_Insert(data, size) != 0) {
			  return AFILE_ERROR;
			}
			if (reboot)
			  Coldstart();
			break;
		default:
			type = AFILE_ERROR;
			break;
	}
	return type;
}

int Atari800_Initialise(void) {
  Device_Initialise();
	RTIME8_Initialise();
	SIO_Initialise ();
	CASSETTE_Initialise();

	INPUT_Initialise();

	// Platform Specific Initialisation 
	Atari_Initialise();

	// Initialise Custom Chips
	ANTIC_Initialise();
	GTIA_Initialise();
	PIA_Initialise();
	POKEY_Initialise();

	Atari800_InitialiseMachine();

	return TRUE;
}

UNALIGNED_STAT_DEF(atari_screen_write_long_stat)
UNALIGNED_STAT_DEF(pm_scanline_read_long_stat)
UNALIGNED_STAT_DEF(memory_read_word_stat)
UNALIGNED_STAT_DEF(memory_write_word_stat)
UNALIGNED_STAT_DEF(memory_read_aligned_word_stat)
UNALIGNED_STAT_DEF(memory_write_aligned_word_stat)

int Atari800_Exit(void) {
	int restart = 0;
#ifndef __PLUS
	if (!restart) {
		SIO_Exit();	/* umount disks, so temporary files are deleted */
	}
#endif /* __PLUS */
	return restart;
}

UBYTE Atari800_GetByte(UWORD addr) {
	UBYTE byte = 0xff;
	switch (addr & 0xff00) {
	case 0x4f00:
	case 0x8f00:
		CART_BountyBob1(addr);
		byte = 0;
		break;
	case 0x5f00:
	case 0x9f00:
		CART_BountyBob2(addr);
		byte = 0;
		break;
	case 0xd000:				/* GTIA */
	case 0xc000:				/* GTIA - 5200 */
		byte = GTIA_GetByte(addr);
		break;
	case 0xd200:				/* POKEY */
	case 0xe800:				/* POKEY - 5200 */
	case 0xeb00:				/* POKEY - 5200 */
	  byte = POKEY_GetByte(addr);
		break;
	case 0xd300:				/* PIA */
		byte = PIA_GetByte(addr);
		break;
	case 0xd400:				/* ANTIC */
		byte = ANTIC_GetByte(addr);
		break;
	case 0xd500:				/* bank-switching cartridges, RTIME-8 */
		byte = CART_GetByte(addr);
		break;
	default:
		break;
	}

	return byte;
}

void Atari800_PutByte(UWORD addr, UBYTE byte) {
	switch (addr & 0xff00) {
    case 0x4f00:
    case 0x8f00:
      CART_BountyBob1(addr);
      break;
    case 0x5f00:
    case 0x9f00:
      CART_BountyBob2(addr);
      break;
    case 0xd000:				/* GTIA */
    case 0xc000:				/* GTIA - 5200 */
      GTIA_PutByte(addr, byte);
      break;
    case 0xd200:				/* POKEY */
    case 0xe800:				/* POKEY - 5200 AAA added other pokey space */
    case 0xeb00:				/* POKEY - 5200 */
      POKEY_PutByte(addr, byte);
      break;
    case 0xd300:				/* PIA */
      PIA_PutByte(addr, byte);
      break;
    case 0xd400:				/* ANTIC */
      ANTIC_PutByte(addr, byte);
      break;
    case 0xd500:				/* bank-switching cartridges, RTIME-8 */
      CART_PutByte(addr, byte);
      break;
    default:
      break;
	}
}

void Atari800_UpdatePatches(void) {
}

#ifndef __PLUS

void Atari800_Frame(void)
{
#ifndef BASIC
	INPUT_Frame();
#endif
	GTIA_Frame();
	ANTIC_Frame();
	POKEY_Frame();
}

#endif /* __PLUS */

#ifndef BASIC

void MainStateSave(void) {
	UBYTE temp;
	int default_tv_mode;
	int os = 0;
	int default_system = 3;
	int pil_on = FALSE;

	if (tv_mode == TV_PAL) {
		temp = 0;
		default_tv_mode = 1;
	}
	else {
		temp = 1;
		default_tv_mode = 2;
	}
	SaveUBYTE(&temp, 1);

	{
		temp = 4;
		default_system = 6;
	}
	SaveUBYTE(&temp, 1);

	SaveINT(&os, 1);
	SaveINT(&pil_on, 1);
	SaveINT(&default_tv_mode, 1);
	SaveINT(&default_system, 1);
}

void MainStateRead(void) {
	/* these are all for compatibility with previous versions */
	UBYTE temp;
	int default_tv_mode;
	int os;
	int default_system;
	int pil_on;

	ReadUBYTE(&temp, 1);
	tv_mode = (temp == 0) ? TV_PAL : TV_NTSC;

	ReadUBYTE(&temp, 1);
	ReadINT(&os, 1);
	ram_size = 16;
	ReadINT(&pil_on, 1);
	ReadINT(&default_tv_mode, 1);
	ReadINT(&default_system, 1);
}

#endif
