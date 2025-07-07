#include "crucible/openat2.h"

#include <sys/syscall.h>

// Compatibility for building on old libc for new kernel

// Every arch that defines this (so far) uses 437, except Alpha, where 437 is
// mq_getsetattr.

#ifndef SYS_openat2
#ifdef __alpha__
#define SYS_openat2 547
#else
#define SYS_openat2 437
#endif
#endif

#include <fcntl.h>
#include <unistd.h>

extern "C" {

int
__attribute__((weak))
openat2(int const dirfd, const char *const pathname, struct open_how *const how, size_t const size)
throw()
{
	return syscall(SYS_openat2, dirfd, pathname, how, size);
}

};
