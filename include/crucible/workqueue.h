#ifndef CRUCIBLE_WORKQUEUE_H
#define CRUCIBLE_WORKQUEUE_H

#include <crucible/error.h>

#include <condition_variable>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <set>

namespace crucible {
	using namespace std;

	template <class Task>
	class WorkQueue {

	public:
		using set_type = set<Task>;
		using key_type = Task;

	private:

		set_type		m_set;
		mutable mutex			m_mutex;
		condition_variable	m_cond_full, m_cond_empty;
		size_t			m_max_queue_depth;

	public:
		~WorkQueue();
		template <class... Args> WorkQueue(size_t max_queue_depth, Args... args);
		template <class... Args> WorkQueue(Args... args);

		void push(const key_type &name);
		void push_wait(const key_type &name, size_t limit);
		void push_nowait(const key_type &name);

		key_type pop();
		bool pop_nowait(key_type &rv);
		key_type peek();

		size_t size() const;
		bool empty();
		set_type copy();
		list<Task> peek(size_t count) const;

	};

	template <class Task>
	WorkQueue<Task>::~WorkQueue()
	{
		if (!m_set.empty()) {
			cerr << "ERROR: " << m_set.size() << " locked items still in WorkQueue " << this << " at destruction" << endl;
		}
	}

	template <class Task>
	void
	WorkQueue<Task>::push(const key_type &name)
	{
		unique_lock<mutex> lock(m_mutex);
		while (!m_set.count(name) && m_set.size() > m_max_queue_depth) {
			m_cond_full.wait(lock);
		}
		m_set.insert(name);
		m_cond_empty.notify_all();
	}

	template <class Task>
	void
	WorkQueue<Task>::push_wait(const key_type &name, size_t limit)
	{
		unique_lock<mutex> lock(m_mutex);
		while (!m_set.count(name) && m_set.size() >= limit) {
			m_cond_full.wait(lock);
		}
		m_set.insert(name);
		m_cond_empty.notify_all();
	}

	template <class Task>
	void
	WorkQueue<Task>::push_nowait(const key_type &name)
	{
		unique_lock<mutex> lock(m_mutex);
		m_set.insert(name);
		m_cond_empty.notify_all();
	}

	template <class Task>
	typename WorkQueue<Task>::key_type
	WorkQueue<Task>::pop()
	{
		unique_lock<mutex> lock(m_mutex);
		while (m_set.empty()) {
			m_cond_empty.wait(lock);
		}
		key_type rv = *m_set.begin();
		m_set.erase(m_set.begin());
		m_cond_full.notify_all();
		return rv;
	}

	template <class Task>
	bool
	WorkQueue<Task>::pop_nowait(key_type &rv)
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_set.empty()) {
			return false;
		}
		rv = *m_set.begin();
		m_set.erase(m_set.begin());
		m_cond_full.notify_all();
		return true;
	}

	template <class Task>
	typename WorkQueue<Task>::key_type
	WorkQueue<Task>::peek()
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_set.empty()) {
			return key_type();
		} else {
			// Make copy with lock held
			auto rv = *m_set.begin();
			return rv;
		}
	}

	template <class Task>
	size_t
	WorkQueue<Task>::size() const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_set.size();
	}

	template <class Task>
	bool
	WorkQueue<Task>::empty()
	{
		unique_lock<mutex> lock(m_mutex);
		return m_set.empty();
	}

	template <class Task>
	typename WorkQueue<Task>::set_type
	WorkQueue<Task>::copy()
	{
		unique_lock<mutex> lock(m_mutex);
		auto rv = m_set;
		return rv;
	}

	template <class Task>
	list<Task>
	WorkQueue<Task>::peek(size_t count) const
	{
		unique_lock<mutex> lock(m_mutex);
		list<Task> rv;
		for (auto i : m_set) {
			if (count--) {
				rv.push_back(i);
			} else {
				break;
			}
		}
		return rv;
	}

	template <class Task>
	template <class... Args>
	WorkQueue<Task>::WorkQueue(Args... args) :
		m_set(args...),
		m_max_queue_depth(numeric_limits<size_t>::max())
	{
	}

	template <class Task>
	template <class... Args>
	WorkQueue<Task>::WorkQueue(size_t max_depth, Args... args) :
		m_set(args...),
		m_max_queue_depth(max_depth)
	{
	}

}

#endif // CRUCIBLE_WORKQUEUE_H
