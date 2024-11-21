#ifndef CRUCIBLE_MULTILOCK_H
#define CRUCIBLE_MULTILOCK_H

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace crucible {
        using namespace std;

	class MultiLocker {
		mutex m_mutex;
		condition_variable m_cv;
		map<string, size_t> m_counters;
		bool m_do_locking = true;

		class LockHandle {
			const string m_type;
			MultiLocker &m_parent;
			bool m_locked = false;
			void set_locked(bool state);
		public:
			~LockHandle();
			LockHandle(const string &type, MultiLocker &parent);
		friend class MultiLocker;
		};

		friend class LockHandle;

		bool is_lock_available(const string &type);
		void put_lock(const string &type);
		shared_ptr<LockHandle> get_lock_private(const string &type);
	public:
		static shared_ptr<LockHandle> get_lock(const string &type);
		static void enable_locking(bool enabled);
	};

}

#endif // CRUCIBLE_MULTILOCK_H
