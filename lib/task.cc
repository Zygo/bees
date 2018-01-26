#include "crucible/task.h"

#include "crucible/cleanup.h"
#include "crucible/error.h"
#include "crucible/process.h"

#include <atomic>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace crucible {
	using namespace std;

	static thread_local weak_ptr<TaskState> tl_current_task_wp;

	class TaskState : public enable_shared_from_this<TaskState> {
		const function<void()> 			m_exec_fn;
		const string				m_title;
		TaskId					m_id;

		static atomic<TaskId>			s_next_id;
	public:
		TaskState(string title, function<void()> exec_fn);

		void exec();
		string title() const;
		TaskId id() const;
	};

	atomic<TaskId> TaskState::s_next_id;

	class TaskConsumer;
	class TaskMasterState;

	class TaskMasterState : public enable_shared_from_this<TaskMasterState> {
		mutex 					m_mutex;
		condition_variable 			m_condvar;
		list<shared_ptr<TaskState>>		m_queue;
		size_t					m_thread_max;
		set<shared_ptr<TaskConsumer>>		m_threads;

	friend class TaskConsumer;
	friend class TaskMaster;

		void start_stop_threads();
		void set_thread_count(size_t thread_max);

	public:
		~TaskMasterState();
		TaskMasterState(size_t thread_max = thread::hardware_concurrency());

		static void push_back(shared_ptr<TaskState> task);
		static void push_front(shared_ptr<TaskState> task);
		size_t get_queue_count();
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
	};

	static shared_ptr<TaskMasterState> s_tms = make_shared<TaskMasterState>();

	TaskState::TaskState(string title, function<void()> exec_fn) :
		m_exec_fn(exec_fn),
		m_title(title),
		m_id(++s_next_id)
	{
		THROW_CHECK0(invalid_argument, !m_title.empty());
	}

	void
	TaskState::exec()
	{
		THROW_CHECK0(invalid_argument, m_exec_fn);
		THROW_CHECK0(invalid_argument, !m_title.empty());

		char buf[24];
		memset(buf, '\0', sizeof(buf));
		DIE_IF_MINUS_ERRNO(pthread_getname_np(pthread_self(), buf, sizeof(buf)));
		Cleanup pthread_name_cleaner([&]() {
			pthread_setname_np(pthread_self(), buf);
		});
		DIE_IF_MINUS_ERRNO(pthread_setname_np(pthread_self(), m_title.c_str()));

		weak_ptr<TaskState> this_task_wp = shared_from_this();
		Cleanup current_task_cleaner([&]() {
			swap(this_task_wp, tl_current_task_wp);
		});
		swap(this_task_wp, tl_current_task_wp);

		m_exec_fn();
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

	TaskMasterState::TaskMasterState(size_t thread_max) :
		m_thread_max(thread_max)
	{
	}

	void
	TaskMasterState::start_stop_threads()
	{
		unique_lock<mutex> lock(m_mutex);
		while (m_threads.size() < m_thread_max) {
			m_threads.insert(make_shared<TaskConsumer>(shared_from_this()));
		}
		while (m_threads.size() > m_thread_max) {
			m_condvar.wait(lock);
		}
	}

	void
	TaskMasterState::push_back(shared_ptr<TaskState> task)
	{
		THROW_CHECK0(runtime_error, task);
		s_tms->start_stop_threads();
		unique_lock<mutex> lock(s_tms->m_mutex);
		s_tms->m_queue.push_back(task);
		s_tms->m_condvar.notify_all();
	}

	void
	TaskMasterState::push_front(shared_ptr<TaskState> task)
	{
		THROW_CHECK0(runtime_error, task);
		s_tms->start_stop_threads();
		unique_lock<mutex> lock(s_tms->m_mutex);
		s_tms->m_queue.push_front(task);
		s_tms->m_condvar.notify_all();
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

	void
	TaskMasterState::set_thread_count(size_t thread_max)
	{
		unique_lock<mutex> lock(m_mutex);

		// If we are reducing the number of threads we have to wake them up so they can exit their loops
		if (thread_max < m_thread_max) {
			m_condvar.notify_all();
		}

		// Lower maximum then release lock
		m_thread_max = thread_max;
		lock.unlock();

		// Wait for threads to be stopped or go start them now
		start_stop_threads();
	}

	void
	TaskMaster::set_thread_count(size_t thread_max)
	{
		s_tms->set_thread_count(thread_max);
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
		TaskMasterState::push_back(m_task_state);
	}

	void
	Task::run_earlier() const
	{
		THROW_CHECK0(runtime_error, m_task_state);
		TaskMasterState::push_front(m_task_state);
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
		while (true) {
			unique_lock<mutex> lock(master_locked->m_mutex);
			if (master_locked->m_thread_max < master_locked->m_threads.size()) {
				break;
			}

			if (master_locked->m_queue.empty()) {
				master_locked->m_condvar.wait(lock);
				continue;
			}

			m_current_task = *master_locked->m_queue.begin();
			master_locked->m_queue.pop_front();
			lock.unlock();
			catch_all([&]() {
				m_current_task->exec();
			});
			lock.lock();
			m_current_task.reset();
		}

		unique_lock<mutex> lock(master_locked->m_mutex);
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
				i.run_earlier();
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
