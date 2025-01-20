#ifndef CRUCIBLE_OPENAT2_H
#define CRUCIBLE_OPENAT2_H

#include <cstdlib>

// Compatibility for building on old libc for new kernel
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)

#include <linux/openat2.h>

#else

#ifndef RESOLVE_NO_XDEV
#define RESOLVE_NO_XDEV 1

// RESOLVE_NO_XDEV was there from the beginning of openat2,
// so if that's missing, so is open_how

struct open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};
#endif

#ifndef RESOLVE_NO_MAGICLINKS
#define RESOLVE_NO_MAGICLINKS 2
#endif
#ifndef RESOLVE_NO_SYMLINKS
#define RESOLVE_NO_SYMLINKS 4
#endif
#ifndef RESOLVE_BENEATH
#define RESOLVE_BENEATH 8
#endif
#ifndef RESOLVE_IN_ROOT
#define RESOLVE_IN_ROOT 16
#endif

#endif // Linux version >= v5.6

extern "C" {

/// Weak symbol to support libc with no syscall wrapper
int openat2(int dirfd, const char *pathname, struct open_how *how, size_t size) throw();

};

#endif // CRUCIBLE_OPENAT2_H
