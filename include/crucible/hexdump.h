#ifndef CRUCIBLE_HEXDUMP_H
#define CRUCIBLE_HEXDUMP_H

#include "crucible/string.h"

#include <ostream>

namespace crucible {
	using namespace std;

	template <class V>
	ostream &
	hexdump(ostream &os, const V &v)
	{
		os << "V { size = " << v.size() << ", data:\n";
		for (size_t i = 0; i < v.size(); i += 8) {
			string hex, ascii;
			for (size_t j = i; j < i + 8; ++j) {
				if (j < v.size()) {
					uint8_t c = v[j];
					char buf[8];
					sprintf(buf, "%02x ", c);
					hex += buf;
					ascii += (c < 32 || c > 126) ? '.' : c;
				} else {
					hex += "   ";
					ascii += ' ';
				}
			}
			os << astringprintf("\t%08x %s %s\n", i, hex.c_str(), ascii.c_str());
		}
		return os << "}";
	}
};

#endif // CRUCIBLE_HEXDUMP_H
