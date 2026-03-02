#ifndef CRUCIBLE_PATH_H
#define CRUCIBLE_PATH_H

#include <string>

namespace crucible {
	using namespace std;

	/// Return the final component of path @p s (everything after the last '/').
	string basename(string s);
	/// Join directory @p dir and filename @p base with a '/' separator.
	string join(string dir, string base);
};

#endif // CRUCIBLE_PATH_H
