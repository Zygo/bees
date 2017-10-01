#ifndef CRUCIBLE_LOCKSET_H
#define CRUCIBLE_LOCKSET_H

#include <crucible/cleanup.h>
#include <crucible/error.h>
#include <crucible/process.h>

#include <cassert>

#include <condition_variable>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace crucible {
	using namespace std;

	template <class T>
	class LockSet {

	public:
		using set_type = map<T, pid_t>;
		using key_type = typename set_type::key_type;

	private:

		set_type			m_set;
		mutex				m_mutex;
		condition_variable		m_condvar;
		size_t				m_max_size = numeric_limits<size_t>::max();
		set<uint64_t>			m_priorities;
		uint64_t			m_priority_counter;

		bool full();
		bool first_in_priority(uint64_t my_priority);
		bool locked(const key_type &name);

		class Lock {
			LockSet		&m_lockset;
			key_type	m_name;
			bool		m_locked;

			Lock() = delete;
			Lock(const Lock &) = delete;
			Lock& operator=(const Lock &) = delete;
			Lock(Lock &&that) = delete;
			Lock& operator=(Lock &&that) = delete;
		public:
			~Lock();
			Lock(LockSet &lockset, const key_type &name, bool start_locked = true);
			void lock();
			void unlock();
			bool try_lock();
		};

	public:
		~LockSet();
		LockSet() = default;

		void lock(const key_type &name);
		void unlock(const key_type &name);
		bool try_lock(const key_type &name);
		size_t size();
		bool empty();
		set_type copy();

		void max_size(size_t max);

		class LockHandle {
			shared_ptr<Lock> m_lock;

		public:
			LockHandle(LockSet &lockset, const key_type &name, bool start_locked = true) :
				m_lock(make_shared<Lock>(lockset, name, start_locked)) {}
			void lock() { m_lock->lock(); }
			void unlock() { m_lock->unlock(); }
			bool try_lock() { return m_lock->try_lock(); }
		};

		LockHandle make_lock(const key_type &name, bool start_locked = true);
	};

	template <class T>
	LockSet<T>::~LockSet()
	{
		if (!m_set.empty()) {
			cerr << "ERROR: " << m_set.size() << " locked items still in set at destruction" << endl;
		}
		// We will crash later.  Might as well crash now.
		assert(m_set.empty());
	}

	template <class T>
	bool
	LockSet<T>::full()
	{
		return m_set.size() >= m_max_size;
	}

	template <class T>
	bool
	LockSet<T>::first_in_priority(uint64_t my_priority)
	{
#if 1
		auto counter = m_max_size;
		for (auto i : m_priorities) {
			if (i == my_priority) {
				return true;
			}
			if (++counter > m_max_size) {
				return false;
			}
		}
		THROW_ERROR(runtime_error, "my_priority " << my_priority << " not in m_priorities (size " << m_priorities.size() << ")");
#else
		return *m_priorities.begin() == my_priority;
#endif
	}

	template <class T>
	bool
	LockSet<T>::locked(const key_type &name)
	{
		return m_set.count(name);
	}

	template <class T>
	void
	LockSet<T>::max_size(size_t new_max_size)
	{
		THROW_CHECK1(out_of_range, new_max_size, new_max_size > 0);
		m_max_size = new_max_size;
	}

	template <class T>
	void
	LockSet<T>::lock(const key_type &name)
	{
		unique_lock<mutex> lock(m_mutex);
		auto my_priority = m_priority_counter++;
		Cleanup cleanup([&]() {
			m_priorities.erase(my_priority);
		});
		m_priorities.insert(my_priority);
		while (full() || locked(name) || !first_in_priority(my_priority)) {
			m_condvar.wait(lock);
		}
		auto rv = m_set.insert(make_pair(name, gettid()));
		THROW_CHECK0(runtime_error, rv.second);
		// We removed our priority slot so other threads have to check again
		m_condvar.notify_all();
	}

	template <class T>
	bool
	LockSet<T>::try_lock(const key_type &name)
	{
		unique_lock<mutex> lock(m_mutex);
		if (full() || locked(name)) {
			return false;
		}
		auto rv = m_set.insert(make_pair(name, gettid()));
		THROW_CHECK1(runtime_error, name, rv.second);
		return true;
	}

	template <class T>
	void
	LockSet<T>::unlock(const key_type &name)
	{
		unique_lock<mutex> lock(m_mutex);
		auto erase_count = m_set.erase(name);
		m_condvar.notify_all();
		lock.unlock();
		this_thread::yield();
		THROW_CHECK1(invalid_argument, erase_count, erase_count == 1);
	}

	template <class T>
	size_t
	LockSet<T>::size()
	{
		unique_lock<mutex> lock(m_mutex);
		return m_set.size();
	}

	template <class T>
	bool
	LockSet<T>::empty()
	{
		unique_lock<mutex> lock(m_mutex);
		return m_set.empty();
	}

	template <class T>
	typename LockSet<T>::set_type
	LockSet<T>::copy()
	{
		unique_lock<mutex> lock(m_mutex);
		// Make temporary copy of set while protected by mutex
		auto rv = m_set;
		// Return temporary copy after releasing lock
		return rv;
	}

	template <class T>
	void
	LockSet<T>::Lock::lock()
	{
		if (m_locked) return;
		m_lockset.lock(m_name);
		m_locked = true;
	}

	template <class T>
	bool
	LockSet<T>::Lock::try_lock()
	{
		if (m_locked) return true;
		m_locked = m_lockset.try_lock(m_name);
		return m_locked;
	}

	template <class T>
	void
	LockSet<T>::Lock::unlock()
	{
		if (!m_locked) return;
		m_lockset.unlock(m_name);
		m_locked = false;
	}

	template <class T>
	LockSet<T>::Lock::~Lock()
	{
		if (m_locked) {
			unlock();
		}
	}

	template <class T>
	LockSet<T>::Lock::Lock(LockSet &lockset, const key_type &name, bool start_locked) :
		m_lockset(lockset),
		m_name(name),
		m_locked(false)
	{
		if (start_locked) {
			lock();
		}
	}

	template <class T>
	typename LockSet<T>::LockHandle
	LockSet<T>::make_lock(const key_type &name, bool start_locked)
	{
		return LockHandle(*this, name, start_locked);
	}

}

#endif // CRUCIBLE_LOCKSET_H
