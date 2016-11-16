#include "crucible/crc64.h"

#define POLY64REV 0xd800000000000000ULL

namespace crucible {

	static bool init = false;
	static uint64_t CRCTable[256];

	static void init_crc64_table()
	{
		if (!init) {
			for (int i = 0; i <= 255; i++) {
				uint64_t part = i;
				for (int j = 0; j < 8; j++) {
					if (part & 1) {
						part = (part >> 1) ^ POLY64REV;
					} else {
						part >>= 1;
					}
				}
				CRCTable[i] = part;
			}
			init = true;
		}
	}

	uint64_t
	Digest::CRC::crc64(const char *s)
	{
		init_crc64_table();

		uint64_t crc = 0;
		for (; *s; s++) {
			uint64_t temp1 = crc >> 8;
			uint64_t temp2 = CRCTable[(crc ^ static_cast<uint64_t>(*s)) & 0xff];
			crc = temp1 ^ temp2;
		}

		return crc;
	}

	uint64_t
	Digest::CRC::crc64(const void *p, size_t len)
	{
		init_crc64_table();

		uint64_t crc = 0;
		for (const unsigned char *s = static_cast<const unsigned char *>(p); len; --len) {
			uint64_t temp1 = crc >> 8;
			uint64_t temp2 = CRCTable[(crc ^ *s++) & 0xff];
			crc = temp1 ^ temp2;
		}

		return crc;
	}


};
