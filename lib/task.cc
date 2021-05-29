#include "crucible/task.h"

#include "crucible/error.h"
#include "crucible/process.h"
#include "crucible/time.h"

#include <atomic>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include <cassert>
#include <cmath>

namespace crucible {
	using namespace std;

	class TaskState;
	using TaskStatePtr = shared_ptr<TaskState>;
	using TaskStateWeak = weak_ptr<TaskState>;

	class TaskConsumer;
	using TaskConsumerPtr = shared_ptr<TaskConsumer>;
	using TaskConsumerWeak = weak_ptr<TaskConsumer>;

	using TaskQueue = list<TaskStatePtr>;

	static thread_local TaskStatePtr tl_current_task;

	/// because we don't want to bump -std=c++-17 just to get scoped_lock
	class PairLock {
		unique_lock<mutex>	m_lock1, m_lock2;
	public:
		PairLock(mutex &m1, mutex &m2) :
			m_lock1(m1, defer_lock),
			m_lock2(m2, defer_lock)
		{
			if (&m1 == &m2) {
				m_lock1.lock();
			} else {
				lock(m_lock1, m_lock2);
			}
		}
	};

	class TaskState : public enable_shared_from_this<TaskState> {
		const int                               m_sched_policy;
		mutex					m_mutex;
		const function<void()> 			m_exec_fn;
		const string				m_title;

		/// Tasks to be executed after the current task is executed
		list<TaskStatePtr>			m_post_exec_queue;

		/// Incremented by run() and append().  Decremented by exec().
		size_t					m_run_count = 0;

		/// Set when task starts execution by exec().
		/// Cleared when exec() ends.
		bool					m_is_running = false;

		/// Sequential identifier for next task
		static atomic<TaskId>			s_next_id;

		/// Sequential identifier for next task
		static atomic<size_t>			s_instance_count;

		/// Identifier for this task
		TaskId					m_id;

		/// Backend for append()
		void append_nolock(const TaskStatePtr &task);

		/// Clear the post-execution queue.  Recursively destroys post-exec
		/// queues of all tasks in post-exec queue.  Useful only when
		/// cancelling the entire task queue.
		void clear();

	friend class TaskMasterState;
	friend class TaskConsumer;

		/// Clear any TaskQueue, not just this one.
		static void clear_queue(TaskQueue &tq);

		/// Rescue any TaskQueue, not just this one.
		static void rescue_queue(TaskQueue &tq);

		TaskState &operator=(const TaskState &) = delete;
		TaskState(const TaskState &) = delete;
		TaskState(TaskState &&) = delete;

	public:
		~TaskState();
		TaskState(string title, int policy, function<void()> exec_fn);

		/// Run the task at most one more time.  If task has
		/// already started running, a new instance is scheduled.
		/// If an instance is already scheduled by run() or
		/// append(), does nothing.  Otherwise, schedules a new
		/// instance at the end of TaskMaster's global queue.
		void run();

		/// Execute task immediately in current thread if it is not already
		/// executing in another thread; otherwise, append the current task
		/// to itself to be executed immediately in the other thread.
		void exec();

		/// Return title of task.
		string title() const;

		/// Return ID of task.
		TaskId id() const;

		/// Queue task to execute after current task finishes executing
		/// or is destroyed.
		void append(const TaskStatePtr &task);

		/// How masy Tasks are there?  Good for catching leaks
		static size_t instance_count();
	};

	atomic<TaskId> TaskState::s_next_id;
	atomic<size_t> TaskState::s_instance_count;

	class TaskMasterState : public enable_shared_from_this<TaskMasterState> {
		mutex 					m_mutex;
		condition_variable 			m_condvar;
		TaskQueue				m_queue;
		size_t					m_thread_max;
		size_t					m_thread_min = 0;
		set<TaskConsumerPtr>			m_threads;
		shared_ptr<thread>			m_load_tracking_thread;
		double					m_load_target = 0;
		double					m_prev_loadavg;
		size_t					m_configured_thread_max;
		double					m_thread_target;
		bool					m_cancelled = false;

	friend class TaskConsumer;
	friend class TaskMaster;

