#include "crucible/task.h"

#include "crucible/error.h"
#include "crucible/process.h"
#include "crucible/time.h"

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace crucible {
	using namespace std;

	static thread_local weak_ptr<TaskState> tl_current_task_wp;

	class TaskStateLock;

	class TaskState : public enable_shared_from_this<TaskState> {
		const int                               m_sched_policy;
		const function<void()> 			m_exec_fn;
		const string				m_title;
		TaskId					m_id;
		function<void(shared_ptr<TaskState>)>   m_queue_fn;

		mutex					m_mutex;

		/// Set when task is on some queue and does not need to be queued again.
		/// Cleared when exec() begins.
		bool					m_is_queued = false;

		/// Set when task starts execution by exec().
		/// Cleared when exec() completes.
		bool					m_is_running = false;

		/// Set when task is being executed by exec(), but is
		/// already running an earlier call to exec() in a second thread.
		/// The earlier exec() shall requeue the task according to its
		/// queue function when the earlier exec() completes.
		bool					m_run_again = false;

		static atomic<TaskId>			s_next_id;

	friend class TaskStateLock;

	public:
		TaskState(string title, int policy, function<void()> exec_fn);

		/// Queue task for execution according to previously stored queue policy.
		void run();

		/// Execute task immediately in current thread if it is not already
		/// executing in another thread.  If m_run_again is set while m_is_running
		/// is true, the thread that set m_is_running will requeue the task.
		void exec();

		/// Return title of task.
		string title() const;

		/// Return ID of task.
		TaskId id() const;

		/// Set queue policy to queue at head (before existing tasks on queue).
		void queue_at_head();

		/// Set queue policy to queue at tail (after existing tasks on queue).
		void queue_at_tail();

		/// Lock the task's queueing state so a queue can decide whether to
		/// accept the task queue request or discard it.  The decision
		/// is communicated through TaskStateLock::set_queued(bool).
		TaskStateLock lock_queue();
	};

	atomic<TaskId> TaskState::s_next_id;

	class TaskStateLock {
		TaskState				&m_state;
		unique_lock<mutex>			m_lock;
		TaskStateLock(TaskState &state);
	friend class TaskState;
	public:
		/// Returns true if TaskState is currently queued.
		bool is_queued() const;

		/// Sets TaskState::m_queued to a different queued state.
		/// Throws an exception on requests to transition to the current state.
		void set_queued(bool is_queued_now);
	};

	class TaskConsumer;
	class TaskMasterState;

	class TaskMasterState : public enable_shared_from_this<TaskMasterState> {
		mutex 					m_mutex;
		condition_variable 			m_condvar;
		list<shared_ptr<TaskState>>		m_queue;
		size_t					m_thread_max;
		size_t					m_thread_min = 0;
		set<shared_ptr<TaskConsumer>>		m_threads;
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

	public:
		~TaskMasterState();
		TaskMasterState(size_t thread_max = thread::hardware_concurrency());

		static void push_back(shared_ptr<TaskState> task);
		static void push_front(shared_ptr<TaskState> task);
		size_t get_queue_count();
		size_t get_thread_count();
	};

	class TaskConsumer : public enable_shared_from_this<TaskConsumer> {
		weak_ptr<TaskMasterState>	m_master;
		thread				m_thread;
		shared_ptr<TaskState>		m_current_task;

		void consumer_thread();
		shared_ptr<TaskState> current_task_locked();
	public:
		TaskConsumer(weak_ptr<TaskMasterState> tms);
		shared_ptr<TaskState> current_task();
	friend class TaskMaster;
	friend class TaskMasterState;
	};

	static shared_ptr<TaskMasterState> s_tms = make_shared<TaskMasterState>();

	TaskStateLock::TaskStateLock(TaskState &state) :
		m_state(state),
		m_lock(state.m_mutex)
	{
	}

	bool
	TaskStateLock::is_queued() const
	{
		return m_state.m_is_queued;
	}

	void
	TaskStateLock::set_queued(bool is_queued_now)
	{
		THROW_CHECK2(runtime_error, m_state.m_is_queued, is_queued_now, m_state.m_is_queued != is_queued_now);
		m_state.m_is_queued = is_queued_now;
	}

	TaskState::TaskState(string title, int policy, function<void()> exec_fn) :
		m_sched_policy(policy),
		m_exec_fn(exec_fn),
		m_title(title),
		m_id(++s_next_id),
		m_queue_fn(TaskMasterState::push_back)
	{
		THROW_CHECK0(invalid_argument, !m_title.empty());
	}

	void
	TaskState::exec()
	{
		THROW_CHECK0(invalid_argument, m_exec_fn);
		THROW_CHECK0(invalid_argument, !m_title.empty());

		unique_lock<mutex> lock(m_mutex);
		m_is_queued = false;
		if (m_is_running) {
			m_run_again = true;
			return;
		} else {
			m_is_running = true;
		}
		lock.unlock();

		char buf[24] = { 0 };
		DIE_IF_MINUS_ERRNO(pthread_getname_np(pthread_self(), buf, sizeof(buf)));
		DIE_IF_MINUS_ERRNO(pthread_setname_np(pthread_self(), m_title.c_str()));

		sched_param param = { .sched_priority = 0 };
		int policy = SCHED_OTHER;
		DIE_IF_MINUS_ERRNO(pthread_getschedparam(pthread_self(), &policy, &param));

		weak_ptr<TaskState> this_task_wp = shared_from_this();
		swap(this_task_wp, tl_current_task_wp);

		catch_all([&]() {
			sched_param param = { .sched_priority = 0 };
			pthread_setschedparam(pthread_self(), m_sched_policy, &param);
			m_exec_fn();
		});

		swap(this_task_wp, tl_current_task_wp);
		pthread_setschedparam(pthread_self(), policy, &param);
		pthread_setname_np(pthread_self(), buf);

		lock.lock();
		m_is_running = false;
		bool run_again = m_run_again;
		m_run_again = false;
		lock.unlock();
		if (run_again) {
			run();
		}
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
	TaskState::queue_at_head()
	{
		unique_lock<mutex> lock(m_mutex);
		m_queue_fn = TaskMasterState::push_front;
	}

	void
	TaskState::queue_at_tail()
	{
		unique_lock<mutex> lock(m_mutex);
		m_queue_fn = TaskMasterState::push_back;
	}

	TaskStateLock
	TaskState::lock_queue()
	{
		return TaskStateLock(*this);
	}

	void
	TaskState::run()
	{
		unique_lock<mutex> lock(m_mutex);
		THROW_CHECK0(runtime_error, m_queue_fn);
		if (!m_is_queued) {
			m_queue_fn(shared_from_this());
			m_is_queued = true;
		}
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
	TaskMasterState::push_back(shared_ptr<TaskState> task)
	{
		THROW_CHECK0(runtime_error, task);
		unique_lock<mutex> lock(s_tms->m_mutex);
		if (s_tms->m_cancelled) {
			return;
		}
		s_tms->m_queue.push_back(task);
		s_tms->m_condvar.notify_all();
		s_tms->start_threads_nolock();
	}

	void
	TaskMasterState::push_front(shared_ptr<TaskState> task)
	{
		THROW_CHECK0(runtime_error, task);
		unique_lock<mutex> lock(s_tms->m_mutex);
		if (s_tms->m_cancelled) {
			return;
		}
		s_tms->m_queue.push_front(task);
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
		// XXX: someday we might want to uncancel, and this would be the place to do it
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
		sched_param param = { .sched_priority = 0 };
		pthread_setschedparam(pthread_self(), SCHED_ISO, &param);
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
	Task::queue_at_head() const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		m_task_state->queue_at_head();
	}

	void
	Task::queue_at_tail() const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		m_task_state->queue_at_tail();
	}

	Task
	Task::current_task()
	{
		return Task(tl_current_task_wp.lock());
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
		auto master_locked = m_master.lock();
		unique_lock<mutex> lock(master_locked->m_mutex);
		return current_task_locked();
	}

	void
	TaskConsumer::consumer_thread()
	{
		auto master_locked = m_master.lock();

		unique_lock<mutex> lock(master_locked->m_mutex);
		while (!master_locked->m_cancelled) {
			if (master_locked->m_thread_max < master_locked->m_threads.size()) {
				break;
			}

			if (master_locked->m_queue.empty()) {
				master_locked->m_condvar.wait(lock);
				continue;
			}

			m_current_task = *master_locked->m_queue.begin();
			auto hold_task = m_current_task;
			master_locked->m_queue.pop_front();

			// Execute task without lock
			lock.unlock();
			catch_all([&]() {
				m_current_task->exec();
			});

			// Update m_current_task with lock
			lock.lock();
			m_current_task.reset();

			// Destroy hold_task without lock
			lock.unlock();
			hold_task.reset();

			// Invariant: lock held
			lock.lock();
		}

		// Still holding lock
		m_thread.detach();
		master_locked->m_threads.erase(shared_from_this());
		master_locked->m_condvar.notify_all();
	}

	TaskConsumer::TaskConsumer(weak_ptr<TaskMasterState> tms) :
		m_master(tms),
		m_thread([=](){ consumer_thread(); })
	{
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
		set<Task>	m_tasks;

	public:
		~ExclusionState();
		void release();
		bool try_lock();
		void insert_task(Task t);
	};

	Exclusion::Exclusion(shared_ptr<ExclusionState> pbs) :
		m_exclusion_state(pbs)
	{
	}

	Exclusion::Exclusion() :
		m_exclusion_state(make_shared<ExclusionState>())
	{
	}

	void
	ExclusionState::release()
	{
		unique_lock<mutex> lock(m_mutex);
		m_locked = false;
		bool first = true;
		for (auto i : m_tasks) {
			if (first) {
				i.queue_at_head();
				i.run();
				first = false;
			} else {
				i.run();
			}
		}
		m_tasks.clear();
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
		m_tasks.insert(task);
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
