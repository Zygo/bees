#ifndef CRUCIBLE_PROCESS_H
#define CRUCIBLE_PROCESS_H

#include "crucible/resource.h"

#include <functional>
#include <memory>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
	pid_t gettid() throw();
};

namespace crucible {
	using namespace std;

	// Like thread, but for processes.
	// TODO:  thread has a few warts for this usage:
	// - can't create one from its native_handle,
	// - can't destroy one without joining/detaching it first
	// - can't implement detach correctly without crossing threshold of insanity
	// - WTF is native_handle() not const?
	struct Process {
		// These parts are for compatibility with std::thread

		using id = ::pid_t;
		using native_handle_type = ::pid_t;

		~Process();
		Process();

		template <class Fn, class... Args>
		Process(Fn fn, Args... args) :
			Process()
		{
			do_fork(function<int()>([&]() { return fn(args...); }));
		}

		Process(const Process &) = delete;
		Process(Process &&move_from);

		bool joinable();
		void detach();
		native_handle_type native_handle();
		id get_id();

		// Modified thread members for Process

		// join() calls waitpid(), returns status or exception (std::thread returns void)
		using status_type = int;
		status_type join();

		// New members for Process

		// kill() terminates a process in the usual Unix way
		void kill(int sig = SIGTERM);

		// take over ownership of an already-forked native process handle
		Process(id pid);

	private:
		id m_pid;

		void do_fork(function<int()>);
	};

	template <>
	struct ResourceTraits<Process::id, Process> {
		Process::id get_key(const Process &res) const { return (const_cast<Process&>(res)).native_handle(); }
		shared_ptr<Process> make_resource(const Process::id &id) const { return make_shared<Process>(id); }
		bool is_null_key(const Process::id &key) const { return !key; }
		Process::id get_null_key() const { return 0; }
	};

	typedef ResourceHandle<Process::id, Process> Pid;

	double getloadavg1();
	double getloadavg5();
	double getloadavg15();

	string signal_ntoa(int sig);
}
#endif // CRUCIBLE_PROCESS_H
