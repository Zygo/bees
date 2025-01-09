#include "crucible/process.h"

#include "crucible/chatter.h"
#include "crucible/error.h"
#include "crucible/ntoa.h"

#include <cstdlib>
#include <utility>

#include <unistd.h>
#include <sys/syscall.h>

extern "C" {
	pid_t
	__attribute__((weak))
	gettid() throw()
	{
		return syscall(SYS_gettid);
	}
};

namespace crucible {
	using namespace std;

	bool
	Process::joinable()
	{
		return !!m_pid;
	}

	Process::~Process()
	{
		if (joinable()) {
			// because it's just not the same without the word "zombie"...
			CHATTER("ZOMBIE WARNING: joinable Process pid " << m_pid << " abandoned");
		}
	}

	Process::Process() :
		m_pid(0)
	{
	}

	Process::Process(Process &&move_from) :
		m_pid(0)
	{
		swap(m_pid, move_from.m_pid);
	}

	void
	Process::do_fork(function<int()> child_func)
	{
		int rv = fork();
		if (rv < 0) {
			THROW_ERRNO("fork failed");
		}
		m_pid = rv;

		if (rv == 0) {
			// child
			catch_all([&]() {
				int rv = child_func();
				exit(rv);
			});
			terminate();
		}
	}

	Process::status_type
	Process::join()
	{
		if (m_pid == 0) {
			THROW_ERROR(invalid_argument, "Process not created");
		}

		int status = 0;
		pid_t rv = waitpid(m_pid, &status, 0);
		if (rv == -1) {
			THROW_ERRNO("waitpid failed, pid = " << m_pid);
		}
		if (rv != m_pid) {
			THROW_ERROR(runtime_error, "waitpid failed, wanted pid = " << m_pid << ", got rv = " << rv << ", status = " << status);
		}
		m_pid = 0;
		return status;
	}

	void
	Process::detach()
	{
		m_pid = 0;
	}

	Process::native_handle_type
	Process::native_handle()
	{
		return m_pid;
	}

	Process::id
	Process::get_id()
	{
		return m_pid;
	}

	void
	Process::kill(int sig)
	{
		if (!m_pid) {
			THROW_ERROR(invalid_argument, "Process not created");
		}

		int rv = ::kill(m_pid, sig);
		if (rv) {
			THROW_ERRNO("killing process " << m_pid << " with signal " << sig);
		}
	}

	double
	getloadavg1()
	{
		double loadavg[1];
		const int rv = ::getloadavg(loadavg, 1);
		if (rv != 1) {
			THROW_ERRNO("getloadavg(..., 1)");
		}
		return loadavg[0];
	}

	double
	getloadavg5()
	{
		double loadavg[2];
		const int rv = ::getloadavg(loadavg, 2);
		if (rv != 2) {
			THROW_ERRNO("getloadavg(..., 2)");
		}
		return loadavg[1];
	}

	double
	getloadavg15()
	{
		double loadavg[3];
		const int rv = ::getloadavg(loadavg, 3);
		if (rv != 3) {
			THROW_ERRNO("getloadavg(..., 3)");
		}
		return loadavg[2];
	}

	static const struct bits_ntoa_table signals_table[] = {

		// POSIX.1-1990
		NTOA_TABLE_ENTRY_ENUM(SIGHUP),
		NTOA_TABLE_ENTRY_ENUM(SIGINT),
		NTOA_TABLE_ENTRY_ENUM(SIGQUIT),
		NTOA_TABLE_ENTRY_ENUM(SIGILL),
		NTOA_TABLE_ENTRY_ENUM(SIGABRT),
		NTOA_TABLE_ENTRY_ENUM(SIGFPE),
		NTOA_TABLE_ENTRY_ENUM(SIGKILL),
		NTOA_TABLE_ENTRY_ENUM(SIGSEGV),
		NTOA_TABLE_ENTRY_ENUM(SIGPIPE),
		NTOA_TABLE_ENTRY_ENUM(SIGALRM),
		NTOA_TABLE_ENTRY_ENUM(SIGTERM),
		NTOA_TABLE_ENTRY_ENUM(SIGUSR1),
		NTOA_TABLE_ENTRY_ENUM(SIGUSR2),
		NTOA_TABLE_ENTRY_ENUM(SIGCHLD),
		NTOA_TABLE_ENTRY_ENUM(SIGCONT),
		NTOA_TABLE_ENTRY_ENUM(SIGSTOP),
		NTOA_TABLE_ENTRY_ENUM(SIGTSTP),
		NTOA_TABLE_ENTRY_ENUM(SIGTTIN),
		NTOA_TABLE_ENTRY_ENUM(SIGTTOU),

		// SUSv2 and POSIX.1-2001
		NTOA_TABLE_ENTRY_ENUM(SIGBUS),
		NTOA_TABLE_ENTRY_ENUM(SIGPOLL),
		NTOA_TABLE_ENTRY_ENUM(SIGPROF),
		NTOA_TABLE_ENTRY_ENUM(SIGSYS),
		NTOA_TABLE_ENTRY_ENUM(SIGTRAP),
		NTOA_TABLE_ENTRY_ENUM(SIGURG),
		NTOA_TABLE_ENTRY_ENUM(SIGVTALRM),
		NTOA_TABLE_ENTRY_ENUM(SIGXCPU),
		NTOA_TABLE_ENTRY_ENUM(SIGXFSZ),

		// Other
		NTOA_TABLE_ENTRY_ENUM(SIGIOT),
#ifdef SIGEMT
		NTOA_TABLE_ENTRY_ENUM(SIGEMT),
#endif
		NTOA_TABLE_ENTRY_ENUM(SIGSTKFLT),
		NTOA_TABLE_ENTRY_ENUM(SIGIO),
#ifdef SIGCLD
		NTOA_TABLE_ENTRY_ENUM(SIGCLD),
#endif
		NTOA_TABLE_ENTRY_ENUM(SIGPWR),
#ifdef SIGINFO
		NTOA_TABLE_ENTRY_ENUM(SIGINFO),
#endif
#ifdef SIGLOST
		NTOA_TABLE_ENTRY_ENUM(SIGLOST),
#endif
		NTOA_TABLE_ENTRY_ENUM(SIGWINCH),
#ifdef SIGUNUSED
		NTOA_TABLE_ENTRY_ENUM(SIGUNUSED),
#endif

		NTOA_TABLE_ENTRY_END(),
	};

	string
	signal_ntoa(int sig)
	{
		return bits_ntoa(sig, signals_table);
	}

}
