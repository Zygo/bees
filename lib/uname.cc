#include "crucible/error.h"
#include "crucible/uname.h"

namespace crucible {
	using namespace std;

	Uname::Uname()
	{
		DIE_IF_NON_ZERO(uname(static_cast<utsname*>(this)));
	}
}
