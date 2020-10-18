#ifndef CRUCIBLE_NAMEDPTR_H
#define CRUCIBLE_NAMEDPTR_H

#include "crucible/lockset.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>

namespace crucible {
	using namespace std;

	/// Storage for objects with unique names

	template <class Return, class... Arguments>
	class NamedPtr {
	public:
		using Key = tuple<Arguments...>;
		using Ptr = shared_ptr<Return>;
		using Func = function<Ptr(Arguments...)>;
	private:
		struct Value;
		using WeakPtr = weak_ptr<Value>;
		using MapType = map<Key, WeakPtr>;
		struct MapRep {
			MapType		m_map;
			mutex		m_mutex;
		};
		using MapPtr = shared_ptr<MapRep>;
		struct Value {
			Ptr	m_ret_ptr;
			MapPtr	m_map_rep;
			Key	m_ret_key;
			~Value();
			Value(Ptr&& ret_ptr, const Key &key, const MapPtr &map_rep);
		};

		Func		m_fn;
		MapPtr		m_map_rep = make_shared<MapRep>();
		LockSet<Key>	m_lockset;

		Ptr lookup_item(const Key &k);
		Ptr insert_item(Func fn, Arguments... args);

	public:
		NamedPtr(Func f = Func());

		void func(Func f);

		Ptr operator()(Arguments... args);
		Ptr insert(const Ptr &r, Arguments... args);
	};

	template <class Return, class... Arguments>
	NamedPtr<Return, Arguments...>::NamedPtr(Func f) :
		m_fn(f)
	{
	}

	template <class Return, class... Arguments>
	NamedPtr<Return, Arguments...>::Value::Value(Ptr&& ret_ptr, const Key &key, const MapPtr &map_rep) :
		m_ret_ptr(ret_ptr),
		m_map_rep(map_rep),
		m_ret_key(key)
	{
	}

	template <class Return, class... Arguments>
	NamedPtr<Return, Arguments...>::Value::~Value()
	{
		unique_lock<mutex> lock(m_map_rep->m_mutex);
		// We are called from the shared_ptr destructor, so we
		// know that the weak_ptr in the map has already expired;
		// however, if another thread already noticed that the
		// map entry expired while we were waiting for the lock,
		// the other thread will have already replaced the map
		// entry with a pointer to some other object, and that
		// object now owns the map entry.  So we do a key lookup
		// here instead of storing a map iterator, and only erase
		// "our" map entry if it exists and is expired.  The other
		// thread would have done the same for us if the race had
		// a different winner.
		auto found = m_map_rep->m_map.find(m_ret_key);
		if (found != m_map_rep->m_map.end() && found->second.expired()) {
			m_map_rep->m_map.erase(found);
		}
	}

	template <class Return, class... Arguments>
	typename NamedPtr<Return, Arguments...>::Ptr
	NamedPtr<Return, Arguments...>::lookup_item(const Key &k)
	{
		// Must be called with lock held
		auto found = m_map_rep->m_map.find(k);
		if (found != m_map_rep->m_map.end()) {
			// Get the strong pointer back
			auto rv = found->second.lock();
			if (rv) {
				// Have strong pointer.  Return value that shares map entry.
				return shared_ptr<Return>(rv, rv->m_ret_ptr.get());
			}
			// Have expired weak pointer.  Another thread is trying to delete it,
			// but we got the lock first.  Leave the map entry alone here.
			// The other thread will erase it, or we will put a different entry
			// in the same map entry.
		}
		return Ptr();
	}

	template <class Return, class... Arguments>
	typename NamedPtr<Return, Arguments...>::Ptr
	NamedPtr<Return, Arguments...>::insert_item(Func fn, Arguments... args)
	{
		Key k(args...);

		// Is it already in the map?
		unique_lock<mutex> lock(m_map_rep->m_mutex);
		auto rv = lookup_item(k);
		if (rv) {
			return rv;
		}

		// Release map lock and acquire key lock
		lock.unlock();
		auto key_lock = m_lockset.make_lock(k);

		// Did item appear in map while we were waiting for key?
		lock.lock();
		rv = lookup_item(k);
		if (rv) {
			return rv;
		}

		// We now hold key and index locks, but item not in map (or expired).
		// Release map lock
		lock.unlock();

		// Call the function and create a new Value
		auto new_value_ptr = make_shared<Value>(fn(args...), k, m_map_rep);
		// Function must return a non-null pointer
		THROW_CHECK0(runtime_error, new_value_ptr->m_ret_ptr);

		// Reacquire index lock for map insertion
		lock.lock();

		// Insert return value in map or overwrite existing
		// empty or expired weak_ptr value.
		WeakPtr &new_item_ref = m_map_rep->m_map[k];

		// We searched the map while holding both locks and
		// found no entry or an expired weak_ptr; therefore, no
		// other thread could have inserted a new non-expired
		// weak_ptr, and the weak_ptr in the map is expired
		// or was default-constructed as a nullptr.  So if the
		// new_item_ref is not expired, we have a bug we need
		// to find and fix.
		assert(new_item_ref.expired());

		// Update the empty map slot
		new_item_ref = new_value_ptr;

		// Drop lock so we don't deadlock in constructor exceptions
		lock.unlock();

		// Return shared_ptr to Return using strong pointer's reference counter
		return shared_ptr<Return>(new_value_ptr, new_value_ptr->m_ret_ptr.get());
	}

	template <class Return, class... Arguments>
	void
	NamedPtr<Return, Arguments...>::func(Func func)
	{
		unique_lock<mutex> lock(m_map_rep->m_mutex);
		m_fn = func;
	}

	template<class Return, class... Arguments>
	typename NamedPtr<Return, Arguments...>::Ptr
	NamedPtr<Return, Arguments...>::operator()(Arguments... args)
	{
		return insert_item(m_fn, args...);
	}

	template<class Return, class... Arguments>
	typename NamedPtr<Return, Arguments...>::Ptr
	NamedPtr<Return, Arguments...>::insert(const Ptr &r, Arguments... args)
	{
		THROW_CHECK0(invalid_argument, r);
		return insert_item([&](Arguments...) -> Ptr { return r; }, args...);
	}

}

#endif // NAMEDPTR_H
