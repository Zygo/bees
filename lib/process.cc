#include "crucible/process.h"

#include "crucible/chatter.h"
#include "crucible/error.h"

#include <cstdlib>
#include <utility>

// for gettid()
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <sys/syscall.h>

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

	template<>
	struct ResourceHandle<Process::id, Process>;

	pid_t
	gettid()
	{
		return syscall(SYS_gettid);
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

}
