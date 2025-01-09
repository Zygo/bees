#ifndef CRUCIBLE_OPENAT2_H
#define CRUCIBLE_OPENAT2_H

#include <linux/openat2.h>

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" {

/// Weak symbol to support libc with no syscall wrapper
int openat2(int dirfd, const char *pathname, struct open_how *how, size_t size) throw();

};

#endif // CRUCIBLE_OPENAT2_H
