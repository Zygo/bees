#ifndef CRUCIBLE_CRC64_H
#define CRUCIBLE_CRC64_H

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace crucible {
	namespace Digest {
		namespace CRC {
			uint64_t crc64(const char *s);
			uint64_t crc64(const void *p, size_t len);
		};
	};
};

#endif
