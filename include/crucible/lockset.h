#ifndef CRUCIBLE_LOCKSET_H
#define CRUCIBLE_LOCKSET_H

#include <crucible/error.h>

#include <cassert>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <set>

namespace crucible {
	using namespace std;

	template <class T>
	class LockSet {

	public:
		using key_type = T;
		using set_type = set<T>;

	private:

		set_type			m_set;
		mutex				m_mutex;
		condition_variable		m_condvar;

	public:
		~LockSet();
		LockSet() = default;

		void lock(const key_type &name);
		void unlock(const key_type &name);
		bool try_lock(const key_type &name);
		size_t size();
		bool empty();
		set_type copy();
		void wait_unlock(double interval);

		class Lock {
			LockSet		&m_lockset;
			key_type	m_name;
			bool		m_locked;

			Lock() = delete;
			Lock(const Lock &) = delete;
			Lock& operator=(const Lock &) = delete;
		public:
			~Lock();
			Lock(LockSet &lockset, const key_type &m_name, bool start_locked = true);
			Lock(Lock &&that);
			Lock& operator=(Lock &&that);
			void lock();
			void unlock();
			bool try_lock();
		};

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
	void
	LockSet<T>::lock(const key_type &name)
	{
		unique_lock<mutex> lock(m_mutex);
		while (m_set.count(name)) {
			m_condvar.wait(lock);
		}
		auto rv = m_set.insert(name);
		THROW_CHECK0(runtime_error, rv.second);
	}

	template <class T>
	bool
	LockSet<T>::try_lock(const key_type &name)
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_set.count(name)) {
			return false;
		}
		auto rv = m_set.insert(name);
		THROW_CHECK1(runtime_error, name, rv.second);
		return true;
	}

	template <class T>
	void
	LockSet<T>::unlock(const key_type &name)
	{
		unique_lock<mutex> lock(m_mutex);
		m_condvar.notify_all();
		auto erase_count = m_set.erase(name);
		THROW_CHECK1(invalid_argument, erase_count, erase_count == 1);
	}

	template <class T>
	void
	LockSet<T>::wait_unlock(double interval)
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_set.empty()) return;
		m_condvar.wait_for(lock, chrono::duration<double>(interval));
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
		return m_set;
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
	LockSet<T>::Lock::Lock(Lock &&that) :
		m_lockset(that.lockset),
		m_name(that.m_name),
		m_locked(that.m_locked)
	{
		that.m_locked = false;
	}

	template <class T>
	typename LockSet<T>::Lock &
	LockSet<T>::Lock::operator=(Lock &&that)
	{
		THROW_CHECK2(invalid_argument, &m_lockset, &that.m_lockset, &m_lockset == &that.m_lockset);
		if (m_locked && that.m_name != m_name) {
			unlock();
		}
		m_name = that.m_name;
		m_locked = that.m_locked;
		that.m_locked = false;
		return *this;
	}

}

#endif // CRUCIBLE_LOCKSET_H
