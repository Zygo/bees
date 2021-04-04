#ifndef CRUCIBLE_POOL_H
#define CRUCIBLE_POOL_H

#include "crucible/error.h"

#include <functional>
#include <list>
#include <memory>
#include <mutex>

namespace crucible {
	using namespace std;

	/// Storage for reusable anonymous objects that are too expensive to create and/or destroy frequently

	template <class T>
	class Pool {
	public:
		using Ptr = shared_ptr<T>;
		using Generator = function<Ptr()>;
		using Checker = function<void(Ptr)>;

		~Pool();
		Pool(Generator f = Generator(), Checker checkin = Checker(), Checker checkout = Checker());

		/// Function to create new objects when Pool is empty
		void generator(Generator f);

		/// Optional function called when objects exit the pool (user handle is created and returned to user)
		void checkout(Checker f);

		/// Optional function called when objects enter the pool (last user handle is destroyed)
		void checkin(Checker f);

		/// Pool() returns a handle to an object of type shared_ptr<T>
		Ptr operator()();

		/// Destroy all objects in Pool that are not in use
		void clear();

	private:
		struct PoolRep {
			list<Ptr>	m_list;
			mutex		m_mutex;
			Checker		m_checkin;
			PoolRep(Checker checkin);
		};
		struct Handle {
			weak_ptr<PoolRep> m_list_rep;
			Ptr	            m_ret_ptr;
			Handle(shared_ptr<PoolRep> list_rep, Ptr ret_ptr);
			~Handle();
		};

		Generator		m_fn;
		Checker			m_checkout;
		shared_ptr<PoolRep>	m_list_rep;
	};

	template <class T>
	Pool<T>::PoolRep::PoolRep(Checker checkin) :
		m_checkin(checkin)
	{
	}

	template <class T>
	Pool<T>::Pool(Generator f, Checker checkin, Checker checkout) :
		m_fn(f),
		m_checkout(checkout),
		m_list_rep(make_shared<PoolRep>(checkin))
	{
	}

	template <class T>
	Pool<T>::~Pool()
	{
		auto list_rep = m_list_rep;
		unique_lock<mutex> lock(list_rep->m_mutex);
		m_list_rep.reset();
	}

	template <class T>
	Pool<T>::Handle::Handle(shared_ptr<PoolRep> list_rep, Ptr ret_ptr) :
		m_list_rep(list_rep),
		m_ret_ptr(ret_ptr)
	{
	}

	template <class T>
	Pool<T>::Handle::~Handle()
	{
		// Checkin prepares the object for storage and reuse.
		// Neither of those will happen if there is no Pool.
		// If the Pool was destroyed, just let m_ret_ptr expire.
		auto list_rep = m_list_rep.lock();
		if (!list_rep) {
			return;
		}

		unique_lock<mutex> lock(list_rep->m_mutex);
		// If a checkin function is defined, call it
		auto checkin = list_rep->m_checkin;
		if (checkin) {
			lock.unlock();
			checkin(m_ret_ptr);
			lock.lock();
		}

		// Place object back in pool
		list_rep->m_list.push_front(m_ret_ptr);
	}

	template <class T>
	typename Pool<T>::Ptr
	Pool<T>::operator()()
	{
		Ptr rv;

		// Do we have an object in the pool we can return instead?
		unique_lock<mutex> lock(m_list_rep->m_mutex);
		if (m_list_rep->m_list.empty()) {
			// No, release cache lock and call the function
			lock.unlock();

			// Create new value
			rv = m_fn();
		} else {
			rv = m_list_rep->m_list.front();
			m_list_rep->m_list.pop_front();

			// Release lock so we don't deadlock with Handle destructor
			lock.unlock();
		}

		// rv now points to a T object that is not in the list.
		THROW_CHECK0(runtime_error, rv);

		// Construct a shared_ptr for Handle which will refcount the Handle objects
		// and reinsert the T into the Pool when the last Handle is destroyed.
		auto hv = make_shared<Handle>(m_list_rep, rv);

		// If a checkout function is defined, call it
		if (m_checkout) {
			m_checkout(rv);
		}

		// T an alias shared_ptr for the T using Handle's refcount.
		return Ptr(hv, rv.get());
	}

	template <class T>
	void
	Pool<T>::generator(Generator func)
	{
		unique_lock<mutex> lock(m_list_rep->m_mutex);
		m_fn = func;
	}

	template <class T>
	void
	Pool<T>::checkin(Checker func)
	{
		unique_lock<mutex> lock(m_list_rep->m_mutex);
		m_list_rep->m_checkin = func;
	}

	template <class T>
	void
	Pool<T>::checkout(Checker func)
	{
		unique_lock<mutex> lock(m_list_rep->m_mutex);
		m_checkout = func;
	}

	template <class T>
	void
	Pool<T>::clear()
	{
		unique_lock<mutex> lock(m_list_rep->m_mutex);
		m_list_rep->m_list.clear();
	}

}

#endif // POOL_H
