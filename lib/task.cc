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

	static const size_t thread_name_length = 15; // TASK_COMM_LEN on Linux

	void
	pthread_setname(const string &name)
	{
		auto name_copy = name.substr(0, thread_name_length);
		// Don't care if a debugging facility fails
		pthread_setname_np(pthread_self(), name_copy.c_str());
	}

	string
	pthread_getname()
	{
		char buf[thread_name_length + 1] = { 0 };
		// We'll get an empty name if this fails...
		pthread_getname_np(pthread_self(), buf, sizeof(buf));
		// ...or at least null-terminated garbage
		buf[thread_name_length] = '\0';
		return buf;
	}

	class TaskState;
	using TaskStatePtr = shared_ptr<TaskState>;
	using TaskStateWeak = weak_ptr<TaskState>;

	class TaskConsumer;
	using TaskConsumerPtr = shared_ptr<TaskConsumer>;
	using TaskConsumerWeak = weak_ptr<TaskConsumer>;

	using TaskQueue = list<TaskStatePtr>;

	static thread_local TaskStatePtr tl_current_task;

	/// because we don't want to bump -std=c++-17 just to get scoped_lock.
	/// Also we don't want to self-deadlock if both mutexes are the same mutex.
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
		mutex					m_mutex;
		const function<void()> 			m_exec_fn;
		const string				m_title;

		/// Tasks to be executed after the current task is executed
		list<TaskStatePtr>			m_post_exec_queue;

		/// Set by run(), append(), and insert().  Cleared by exec().
		bool					m_run_now = false;

		/// Set by insert().  Cleared by exec() and destructor.
		bool					m_sort_queue = false;

		/// Set when task starts execution by exec().
		/// Cleared when exec() ends.
		bool					m_is_running = false;

		/// Set when task is queued while already running.
		/// Cleared when task is requeued.
		bool					m_run_again = false;

		/// Set when task is queued as idle task while already running.
		/// Cleared when task is queued as non-idle task.
		bool					m_idle = false;

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
		static void rescue_queue(TaskQueue &tq, const bool sort_queue);

		TaskState &operator=(const TaskState &) = delete;
		TaskState(const TaskState &) = delete;
		TaskState(TaskState &&) = delete;

	public:
		~TaskState();
		TaskState(string title, function<void()> exec_fn);

		/// Run the task at most one more time.  If task has
		/// already started running, a new instance is scheduled.
		/// If an instance is already scheduled by run() or
		/// append(), does nothing.  Otherwise, schedules a new
		/// instance at the end of TaskMaster's global queue.
		void run();

		/// Run the task when there are no more Tasks on the main queue.
		void idle();

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

		/// Queue task to execute after current task finishes executing
		/// or is destroyed, in task ID order.
		void insert(const TaskStatePtr &task);

		/// How masy Tasks are there?  Good for catching leaks
		static size_t instance_count();
	};

	atomic<TaskId> TaskState::s_next_id;
	atomic<size_t> TaskState::s_instance_count;

	class TaskMasterState : public enable_shared_from_this<TaskMasterState> {
		mutex 					m_mutex;
		condition_variable 			m_condvar;
		TaskQueue				m_queue;
		TaskQueue				m_idle_queue;
		size_t					m_thread_max;
		size_t					m_thread_min = 0;
		set<TaskConsumerPtr>			m_threads;
		shared_ptr<thread>			m_load_tracking_thread;
		double					m_load_target = 0;
		double					m_prev_loadavg;
		size_t					m_configured_thread_max;
		double					m_thread_target;
		bool					m_cancelled = false;
		bool					m_paused = false;
		TaskMaster::LoadStats			m_load_stats;

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
		void pause(bool paused = true);

		TaskMasterState &operator=(const TaskMasterState &) = delete;
		TaskMasterState(const TaskMasterState &) = delete;

	public:
		~TaskMasterState();
		TaskMasterState(size_t thread_max = thread::hardware_concurrency());

		static void push_back(const TaskStatePtr &task);
		static void push_back_idle(const TaskStatePtr &task);
		static void push_front(TaskQueue &queue);
		size_t get_queue_count();
		size_t get_thread_count();
		static TaskMaster::LoadStats get_current_load();
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
	TaskState::rescue_queue(TaskQueue &queue, const bool sort_queue)
	{
		if (queue.empty()) {
			return;
		}
		const auto &tlcc = tl_current_consumer;
		if (tlcc) {
			// We are executing under a TaskConsumer, splice our post-exec queue at front.
			// No locks needed because we are using only thread-local objects.
			tlcc->m_local_queue.splice(tlcc->m_local_queue.begin(), queue);
			if (sort_queue) {
				tlcc->m_local_queue.sort([&](const TaskStatePtr &a, const TaskStatePtr &b) {
					return a->m_id < b->m_id;
				});
			}
		} else {
			// We are not executing under a TaskConsumer.
			// If there is only one task, then just insert it at the front of the queue.
			if (queue.size() == 1) {
				TaskMasterState::push_front(queue);
			} else {
				// If there are multiple tasks, create a new task to wrap our post-exec queue,
				// then push it to the front of the global queue using normal locking methods.
				TaskStatePtr rescue_task(make_shared<TaskState>("rescue_task", [](){}));
				swap(rescue_task->m_post_exec_queue, queue);
				// Do the sort--once--when a new Consumer has picked up the Task
				rescue_task->m_sort_queue = sort_queue;
				TaskQueue tq_one { rescue_task };
				TaskMasterState::push_front(tq_one);
			}
		}
		assert(queue.empty());
	}

	TaskState::~TaskState()
	{
		--s_instance_count;
		unique_lock<mutex> lock(m_mutex);
		// If any dependent Tasks were appended since the last exec, run them now
		TaskState::rescue_queue(m_post_exec_queue, m_sort_queue);
		// No need to clear m_sort_queue here, it won't exist soon
	}

	TaskState::TaskState(string title, function<void()> exec_fn) :
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
		for (auto &i : tq) {
			i->clear();
		}
		tq.clear();
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
		if (!task->m_run_now) {
			task->m_run_now = true;
			append_nolock(task);
		}
		task->m_idle = false;
	}

	void
	TaskState::insert(const TaskStatePtr &task)
	{
		THROW_CHECK0(invalid_argument, task);
		THROW_CHECK2(invalid_argument, m_id, task->m_id, m_id != task->m_id);
		PairLock lock(m_mutex, task->m_mutex);
		if (!task->m_run_now) {
			task->m_run_now = true;
			// Move the task and its post-exec queue to follow this task,
			// and request a sort of the flattened list.
			m_sort_queue = true;
			m_post_exec_queue.push_back(task);
			m_post_exec_queue.splice(m_post_exec_queue.end(), task->m_post_exec_queue);
		}
		task->m_idle = false;
	}

	void
	TaskState::exec()
	{
		THROW_CHECK0(invalid_argument, m_exec_fn);
		THROW_CHECK0(invalid_argument, !m_title.empty());

		unique_lock<mutex> lock(m_mutex);
		if (m_is_running) {
			m_run_again = true;
			return;
		} else {
			m_run_now = false;
			m_is_running = true;
		}

		TaskStatePtr this_task = shared_from_this();
		swap(this_task, tl_current_task);
		lock.unlock();

		const auto old_thread_name = pthread_getname();
		pthread_setname(m_title);

		catch_all([&]() {
			m_exec_fn();
		});

		pthread_setname(old_thread_name);

		lock.lock();
		swap(this_task, tl_current_task);
		m_is_running = false;

		if (m_run_again) {
			m_run_again = false;
			if (m_idle) {
				// All the way back to the end of the line
				TaskMasterState::push_back_idle(shared_from_this());
			} else {
				// Insert after any dependents waiting for this Task
				m_post_exec_queue.push_back(shared_from_this());
			}
		}

		// Splice task post_exec queue at front of local queue
		TaskState::rescue_queue(m_post_exec_queue, m_sort_queue);
		m_sort_queue = false;
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
		m_idle = false;
		if (m_run_now) {
			return;
		}
		m_run_now = true;
		if (m_is_running) {
			m_run_again = true;
		} else {
			TaskMasterState::push_back(shared_from_this());
		}
	}

	void
	TaskState::idle()
	{
		unique_lock<mutex> lock(m_mutex);
		m_idle = true;
		if (m_run_now) {
			return;
		}
		m_run_now = true;
		if (m_is_running) {
			m_run_again = true;
		} else {
			TaskMasterState::push_back_idle(shared_from_this());
		}
	}

	TaskMasterState::TaskMasterState(size_t thread_max) :
		m_thread_max(thread_max),
		m_configured_thread_max(thread_max),
		m_thread_target(thread_max),
		m_load_stats(TaskMaster::LoadStats { 0 })
	{
	}

	void
	TaskMasterState::start_threads_nolock()
	{
		while (m_threads.size() < m_thread_max && !m_paused) {
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
	TaskMasterState::push_back_idle(const TaskStatePtr &task)
	{
		THROW_CHECK0(runtime_error, task);
		unique_lock<mutex> lock(s_tms->m_mutex);
		if (s_tms->m_cancelled) {
			task->clear();
			return;
		}
		s_tms->m_idle_queue.push_back(task);
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

	TaskMaster::LoadStats
	TaskMaster::get_current_load()
	{
		unique_lock<mutex> lock(s_tms->m_mutex);
		return s_tms->m_load_stats;
	}

	ostream &
	TaskMaster::print_queue(ostream &os)
	{
		unique_lock<mutex> lock(s_tms->m_mutex);
		auto queue_copy = s_tms->m_queue;
		lock.unlock();
		os << "Queue (size " << queue_copy.size() << "):" << endl;
		size_t counter = 0;
		for (auto i : queue_copy) {
			os << "Queue #" << ++counter << " Task ID " << i->id() << " " << i->title() << endl;
		}
		os << "Queue End" << endl;

		lock.lock();
		queue_copy = s_tms->m_idle_queue;
		lock.unlock();
		os << "Idle (size " << queue_copy.size() << "):" << endl;
		counter = 0;
		for (const auto &i : queue_copy) {
			os << "Idle #" << ++counter << " Task ID " << i->id() << " " << i->title() << endl;
		}
		os << "Idle End" << endl;

		return os;
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

		const double load_deficit = m_load_target - loadavg;
		if (load_deficit > 0) {
			// Load is too low, solve by adding another worker
			m_thread_target += load_deficit / 3;
		} else if (load_deficit < 0) {
			// Load is too high, solve by removing all known excess tasks
			m_thread_target += load_deficit;
		}

		m_load_stats = TaskMaster::LoadStats {
			.current_load = current_load,
			.thread_target = m_thread_target,
			.loadavg = loadavg,
		};

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
		m_paused = true;
		m_cancelled = true;
		decltype(m_queue) empty_queue;
		m_queue.swap(empty_queue);
		empty_queue.splice(empty_queue.end(), m_idle_queue);
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
	TaskMasterState::pause(const bool paused)
	{
		unique_lock<mutex> lock(m_mutex);
		m_paused = paused;
		m_condvar.notify_all();
		if (!m_paused) {
			start_threads_nolock();
		}
		lock.unlock();
	}

	void
	TaskMaster::pause(const bool paused)
	{
		s_tms->pause(paused);
	}

	void
	TaskMasterState::set_thread_min_count(size_t thread_min)
	{
		unique_lock<mutex> lock(m_mutex);
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
		pthread_setname("load_tracker");
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

	Task::Task(string title, function<void()> exec_fn) :
		m_task_state(make_shared<TaskState>(title, exec_fn))
	{
	}

	void
	Task::run() const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		m_task_state->run();
	}

	void
	Task::idle() const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		m_task_state->idle();
	}

	void
	Task::append(const Task &that) const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		THROW_CHECK0(runtime_error, that);
		m_task_state->append(that.m_task_state);
	}

	void
	Task::insert(const Task &that) const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		THROW_CHECK0(runtime_error, that);
		m_task_state->insert(that.m_task_state);
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
		const auto master_copy = m_master;

		// Constructor is running with master locked.
		// Wait until that is done before trying to do anything.
		unique_lock<mutex> lock(master_copy->m_mutex);

		// Detach thread so destructor doesn't call terminate
		m_thread->detach();

		// Set thread name so it isn't empty or the name of some other thread
		pthread_setname("task_consumer");

		// It is now safe to access our own shared_ptr
		TaskConsumerPtr this_consumer = shared_from_this();
		swap(this_consumer, tl_current_consumer);

		while (!master_copy->m_paused) {
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
			} else if (!master_copy->m_idle_queue.empty()) {
				m_current_task = *master_copy->m_idle_queue.begin();
				master_copy->m_idle_queue.pop_front();
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

		// Release lock to rescue queue (may attempt to queue a
		// new task at TaskMaster).  rescue_queue normally sends
		// tasks to the local queue of the current TaskConsumer
		// thread, but we just disconnected ourselves from that.
		// No sorting here because this is not a TaskState.
		lock.unlock();
		TaskState::rescue_queue(m_local_queue, false);

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

	void
	BarrierState::release()
	{
		set<Task> tasks_local;
		unique_lock<mutex> lock(m_mutex);
		swap(tasks_local, m_tasks);
		lock.unlock();
		for (const auto &i : tasks_local) {
			i.run();
		}
	}

	BarrierState::~BarrierState()
	{
		release();
	}

	void
	BarrierState::insert_task(Task t)
	{
		unique_lock<mutex> lock(m_mutex);
		m_tasks.insert(t);
	}

	Barrier::Barrier() :
		m_barrier_state(make_shared<BarrierState>())
	{
	}

	void
	Barrier::insert_task(Task t)
	{
		m_barrier_state->insert_task(t);
	}

	void
	Barrier::release()
	{
		m_barrier_state.reset();
	}

	ExclusionLock::ExclusionLock(shared_ptr<Task> owner) :
		m_owner(owner)
	{
	}

	void
	ExclusionLock::release()
	{
		m_owner.reset();
	}

	ExclusionLock
	Exclusion::try_lock(const Task &task)
	{
		unique_lock<mutex> lock(m_mutex);
		const auto sp = m_owner.lock();
		if (sp) {
			if (task) {
				sp->insert(task);
			}
			return ExclusionLock();
		} else {
			const auto rv = make_shared<Task>(task);
			m_owner = rv;
			return ExclusionLock(rv);
		}
	}

	ExclusionLock::operator bool() const
	{
		return !!m_owner;
	}

}
