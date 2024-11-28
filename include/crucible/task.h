#ifndef CRUCIBLE_TASK_H
#define CRUCIBLE_TASK_H

#include <functional>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>

namespace crucible {
	using namespace std;

	class TaskState;

	using TaskId = uint64_t;

	/// A unit of work to be scheduled by TaskMaster.
	class Task {
		shared_ptr<TaskState> m_task_state;

		Task(shared_ptr<TaskState> pts);

	public:

		/// Create empty Task object.
		Task() = default;

		/// Create Task object containing closure and description.
		Task(string title, function<void()> exec_fn);

		/// Schedule Task for at most one future execution.
		/// May run Task in current thread or in other thread.
		/// May run Task before or after returning.
		/// Schedules Task at the end of the global execution queue.
		///
		/// Only one instance of a Task may execute at a time.
		/// If a Task is already scheduled, run() does nothing.
		/// If a Task is already running when a new instance reaches
		/// the front of the queue, the new instance will execute
		/// after the current instance exits.
		void run() const;

		/// Schedule task to run when no other Task is available.
		void idle() const;

		/// Schedule Task to run after this Task has run or
		/// been destroyed.
		void append(const Task &task) const;

		/// Describe Task as text.
		string title() const;

		/// Returns currently executing task if called from exec_fn.
		/// Usually used to reschedule the currently executing Task.
		static Task current_task();

		/// Returns number of currently existing Task objects.
		/// Good for spotting leaks.
		static size_t instance_count();

		/// Ordering operator for containers
		bool operator<(const Task &that) const;

		/// Null test
		operator bool() const;

		/// Unique non-repeating(ish) ID for task
		TaskId id() const;
	};

	ostream &operator<<(ostream &os, const Task &task);

	class TaskMaster {
	public:
		/// Blocks until the running thread count reaches this number
		static void set_thread_count(size_t threads);

		/// Sets minimum thread count when load average tracking enabled
		static void set_thread_min_count(size_t min_threads);

		/// Calls set_thread_count with default
		static void set_thread_count();

		/// Creates thread to track load average and adjust thread count dynamically
		static void set_loadavg_target(double target);

		/// Writes the current non-executing Task queue
		static ostream & print_queue(ostream &);

		/// Writes the current executing Task for each worker
		static ostream & print_workers(ostream &);

		/// Gets the current number of queued Tasks
		static size_t get_queue_count();

		/// Gets the current number of active workers
		static size_t get_thread_count();

		/// Gets the current load tracking statistics
		struct LoadStats {
			/// Current load extracted from last two 5-second load average samples
			double current_load;
			/// Target thread count computed from previous thread count and current load
			double thread_target;
			/// Load average for last 60 seconds
			double loadavg;
		};
		static LoadStats get_current_load();

		/// Drop the current queue and discard new Tasks without
		/// running them.  Currently executing tasks are not
		/// affected (use set_thread_count(0) to wait for those
		/// to complete).
		static void cancel();

		/// Stop running any new Tasks.  All existing
		/// Consumer threads will exit.  Does not affect queue.
		/// Does not wait for threads to exit.  Reversible.
		static void pause(bool paused = true);
	};

	class BarrierState;

	/// Barrier delays the execution of one or more Tasks.
	/// The Tasks are executed when the last shared reference to the
	/// BarrierState is released.  Copies of Barrier objects refer
	/// to the same Barrier state.
	class Barrier {
		shared_ptr<BarrierState> m_barrier_state;

	public:
		Barrier();

		/// Schedule a task for execution when last Barrier is released.
		void insert_task(Task t);

		/// Release this reference to the barrier state.
		/// Last released reference executes the task.
		/// Barrier can only be released once, after which the
		/// object can no longer be used.
		void release();
	};

	class ExclusionLock {
		shared_ptr<Task> m_owner;
		ExclusionLock(shared_ptr<Task> owner);
	friend class Exclusion;
	public:
		/// Explicit default constructor because we have other kinds
		ExclusionLock() = default;

		/// Release this Lock immediately and permanently
		void release();

		/// Test for locked state
		operator bool() const;
	};

	class Exclusion {
		mutex m_mutex;
		weak_ptr<Task> m_owner;

	public:
		/// Attempt to obtain a Lock.  If successful, current Task
		/// owns the Lock until the ExclusionLock is released
		/// (it is the ExclusionLock that owns the lock, so it can
		/// be passed to other Tasks or threads, but this is not
		/// recommended practice).
		/// If not successful, the argument Task is appended to the
		/// task that currently holds the lock.  Current task is
		/// expected to immediately release any other ExclusionLock
		/// objects it holds, and exit its Task function.
		ExclusionLock try_lock(const Task &task);

		/// Execute Task when Exclusion is unlocked (possibly
		/// immediately).
		void insert_task(const Task &t);
	};

	/// Wrapper around pthread_setname_np which handles length limits
	void pthread_setname(const string &name);

	/// Wrapper around pthread_getname_np for symmetry
	string pthread_getname();
}

#endif // CRUCIBLE_TASK_H
