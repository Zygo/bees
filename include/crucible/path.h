#ifndef CRUCIBLE_PATH_H
#define CRUCIBLE_PATH_H

#include <string>

namespace crucible {
	using namespace std;

	string basename(string s);
	string join(string dir, string base);
};

#endif // CRUCIBLE_PATH_H
