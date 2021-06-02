#ifndef CRUCIBLE_TASK_H
#define CRUCIBLE_TASK_H

#include <functional>
#include <memory>
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
		Task(string title, int policy, function<void()> exec_fn);

		/// Insert at tail of queue (default).
		void queue_at_tail() const;

		/// Insert at head of queue instead of tail.
		/// May insert onto current thread/CPU core's queue.
		void queue_at_head() const;

		// Add other insertion points here (same CPU, time delay, etc).

		/// Schedule Task at designated queue position.
		/// May run Task in current thread or in other thread.
		/// May run Task before or after returning.
		///
		/// Only one instance of a Task may execute at a time.
		/// If a Task is already scheduled, run() does nothing.
		/// If a Task is already running, run() reschedules the
		/// task after the currently running instance returns.
		void run() const;

		/// Describe Task as text.
		string title() const;

		/// Returns currently executing task if called from exec_fn.
		/// Usually used to reschedule the currently executing Task.
		static Task current_task();

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

		/// Drop the current queue and discard new Tasks without
		/// running them.  Currently executing tasks are not
		/// affected (use set_thread_count(0) to wait for those
		/// to complete).
		static void cancel();
	};

	// Barrier executes waiting Tasks once the last BarrierLock
	// is released.  Multiple unique Tasks may be scheduled while
	// BarrierLocks exist and all will be run() at once upon
	// release.  If no BarrierLocks exist, Tasks are executed
	// immediately upon insertion.

	class BarrierState;

	class BarrierLock {
		shared_ptr<BarrierState> m_barrier_state;
		BarrierLock(shared_ptr<BarrierState> pbs);
	friend class Barrier;
	public:
		// Release this Lock immediately and permanently
		void release();
	};

	class Barrier {
		shared_ptr<BarrierState> m_barrier_state;

		Barrier(shared_ptr<BarrierState> pbs);
	public:
		Barrier();

		// Prevent execution of tasks behind barrier until
		// BarrierLock destructor or release() method is called.
		BarrierLock lock();

		// Schedule a task for execution when no Locks exist
		void insert_task(Task t);
	};

	// Exclusion provides exclusive access to a ExclusionLock.
	// One Task will be able to obtain the ExclusionLock; other Tasks
	// may schedule themselves for re-execution after the ExclusionLock
	// is released.

	class ExclusionState;
	class Exclusion;

	class ExclusionLock {
		shared_ptr<ExclusionState> m_exclusion_state;
		ExclusionLock(shared_ptr<ExclusionState> pes);
		ExclusionLock() = default;
	friend class Exclusion;
	public:
		// Calls release()
		~ExclusionLock();

		// Release this Lock immediately and permanently
		void release();

		// Test for locked state
		operator bool() const;
	};

	class Exclusion {
		shared_ptr<ExclusionState> m_exclusion_state;

		Exclusion(shared_ptr<ExclusionState> pes);
	public:
		Exclusion();

		// Attempt to obtain a Lock.  If successful, current Task
		// owns the Lock until the ExclusionLock is released
		// (it is the ExclusionLock that owns the lock, so it can
		// be passed to other Tasks or threads, but this is not
		// recommended practice).
		// If not successful, current Task is expected to call
		// insert_task(current_task()), release any ExclusionLock
		// objects it holds, and exit its Task function.
		ExclusionLock try_lock();

		// Execute Task when Exclusion is unlocked (possibly
		// immediately).  First Task is scheduled at head,
		// all others are scheduled at tail.
		void insert_task(Task t);
	};


}

#endif // CRUCIBLE_TASK_H
