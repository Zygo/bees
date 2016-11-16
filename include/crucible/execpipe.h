#ifndef CRUCIBLE_EXECPIPE_H
#define CRUCIBLE_EXECPIPE_H

#include "crucible/fd.h"

#include <functional>
#include <limits>
#include <string>

namespace crucible {
	using namespace std;

	void redirect_stdin(const Fd &child_fd);
	void redirect_stdin_stdout(const Fd &child_fd);
	void redirect_stdin_stdout_stderr(const Fd &child_fd);
	void redirect_stdout(const Fd &child_fd);
	void redirect_stdout_stderr(const Fd &child_fd);

	// Open a pipe (actually socketpair) to child process, then execute code in that process.
	// e.g. popen([] () { system("echo Hello, World!"); });
	// Forked process will exit when function returns.
	Fd popen(function<int()> f, function<void(const Fd &child_fd)> import_fd_fn = redirect_stdin_stdout);

	// Read all the data from fd into a string
        string read_all(Fd fd, size_t max_bytes = numeric_limits<size_t>::max(), size_t chunk_bytes = 4096);
};

#endif // CRUCIBLE_EXECPIPE_H
