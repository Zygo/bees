#include "crucible/multilock.h"

#include "crucible/error.h"

namespace crucible {
	using namespace std;

	MultiLocker::LockHandle::LockHandle(const string &type, MultiLocker &parent) :
		m_type(type),
		m_parent(parent)
	{
	}

	void
	MultiLocker::LockHandle::set_locked(const bool state)
	{
		m_locked = state;
	}

	MultiLocker::LockHandle::~LockHandle()
	{
		if (m_locked) {
			m_parent.put_lock(m_type);
			m_locked = false;
		}
	}

	bool
	MultiLocker::is_lock_available(const string &type)
	{
		for (const auto &i : m_counters) {
			if (i.second != 0 && i.first != type) {
				return false;
			}
		}
		return true;
	}

	void
	MultiLocker::put_lock(const string &type)
	{
		unique_lock<mutex> lock(m_mutex);
		auto &counter = m_counters[type];
		THROW_CHECK2(runtime_error, type, counter, counter > 0);
		--counter;
		if (counter == 0) {
			m_cv.notify_all();
		}
	}

	shared_ptr<MultiLocker::LockHandle>
	MultiLocker::get_lock_private(const string &type)
	{
		unique_lock<mutex> lock(m_mutex);
		m_counters.insert(make_pair(type, size_t(0)));
		while (!is_lock_available(type)) {
			m_cv.wait(lock);
		}
		const auto rv = make_shared<LockHandle>(type, *this);
		++m_counters[type];
		rv->set_locked(true);
		return rv;
	}

	shared_ptr<MultiLocker::LockHandle>
	MultiLocker::get_lock(const string &type)
	{
		static MultiLocker s_process_instance;
		return s_process_instance.get_lock_private(type);
	}

}
