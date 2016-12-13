/* crc64.c -- compute CRC-64
 * Copyright (C) 2013 Mark Adler
 * Version 1.4  16 Dec 2013  Mark Adler
 */

/*
 This software is provided 'as-is', without any express or implied
 warranty.  In no event will the author be held liable for any damages
 arising from the use of this software.

 Permission is granted to anyone to use this software for any purpose,
 including commercial applications, and to alter it and redistribute it
 freely, subject to the following restrictions:

 1. The origin of this software must not be misrepresented; you must not
 claim that you wrote the original software. If you use this software
 in a product, an acknowledgment in the product documentation would be
 appreciated but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
 misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.

 Mark Adler
 madler@alumni.caltech.edu
 */

/* Substantially modified by Paul Jones for usage in bees */

#include "crucible/crc64.h"

#define POLY64REV 0xd800000000000000ULL

namespace crucible {

	static bool init = false;
	static uint64_t CRCTable[8][256];

	static void init_crc64_table()
	{
		if (!init) {
			uint64_t crc;

			// Generate CRCs for all single byte sequences
			for (int n = 0; n < 256; n++) {
				uint64_t part = n;
				for (int j = 0; j < 8; j++) {
					if (part & 1) {
						part = (part >> 1) ^ POLY64REV;
					} else {
						part >>= 1;
					}
				}
				CRCTable[0][n] = part;
			}

			// Generate nested CRC table for slice-by-8 lookup
			for (int n = 0; n < 256; n++) {
				crc = CRCTable[0][n];
				for (int k = 1; k < 8; k++) {
					crc = CRCTable[0][crc & 0xff] ^ (crc >> 8);
					CRCTable[k][n] = crc;
				}
			}
			init = true;
		}
	}

	uint64_t
	Digest::CRC::crc64(const void *p, size_t len)
	{
		init_crc64_table();
		const unsigned char *next = static_cast<const unsigned char *>(p);
		uint64_t crc = 0;

		// Process individual bytes until we reach an 8-byte aligned pointer
		while (len && (reinterpret_cast<uintptr_t>(next) & 7) != 0) {
			crc = CRCTable[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
			len--;
		}

		// Fast middle processing, 8 bytes (aligned!) per loop
		while (len >= 8) {
			crc ^= *(reinterpret_cast< const uint64_t *>(next));
			crc = CRCTable[7][crc & 0xff] ^
				  CRCTable[6][(crc >> 8) & 0xff] ^
				  CRCTable[5][(crc >> 16) & 0xff] ^
				  CRCTable[4][(crc >> 24) & 0xff] ^
				  CRCTable[3][(crc >> 32) & 0xff] ^
				  CRCTable[2][(crc >> 40) & 0xff] ^
				  CRCTable[1][(crc >> 48) & 0xff] ^
				  CRCTable[0][crc >> 56];
			next += 8;
			len -= 8;
		}

		// Process remaining bytes (can't be larger than 8)
		while (len) {
			crc = CRCTable[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
			len--;
		}

		return crc;
	}


};
