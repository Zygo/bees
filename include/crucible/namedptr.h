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

	/// A thread-safe container for RAII of shared resources with unique names.

	template <class Return, class... Arguments>
	class NamedPtr {
	public:
		/// The name in "NamedPtr"
		using Key = tuple<Arguments...>;
		/// A shared pointer to the named object with ownership
		/// tracking that erases the object's stored name when
		/// the last shared pointer is destroyed.
		using Ptr = shared_ptr<Return>;
		/// A function that translates a name into a shared pointer to an object.
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
		/// Container for Return pointers.  Destructor removes entry from map.
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

	/// Construct NamedPtr map and define a function to turn a name into a pointer.
	template <class Return, class... Arguments>
	NamedPtr<Return, Arguments...>::NamedPtr(Func f) :
		m_fn(f)
	{
	}

	/// Construct a Value wrapper: the value to store, the argument key to store the value under,
	/// and a pointer to the map.  Everything needed to remove the key from the map when the
	/// last NamedPtr is deleted.  NamedPtr then releases its own pointer to the value, which
	/// may or may not trigger deletion there.
	template <class Return, class... Arguments>
	NamedPtr<Return, Arguments...>::Value::Value(Ptr&& ret_ptr, const Key &key, const MapPtr &map_rep) :
		m_ret_ptr(ret_ptr),
		m_map_rep(map_rep),
		m_ret_key(key)
	{
	}

	/// Destroy a Value wrapper: remove a dead Key from the map, then let the member destructors
	/// do the rest.  The Key might be in the map and not dead, so leave it alone in that case.
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
		const auto found = m_map_rep->m_map.find(m_ret_key);
		if (found != m_map_rep->m_map.end() && found->second.expired()) {
			m_map_rep->m_map.erase(found);
		}
	}

	/// Find a Return by key and fetch a strong Return pointer.
	/// Ignore Keys that have expired weak pointers.
	template <class Return, class... Arguments>
	typename NamedPtr<Return, Arguments...>::Ptr
	NamedPtr<Return, Arguments...>::lookup_item(const Key &k)
	{
		// Must be called with lock held
		const auto found = m_map_rep->m_map.find(k);
		if (found != m_map_rep->m_map.end()) {
			// Get the strong pointer back
			const auto rv = found->second.lock();
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

	/// Insert the Return value of calling Func(Arguments...).
	/// If the value already exists in the map, return the existing value.
	/// If another thread is already running Func(Arguments...) then this thread
	/// will block until the other thread finishes inserting the Return in the
	/// map, and both threads will return the same Return value.
	template <class Return, class... Arguments>
	typename NamedPtr<Return, Arguments...>::Ptr
	NamedPtr<Return, Arguments...>::insert_item(Func fn, Arguments... args)
	{
		Key k(args...);

		// Is it already in the map?
		unique_lock<mutex> lock_lookup(m_map_rep->m_mutex);
		auto rv = lookup_item(k);
		if (rv) {
			return rv;
		}

		// Release map lock and acquire key lock
		lock_lookup.unlock();
		const auto key_lock = m_lockset.make_lock(k);

		// Did item appear in map while we were waiting for key?
		lock_lookup.lock();
		rv = lookup_item(k);
		if (rv) {
			return rv;
		}

		// We now hold key and index locks, but item not in map (or expired).
		// Release map lock so other threads can use the map
		lock_lookup.unlock();

		// Call the function and create a new Value outside of the map
		const auto new_value_ptr = make_shared<Value>(fn(args...), k, m_map_rep);

		// Function must return a non-null pointer
		THROW_CHECK0(runtime_error, new_value_ptr->m_ret_ptr);

		// Reacquire index lock for map insertion.  We still hold the key lock.
		// Use a different lock object to make exceptions unlock in the right order
		unique_lock<mutex> lock_insert(m_map_rep->m_mutex);

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

		// Update the map slot we are sure is empty
		new_item_ref = new_value_ptr;

		// Return shared_ptr to Return using strong pointer's reference counter
		return shared_ptr<Return>(new_value_ptr, new_value_ptr->m_ret_ptr.get());

		// Release map lock, then key lock
	}

	/// (Re)define a function to turn a name into a pointer.
	template <class Return, class... Arguments>
	void
	NamedPtr<Return, Arguments...>::func(Func func)
	{
		unique_lock<mutex> lock(m_map_rep->m_mutex);
		m_fn = func;
	}

	/// Convert a name into a pointer using the configured function.
	template<class Return, class... Arguments>
	typename NamedPtr<Return, Arguments...>::Ptr
	NamedPtr<Return, Arguments...>::operator()(Arguments... args)
	{
		return insert_item(m_fn, args...);
	}

	/// Insert a pointer that has already been created under the
	/// given name.  Useful for inserting a pointer to a derived
	/// class when the name doesn't contain all of the information
	/// required for the object, or when the Return is already known by
	/// some cheaper method than calling the function.
	template<class Return, class... Arguments>
	typename NamedPtr<Return, Arguments...>::Ptr
	NamedPtr<Return, Arguments...>::insert(const Ptr &r, Arguments... args)
	{
		THROW_CHECK0(invalid_argument, r);
		return insert_item([&](Arguments...) { return r; }, args...);
	}

}

#endif // CRUCIBLE_NAMEDPTR_H
