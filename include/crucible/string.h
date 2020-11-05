#ifndef CRUCIBLE_STRING_H
#define CRUCIBLE_STRING_H

#include "crucible/error.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace crucible {
	using namespace std;

	// Zero-initialize a base class object (usually a C struct)
	template <class Base>
	void
	memset_zero(Base *that)
	{
		memset(that, 0, sizeof(Base));
	}

	// Copy a base class object (usually a C struct) into a vector<uint8_t>
	template <class Base>
	vector<uint8_t>
	vector_copy_struct(Base *that)
	{
		const uint8_t *begin_that = reinterpret_cast<const uint8_t *>(static_cast<const Base *>(that));
		return vector<uint8_t>(begin_that, begin_that + sizeof(Base));
	}

	// int->hex conversion with sprintf
	string to_hex(uint64_t i);

	// hex->int conversion with stoull
	uint64_t from_hex(const string &s);

	// asprintf with string output and exceptions
	template<class... Args>
	string
	astringprintf(const char *fmt, Args... args)
	{
		char *rv = NULL;
		DIE_IF_MINUS_ONE(asprintf(&rv, fmt, args...));
		string rv_string = rv;
		free(rv);
		return rv_string;
	}

	template<class... Args>
	string
	astringprintf(const string &fmt, Args... args)
	{
		return astringprintf(fmt.c_str(), args...);
	}

	vector<string> split(string delim, string s);

	// Shut up and give me the difference between two pointers
	template <class P1, class P2>
	ptrdiff_t
	pointer_distance(const P1 *a, const P2 *b)
	{
		return reinterpret_cast<const uint8_t *>(a) - reinterpret_cast<const uint8_t *>(b);
	}
};

#endif // CRUCIBLE_STRING_H
