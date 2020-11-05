#include "crucible/string.h"

#include "crucible/error.h"

#include <inttypes.h>

namespace crucible {
	using namespace std;

	string
	to_hex(uint64_t i)
	{
		return astringprintf("0x%" PRIx64, i);
	}

	uint64_t
	from_hex(const string &s)
	{
		return stoull(s, nullptr, 0);
	}

	vector<string>
	split(string delim, string s)
	{
		if (delim.empty()) {
			THROW_ERROR(invalid_argument, "delimiter empty when splitting '" << s << "'");
		}
		vector<string> rv;
		size_t n = 0;
		while (n < s.length()) {
			size_t f = s.find(delim, n);
			if (f == string::npos) {
				rv.push_back(s.substr(n));
				break;
			}
			if (f > n) {
				rv.push_back(s.substr(n, f - n));
			}
			n = f + delim.length();
		}
		return rv;
	}
};
