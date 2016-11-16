#include "crucible/path.h"

#include "crucible/error.h"

namespace crucible {
	using namespace std;

	string
	basename(string s)
	{
		size_t left = s.find_last_of("/");
		size_t right = s.find_last_not_of("/");
		if (left == string::npos) {
			return s;
		}
		return s.substr(left + 1, right);
	}

	string
	join(string dir, string base)
	{
		// TODO:  a lot of sanity checking, maybe canonicalization
		return dir + "/" + base;
	}

};
