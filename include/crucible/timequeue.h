#ifndef CRUCIBLE_TIMEQUEUE_H
#define CRUCIBLE_TIMEQUEUE_H

#include <crucible/error.h>
#include <crucible/time.h>

#include <condition_variable>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <set>

namespace crucible {
	using namespace std;

	template <class Task>
	class TimeQueue {

	public:
		using Timestamp = chrono::high_resolution_clock::time_point;

	private:
		struct Item {
			Timestamp m_time;
			unsigned m_id;
			Task m_task;

			bool operator<(const Item &that) const {
				if (m_time < that.m_time) return true;
				if (that.m_time < m_time) return false;
				return m_id < that.m_id;
			}
			static unsigned s_id;

			Item(const Timestamp &time, const Task& task) :
				m_time(time),
				m_id(++s_id),
				m_task(task)
			{
			}

		};

		set<Item>		m_set;
		mutable mutex		m_mutex;
		condition_variable	m_cond_full, m_cond_empty;
		size_t			m_max_queue_depth;

	public:
		~TimeQueue();
		TimeQueue(size_t max_queue_depth = numeric_limits<size_t>::max());

		void push(const Task &task, double delay = 0);
		void push_nowait(const Task &task, double delay = 0);
		Task pop();
		bool pop_nowait(Task &t);
		double when() const;

		size_t size() const;
		bool empty() const;

		list<Task> peek(size_t count) const;
	};

	template <class Task> unsigned TimeQueue<Task>::Item::s_id = 0;

	template <class Task>
	TimeQueue<Task>::~TimeQueue()
	{
		if (!m_set.empty()) {
			cerr << "ERROR: " << m_set.size() << " locked items still in TimeQueue at destruction" << endl;
		}
	}

	template <class Task>
	void
	TimeQueue<Task>::push(const Task &task, double delay)
	{
		Timestamp time = chrono::high_resolution_clock::now() + 
			chrono::duration_cast<chrono::high_resolution_clock::duration>(chrono::duration<double>(delay));
		unique_lock<mutex> lock(m_mutex);
		while (m_set.size() > m_max_queue_depth) {
			m_cond_full.wait(lock);
		}
		m_set.insert(Item(time, task));
		m_cond_empty.notify_all();
	}

	template <class Task>
	void
	TimeQueue<Task>::push_nowait(const Task &task, double delay)
	{
		Timestamp time = chrono::high_resolution_clock::now() + 
			chrono::duration_cast<chrono::high_resolution_clock::duration>(chrono::duration<double>(delay));
		unique_lock<mutex> lock(m_mutex);
		m_set.insert(Item(time, task));
		m_cond_empty.notify_all();
	}

	template <class Task>
	Task
	TimeQueue<Task>::pop()
	{
		unique_lock<mutex> lock(m_mutex);
		while (1) {
			while (m_set.empty()) {
				m_cond_empty.wait(lock);
			}
			Timestamp now = chrono::high_resolution_clock::now();
			if (now > m_set.begin()->m_time) {
				Task rv = m_set.begin()->m_task;
				m_set.erase(m_set.begin());
				m_cond_full.notify_all();
				return rv;
			}
			m_cond_empty.wait_until(lock, m_set.begin()->m_time);
		}
	}

	template <class Task>
	bool
	TimeQueue<Task>::pop_nowait(Task &t)
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_set.empty()) {
			return false;
		}
		Timestamp now = chrono::high_resolution_clock::now();
		if (now <= m_set.begin()->m_time) {
			return false;
		}
		t = m_set.begin()->m_task;
		m_set.erase(m_set.begin());
		m_cond_full.notify_all();
		return true;
	}

	template <class Task>
	double
	TimeQueue<Task>::when() const
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_set.empty()) {
			return numeric_limits<double>::infinity();
		}
		return chrono::duration<double>(m_set.begin()->m_time - chrono::high_resolution_clock::now()).count();
	}

	template <class Task>
	size_t
	TimeQueue<Task>::size() const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_set.size();
	}

	template <class Task>
	bool
	TimeQueue<Task>::empty() const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_set.empty();
	}

	template <class Task>
	list<Task>
	TimeQueue<Task>::peek(size_t count) const
	{
		unique_lock<mutex> lock(m_mutex);
		list<Task> rv;
		auto it = m_set.begin();
		while (count-- && it != m_set.end()) {
			rv.push_back(it->m_task);
			++it;
		}
		return rv;
	}

	template <class Task>
	TimeQueue<Task>::TimeQueue(size_t max_depth) :
		m_max_queue_depth(max_depth)
	{
	}

}

#endif // CRUCIBLE_TIMEQUEUE_H
