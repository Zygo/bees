#include "crucible/execpipe.h"

#include "crucible/chatter.h"
#include "crucible/error.h"
#include "crucible/process.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace crucible {
	using namespace std;

	void
	redirect_stdin(const Fd &child_fd)
	{
		dup2_or_die(child_fd, STDIN_FILENO);
	}

	void
	redirect_stdin_stdout(const Fd &child_fd)
	{
		dup2_or_die(child_fd, STDOUT_FILENO);
		dup2_or_die(child_fd, STDIN_FILENO);
	}

	void
	redirect_stdin_stdout_stderr(const Fd &child_fd)
	{
		dup2_or_die(child_fd, STDERR_FILENO);
		dup2_or_die(child_fd, STDOUT_FILENO);
		dup2_or_die(child_fd, STDIN_FILENO);
	}

	void
	redirect_stdout_stderr(const Fd &child_fd)
	{
		dup2_or_die(child_fd, STDERR_FILENO);
		dup2_or_die(child_fd, STDOUT_FILENO);
	}

	void
	redirect_stdout(const Fd &child_fd)
	{
		dup2_or_die(child_fd, STDOUT_FILENO);
	}

	void
	redirect_stderr(const Fd &child_fd)
	{
		dup2_or_die(child_fd, STDERR_FILENO);
	}

	Fd popen(function<int()> f, function<void(const Fd &child_fd)> import_fd_fn)
	{
		Fd parent_fd, child_fd;
		{
			pair<Fd, Fd> fd_pair = socketpair_or_die();
			parent_fd = fd_pair.first;
			child_fd = fd_pair.second;
		}

		pid_t fv;
		DIE_IF_MINUS_ONE(fv = fork());

		if (fv) {
			child_fd->close();
			return parent_fd;
		} else {
			int rv = EXIT_FAILURE;
			catch_all([&]() {
				parent_fd->close();
				import_fd_fn(child_fd);
				// system("ls -l /proc/$$/fd/ >&2");

				rv = f();
			});
			_exit(rv);
			cerr << "PID " << getpid() << " TID " << gettid() << "STILL ALIVE" << endl;
			system("ls -l /proc/$$/task/ >&2");
			exit(EXIT_FAILURE);
		}
	}

	string
	read_all(Fd fd, size_t max_bytes, size_t chunk_bytes)
	{
		char buf[chunk_bytes];
		string str;
		size_t rv;
		while (1) {
			read_partial_or_die(fd, static_cast<void *>(buf), chunk_bytes, rv);
			if (rv == 0) {
				break;
			}
			if (max_bytes - str.size() < rv) {
				THROW_ERROR(out_of_range, "Output size limit " << max_bytes << " exceeded by appending " << rv << " bytes read to " << str.size() << " already in string");
			}
			str.append(buf, rv);
		}
		return str;
	}
}