		void start_threads_nolock();
		void start_stop_threads();
		void set_thread_count(size_t thread_max);
		void set_thread_min_count(size_t thread_min);
		void adjust_thread_count();
		size_t calculate_thread_count_nolock();
		void set_loadavg_target(double target);
		void loadavg_thread_fn();
		void cancel();

		TaskMasterState &operator=(const TaskMasterState &) = delete;
		TaskMasterState(const TaskMasterState &) = delete;

	public:
		~TaskMasterState();
		TaskMasterState(size_t thread_max = thread::hardware_concurrency());

		static void push_back(const TaskStatePtr &task);
		static void push_front(TaskQueue &queue);
		size_t get_queue_count();
		size_t get_thread_count();
	};

	class TaskConsumer : public enable_shared_from_this<TaskConsumer> {
		shared_ptr<TaskMasterState>	m_master;
		TaskStatePtr			m_current_task;

	friend class TaskState;
		TaskQueue			m_local_queue;

		void consumer_thread();
		shared_ptr<TaskState> current_task_locked();
	friend class TaskMaster;
	friend class TaskMasterState;
	public:
		TaskConsumer(const shared_ptr<TaskMasterState> &tms);
		shared_ptr<TaskState> current_task();
	private:
		// Make sure this gets constructed _last_
		shared_ptr<thread>		m_thread;
	};

	static thread_local TaskConsumerPtr tl_current_consumer;

	static auto s_tms = make_shared<TaskMasterState>();

	void
	TaskState::rescue_queue(TaskQueue &queue)
	{
		if (queue.empty()) {
			return;
		}
		auto tlcc = tl_current_consumer;
		if (tlcc) {
			// We are executing under a TaskConsumer, splice our post-exec queue at front.
			// No locks needed because we are using only thread-local objects.
			tlcc->m_local_queue.splice(tlcc->m_local_queue.begin(), queue);
		} else {
			// We are not executing under a TaskConsumer.
			// If there is only one task, then just insert it at the front of the queue.
			if (queue.size() == 1) {
				TaskMasterState::push_front(queue);
			} else {
				// If there are multiple tasks, create a new task to wrap our post-exec queue,
				// then push it to the front of the global queue using normal locking methods.
				TaskStatePtr rescue_task(make_shared<TaskState>("rescue_task", SCHED_IDLE, [](){})); //TODO(kakra) Check prio
				swap(rescue_task->m_post_exec_queue, queue);
				TaskQueue tq_one { rescue_task };
				TaskMasterState::push_front(tq_one);
			}
		}
		assert(queue.empty());
	}

	TaskState::~TaskState()
	{
		--s_instance_count;
	}

	TaskState::TaskState(string title, int policy, function<void()> exec_fn) :
		m_sched_policy(policy),
		m_exec_fn(exec_fn),
		m_title(title),
		m_id(++s_next_id)
	{
		THROW_CHECK0(invalid_argument, !m_title.empty());
		++s_instance_count;
	}

	size_t
	TaskState::instance_count()
	{
		return s_instance_count;
	}

	size_t
	Task::instance_count()
	{
		return TaskState::instance_count();
	}

	void
	TaskState::clear()
	{
		TaskQueue post_exec_queue;
		unique_lock<mutex> lock(m_mutex);
		swap(post_exec_queue, m_post_exec_queue);
		lock.unlock();
		clear_queue(post_exec_queue);
	}

	void
	TaskState::clear_queue(TaskQueue &tq)
	{
		while (!tq.empty()) {
			auto i = *tq.begin();
			tq.pop_front();
			i->clear();
		}
	}

	void
	TaskState::append_nolock(const TaskStatePtr &task)
	{
		THROW_CHECK0(invalid_argument, task);
		m_post_exec_queue.push_back(task);
	}

	void
	TaskState::append(const TaskStatePtr &task)
	{
		THROW_CHECK0(invalid_argument, task);
		PairLock lock(m_mutex, task->m_mutex);
		if (!task->m_run_count) {
			++task->m_run_count;
			append_nolock(task);
		}
	}

