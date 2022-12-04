#ifndef CRUCIBLE_NTOA_H
#define CRUCIBLE_NTOA_H

#include <string>

namespace crucible {
	using namespace std;

	struct bits_ntoa_table {
		unsigned long long n;
		unsigned long long mask;
		const char *a;
	};

	string bits_ntoa(unsigned long long n, const bits_ntoa_table *a);

};

// Combinations of bits (list multiple-bit entries first)
#define NTOA_TABLE_ENTRY_BITS(x) { .n = (x), .mask = (x), .a = (#x) }

// Enumerations (entire value matches all bits)
#define NTOA_TABLE_ENTRY_ENUM(x) { .n = (x), .mask = ~0ULL,  .a = (#x) }

// End of table (sorry, C++ didn't get C99's compound literals, so we have to write out all the member names)
#define NTOA_TABLE_ENTRY_END() { .n = 0, .mask = 0, .a = nullptr }

#endif // CRUCIBLE_NTOA_H
