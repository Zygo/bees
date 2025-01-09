#include "crucible/openat2.h"

extern "C" {

int
__attribute__((weak))
openat2(int const dirfd, const char *const pathname, struct open_how *const how, size_t const size)
throw()
{
	return syscall(SYS_openat2, dirfd, pathname, how, size);
}

};