	void
	TaskState::exec()
	{
		THROW_CHECK0(invalid_argument, m_exec_fn);
		THROW_CHECK0(invalid_argument, !m_title.empty());

		unique_lock<mutex> lock(m_mutex);
		if (m_is_running) {
			append_nolock(shared_from_this());
			return;
		} else {
			--m_run_count;
			m_is_running = true;
		}

		TaskStatePtr this_task = shared_from_this();
		swap(this_task, tl_current_task);
		lock.unlock();

		char buf[24] = { 0 };
		DIE_IF_MINUS_ERRNO(pthread_getname_np(pthread_self(), buf, sizeof(buf)));
		DIE_IF_MINUS_ERRNO(pthread_setname_np(pthread_self(), m_title.c_str()));

		int policy = SCHED_OTHER;
		sched_param param = { .sched_priority = 0 };
		DIE_IF_MINUS_ERRNO(pthread_getschedparam(pthread_self(), &policy, &param));

		catch_all([&]() {
			sched_param param = { .sched_priority = 0 };
			pthread_setschedparam(pthread_self(), m_sched_policy, &param);
			m_exec_fn();
		});

		pthread_setschedparam(pthread_self(), policy, &param);
		pthread_setname_np(pthread_self(), buf);

		lock.lock();
		swap(this_task, tl_current_task);
		m_is_running = false;

		// Splice task post_exec queue at front of local queue
		TaskState::rescue_queue(m_post_exec_queue);
	}

	string
	TaskState::title() const
	{
		THROW_CHECK0(runtime_error, !m_title.empty());
		return m_title;
	}

	TaskId
	TaskState::id() const
	{
		return m_id;
	}

