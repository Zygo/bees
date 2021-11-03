#ifndef CRUCIBLE_UNAME_H
#define CRUCIBLE_UNAME_H

#include <sys/utsname.h>

namespace crucible {
	using namespace std;

	struct Uname : public utsname {
		Uname();
	};
}

#endif