	void
	TaskState::run()
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_run_count) {
			return;
		}
		++m_run_count;
		TaskMasterState::push_back(shared_from_this());
	}

	TaskMasterState::TaskMasterState(size_t thread_max) :
		m_thread_max(thread_max),
		m_configured_thread_max(thread_max),
		m_thread_target(thread_max)
	{
	}

	void
	TaskMasterState::start_threads_nolock()
	{
		while (m_threads.size() < m_thread_max) {
			m_threads.insert(make_shared<TaskConsumer>(shared_from_this()));
		}
	}

	void
	TaskMasterState::start_stop_threads()
	{
		unique_lock<mutex> lock(m_mutex);
		while (m_threads.size() != m_thread_max) {
			if (m_threads.size() < m_thread_max) {
				m_threads.insert(make_shared<TaskConsumer>(shared_from_this()));
			} else if (m_threads.size() > m_thread_max) {
				m_condvar.wait(lock);
			}
		}
	}

	void
	TaskMasterState::push_back(const TaskStatePtr &task)
	{
		THROW_CHECK0(runtime_error, task);
		unique_lock<mutex> lock(s_tms->m_mutex);
		if (s_tms->m_cancelled) {
			task->clear();
			return;
		}
		s_tms->m_queue.push_back(task);
		s_tms->m_condvar.notify_all();
		s_tms->start_threads_nolock();
	}

	void
	TaskMasterState::push_front(TaskQueue &queue)
	{
		if (queue.empty()) {
			return;
		}
		unique_lock<mutex> lock(s_tms->m_mutex);
		if (s_tms->m_cancelled) {
			TaskState::clear_queue(queue);
			return;
		}
		s_tms->m_queue.splice(s_tms->m_queue.begin(), queue);
		s_tms->m_condvar.notify_all();
		s_tms->start_threads_nolock();
	}

	TaskMasterState::~TaskMasterState()
	{
		set_thread_count(0);
	}

	size_t
	TaskMaster::get_queue_count()
	{
		unique_lock<mutex> lock(s_tms->m_mutex);
		return s_tms->m_queue.size();
	}

	size_t
	TaskMaster::get_thread_count()
	{
		unique_lock<mutex> lock(s_tms->m_mutex);
		return s_tms->m_threads.size();
	}

	ostream &
	TaskMaster::print_queue(ostream &os)
	{
		unique_lock<mutex> lock(s_tms->m_mutex);
		os << "Queue (size " << s_tms->m_queue.size() << "):" << endl;
		size_t counter = 0;
		for (auto i : s_tms->m_queue) {
			os << "Queue #" << ++counter << " Task ID " << i->id() << " " << i->title() << endl;
		}
		return os << "Queue End" << endl;
	}

	ostream &
	TaskMaster::print_workers(ostream &os)
	{
		unique_lock<mutex> lock(s_tms->m_mutex);
		os << "Workers (size " << s_tms->m_threads.size() << "):" << endl;
		size_t counter = 0;
		for (auto i : s_tms->m_threads) {
			os << "Worker #" << ++counter << " ";
			auto task = i->current_task_locked();
			if (task) {
				os << "Task ID " << task->id() << " " << task->title();
			} else {
				os << "(idle)";
			}
			os << endl;
		}
		return os << "Workers End" << endl;
	}

	size_t
	TaskMasterState::calculate_thread_count_nolock()
	{
		if (m_cancelled) {
			// No threads running while cancelled
			return 0;
		}

		if (m_load_target == 0) {
			// No limits, no stats, use configured thread count
			return m_configured_thread_max;
		}

		if (m_configured_thread_max == 0) {
			// Not a lot of choice here, and zeros break the algorithm
			return 0;
		}

		const double loadavg = getloadavg1();

		static const double load_exp = exp(-5.0 / 60.0);

		// Averages are fun, but want to know the load from the last 5 seconds.
		// Invert the load average function:
		// LA = LA * load_exp + N * (1 - load_exp)
		// LA2 - LA1 = LA1 * load_exp + N * (1 - load_exp) - LA1
		// LA2 - LA1 + LA1 = LA1 * load_exp + N * (1 - load_exp)
		// LA2 - LA1 + LA1 - LA1 * load_exp = N * (1 - load_exp)
		// LA2 - LA1 * load_exp = N * (1 - load_exp)
		// LA2 / (1 - load_exp) - (LA1 * load_exp / 1 - load_exp) = N
		// (LA2 - LA1 * load_exp) / (1 - load_exp) = N
		// except for rounding error which might make this just a bit below zero.
		const double current_load = max(0.0, (loadavg - m_prev_loadavg * load_exp) / (1 - load_exp));

		m_prev_loadavg = loadavg;

		// Change the thread target based on the
		// difference between current and desired load
		// but don't get too close all at once due to rounding and sample error.
		// If m_load_target < 1.0 then we are just doing PWM with one thread.

		if (m_load_target <= 1.0) {
			m_thread_target = 1.0;
		} else if (m_load_target - current_load >= 1.0) {
			m_thread_target += (m_load_target - current_load - 1.0) / 2.0;
		} else if (m_load_target < current_load) {
			m_thread_target += m_load_target - current_load;
		}

		// Cannot exceed configured maximum thread count or less than zero
		m_thread_target = min(max(0.0, m_thread_target), double(m_configured_thread_max));

		// Convert to integer but keep within range
		const size_t rv = max(m_thread_min, min(size_t(ceil(m_thread_target)), m_configured_thread_max));

		return rv;
	}

	void
	TaskMasterState::adjust_thread_count()
	{
		unique_lock<mutex> lock(m_mutex);
		size_t new_thread_max = calculate_thread_count_nolock();
		size_t old_thread_max = m_thread_max;
		m_thread_max = new_thread_max;

		// If we are reducing the number of threads we have to wake them up so they can exit their loops
		// If we are increasing the number of threads we have to notify start_stop_threads it can stop waiting for threads to stop
		if (new_thread_max != old_thread_max) {
			m_condvar.notify_all();
			start_threads_nolock();
		}
	}

	void
	TaskMasterState::set_thread_count(size_t thread_max)
	{
		unique_lock<mutex> lock(m_mutex);
		// XXX: someday we might want to uncancel, and this would be the place to do it;
		// however, when we cancel we destroy the entire Task queue, and that might be
		// non-trivial to recover from
		if (m_cancelled) {
			return;
		}
		m_configured_thread_max = thread_max;
		lock.unlock();
		adjust_thread_count();
		start_stop_threads();
	}

	void
	TaskMaster::set_thread_count(size_t thread_max)
	{
		s_tms->set_thread_count(thread_max);
	}

	void
	TaskMasterState::cancel()
	{
		unique_lock<mutex> lock(m_mutex);
		m_cancelled = true;
		decltype(m_queue) empty_queue;
		m_queue.swap(empty_queue);
		m_condvar.notify_all();
		lock.unlock();
		TaskState::clear_queue(empty_queue);
	}

	void
	TaskMaster::cancel()
	{
		s_tms->cancel();
	}

	void
	TaskMasterState::set_thread_min_count(size_t thread_min)
	{
		unique_lock<mutex> lock(m_mutex);
		// XXX: someday we might want to uncancel, and this would be the place to do it
		if (m_cancelled) {
			return;
		}
		m_thread_min = thread_min;
		lock.unlock();
		adjust_thread_count();
		start_stop_threads();
	}

	void
	TaskMaster::set_thread_min_count(size_t thread_min)
	{
		s_tms->set_thread_min_count(thread_min);
	}

	void
	TaskMasterState::loadavg_thread_fn()
	{
		pthread_setname_np(pthread_self(), "load_tracker");
		while (!m_cancelled) {
			adjust_thread_count();
			nanosleep(5.0);
		}
	}

	void
	TaskMasterState::set_loadavg_target(double target)
	{
		THROW_CHECK1(out_of_range, target, target >= 0);

		unique_lock<mutex> lock(m_mutex);
		if (m_cancelled) {
			return;
		}
		m_load_target = target;
		m_prev_loadavg = getloadavg1();

		if (target && !m_load_tracking_thread) {
			m_load_tracking_thread = make_shared<thread>([=] () { loadavg_thread_fn(); });
			m_load_tracking_thread->detach();
		}
	}

	void
	TaskMaster::set_loadavg_target(double target)
	{
		s_tms->set_loadavg_target(target);
	}

	void
	TaskMaster::set_thread_count()
	{
		set_thread_count(thread::hardware_concurrency());
	}

	Task::Task(shared_ptr<TaskState> pts) :
		m_task_state(pts)
	{
	}

	Task::Task(string title, int policy, function<void()> exec_fn) :
		m_task_state(make_shared<TaskState>(title, policy, exec_fn))
	{
	}

	void
	Task::run() const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		m_task_state->run();
	}

	void
	Task::append(const Task &that) const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		THROW_CHECK0(runtime_error, that);
		m_task_state->append(that.m_task_state);
	}

	Task
	Task::current_task()
	{
		return Task(tl_current_task);
	}

	string
	Task::title() const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		return m_task_state->title();
	}

	ostream &
	operator<<(ostream &os, const Task &task)
	{
		return os << task.title();
	};

	TaskId
	Task::id() const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		return m_task_state->id();
	}

	bool
	Task::operator<(const Task &that) const
	{
		return id() < that.id();
	}

	Task::operator bool() const
	{
		return !!m_task_state;
	}

	shared_ptr<TaskState>
	TaskConsumer::current_task_locked()
	{
		return m_current_task;
	}

	shared_ptr<TaskState>
	TaskConsumer::current_task()
	{
		unique_lock<mutex> lock(m_master->m_mutex);
		return current_task_locked();
	}

	void
	TaskConsumer::consumer_thread()
	{
		// Keep a copy because we will be destroying *this later
		auto master_copy = m_master;

		// Constructor is running with master locked.
		// Wait until that is done before trying to do anything.
		unique_lock<mutex> lock(master_copy->m_mutex);

		// Detach thread so destructor doesn't call terminate
		m_thread->detach();

		// Set thread name so it isn't empty or the name of some other thread
		DIE_IF_MINUS_ERRNO(pthread_setname_np(pthread_self(), "task_consumer"));

		// It is now safe to access our own shared_ptr
		TaskConsumerPtr this_consumer = shared_from_this();
		swap(this_consumer, tl_current_consumer);

		while (!master_copy->m_cancelled) {
			if (master_copy->m_thread_max < master_copy->m_threads.size()) {
				// We are one of too many threads, exit now
				break;
			}

			if (!m_local_queue.empty()) {
				m_current_task = *m_local_queue.begin();
				m_local_queue.pop_front();
			} else if (!master_copy->m_queue.empty()) {
				m_current_task = *master_copy->m_queue.begin();
				master_copy->m_queue.pop_front();
			} else {
				master_copy->m_condvar.wait(lock);
				continue;
			}

			// Execute task without lock
			lock.unlock();
			catch_all([&]() {
				m_current_task->exec();
			});

			// Update m_current_task with lock
			TaskStatePtr hold_task;
			lock.lock();
			swap(hold_task, m_current_task);

			// Destroy hold_task without lock
			lock.unlock();
			hold_task.reset();

			// Invariant: lock held
			lock.lock();
		}

		// There is no longer a current consumer, but hold our own shared
		// state so it's still there in the destructor
		swap(this_consumer, tl_current_consumer);
		assert(!tl_current_consumer);

		// Release lock to rescue queue (may attempt to queue a new task at TaskMaster).
		// rescue_queue normally sends tasks to the local queue of the current TaskConsumer thread,
		// but we just disconnected ourselves from that.
		lock.unlock();
		TaskState::rescue_queue(m_local_queue);

		// Hold lock so we can erase ourselves
		lock.lock();

		// Fun fact:  shared_from_this() isn't usable until the constructor returns...
		master_copy->m_threads.erase(shared_from_this());
		master_copy->m_condvar.notify_all();
	}

	TaskConsumer::TaskConsumer(const shared_ptr<TaskMasterState> &tms) :
		m_master(tms)
	{
		m_thread = make_shared<thread>([=](){ consumer_thread(); });
	}

	class BarrierState {
		mutex		m_mutex;
		set<Task>	m_tasks;

		void release();
	public:
		~BarrierState();
		void insert_task(Task t);
	};

	Barrier::Barrier(shared_ptr<BarrierState> pbs) :
		m_barrier_state(pbs)
	{
	}

	Barrier::Barrier() :
		m_barrier_state(make_shared<BarrierState>())
	{
	}

	void
	BarrierState::release()
	{
		unique_lock<mutex> lock(m_mutex);
		for (auto i : m_tasks) {
			i.run();
		}
		m_tasks.clear();
	}

	BarrierState::~BarrierState()
	{
		release();
	}

	BarrierLock::BarrierLock(shared_ptr<BarrierState> pbs) :
		m_barrier_state(pbs)
	{
	}

	void
	BarrierLock::release()
	{
		m_barrier_state.reset();
	}

	void
	BarrierState::insert_task(Task t)
	{
		unique_lock<mutex> lock(m_mutex);
		m_tasks.insert(t);
	}

	void
	Barrier::insert_task(Task t)
	{
		m_barrier_state->insert_task(t);
	}

	BarrierLock
	Barrier::lock()
	{
		return BarrierLock(m_barrier_state);
	}

	class ExclusionState {
		mutex		m_mutex;
		bool		m_locked = false;
		Task		m_task;

	public:
		ExclusionState(const string &title);
		~ExclusionState();
		void release();
		bool try_lock();
		void insert_task(Task t);
	};

	Exclusion::Exclusion(shared_ptr<ExclusionState> pbs) :
		m_exclusion_state(pbs)
	{
	}

	Exclusion::Exclusion(const string &title) :
		m_exclusion_state(make_shared<ExclusionState>(title))
	{
	}

	ExclusionState::ExclusionState(const string &title) :
		m_task(title, SCHED_IDLE, [](){}) //TODO(kakra) Check prio
	{
	}

	void
	ExclusionState::release()
	{
		unique_lock<mutex> lock(m_mutex);
		m_locked = false;
		m_task.run();
	}

	ExclusionState::~ExclusionState()
	{
		release();
	}

	ExclusionLock::ExclusionLock(shared_ptr<ExclusionState> pbs) :
		m_exclusion_state(pbs)
	{
	}

	void
	ExclusionLock::release()
	{
		if (m_exclusion_state) {
			m_exclusion_state->release();
			m_exclusion_state.reset();
		}
	}

	ExclusionLock::~ExclusionLock()
	{
		release();
	}

	void
	ExclusionState::insert_task(Task task)
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_locked) {
			// If Exclusion is locked then queue task for release;
			m_task.append(task);
		} else {
			// otherwise, run the inserted task immediately
			task.run();
		}
	}

	bool
	ExclusionState::try_lock()
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_locked) {
			return false;
		} else {
			m_locked = true;
			return true;
		}
	}

	void
	Exclusion::insert_task(Task t)
	{
		m_exclusion_state->insert_task(t);
	}

	ExclusionLock::operator bool() const
	{
		return !!m_exclusion_state;
	}

	ExclusionLock
	Exclusion::try_lock()
	{
		THROW_CHECK0(runtime_error, m_exclusion_state);
		if (m_exclusion_state->try_lock()) {
			return ExclusionLock(m_exclusion_state);
		} else {
			return ExclusionLock();
		}
	}
}
