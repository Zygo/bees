#ifndef CRUCIBLE_RESOURCE_H
#define CRUCIBLE_RESOURCE_H

#include "crucible/error.h"

#include <map>
#include <memory>
#include <mutex>
#include <iostream>
#include <stdexcept>

namespace crucible {
	using namespace std;

	// Template classes for non-copiable resource owner objects
	// for objects with process-wide unique names.

	// Everything we need to know about Key and Resource.
	// Specialize this template for your Resource class.
	template <class Key, class Resource>
	struct ResourceTraits {
		// How to get the Key out of a Resource owner.
		// If the owner owns no resource, returns "null" for "no Resource."
		Key get_key(const Resource &res) const;

		// How to construct a new Resource owner given _only_ the key.
		// Usually just calls make_shared<Resource>(key).
		shared_ptr<Resource> make_resource(const Key &key) const;

		// Test a Key value to see if it is null (no active Resource has this Key value).
		// Usually an equality test with get_null_key(), but sometimes many Key values are equivalent to null.
		bool is_null_key(const Key &key) const;

		// is_null_key(get_null_key()) == true
		Key get_null_key() const;
	};

	template <class Key, class Resource>
	class ResourceHandle {
	public:
		using key_type = Key;
		using resource_type = Resource;
		using resource_ptr_type = shared_ptr<Resource>;

	private:
		using traits_type = ResourceTraits<Key, Resource>;
		using weak_ptr_type = weak_ptr<Resource>;
		using map_type = map<key_type, weak_ptr_type>;

		// The only instance variable
		resource_ptr_type m_ptr;

		// A bunch of static variables and functions
		static mutex s_map_mutex;
		static mutex s_ptr_mutex;
		static map_type s_map;
		static resource_ptr_type insert(const key_type &key);
		static resource_ptr_type insert(const resource_ptr_type &res);
		static void clean_locked();
		static ResourceTraits<Key, Resource> s_traits;

	public:

		// Exceptions
		struct duplicate_resource : public invalid_argument {
			key_type m_key;
			key_type get_key() const;
			duplicate_resource(const key_type &key);
		};

		// test for resource.  A separate operator because key_type could be confused with bool.
		bool operator!() const;

		// get key_type for an active resource or null
		key_type get_key() const;

		// conversion/assignment to and from key_type
		operator key_type() const;
		ResourceHandle(const key_type &key);
		ResourceHandle& operator=(const key_type &key);

		// conversion to/from resource_ptr_type
		ResourceHandle(const resource_ptr_type &res);
		ResourceHandle& operator=(const resource_ptr_type &res);

		// default constructor is public and mostly harmless
		ResourceHandle() = default;

		// copy/assign/move/move-assign - with a mutex to help shared_ptr be atomic
		ResourceHandle(const ResourceHandle &that);
		ResourceHandle(ResourceHandle &&that);
		ResourceHandle& operator=(const ResourceHandle &that);
		ResourceHandle& operator=(ResourceHandle &&that);
		~ResourceHandle();

		// forward anything else to the Resource constructor
		// if we can do so unambiguously
		template<class A1, class A2, class... Args>
		ResourceHandle(A1 a1, A2 a2, Args... args) : ResourceHandle( make_shared<Resource>(a1, a2, args...) )
		{
		}

		// forward anything else to a Resource factory method
		template<class... Args>
		static
		ResourceHandle
		make(Args... args) {
			return ResourceHandle( make_shared<Resource>(args...) );
		}

		// get pointer to Resource object (nothrow, result may be null)
		resource_ptr_type get_resource_ptr() const;
		// this version throws
		resource_ptr_type operator->() const;

		// dynamic casting of the resource (throws if cast fails)
		template <class T> shared_ptr<T> cast() const;
	};

	template <class Key, class Resource>
	Key
	ResourceTraits<Key, Resource>::get_key(const Resource &res) const
	{
		return res.get_key();
	}

	template <class Key, class Resource>
	shared_ptr<Resource>
	ResourceTraits<Key, Resource>::make_resource(const Key &key) const
	{
		return make_shared<Resource>(key);
	}

	template <class Key, class Resource>
	bool
	ResourceTraits<Key, Resource>::is_null_key(const Key &key) const
	{
		return !key;
	}

	template <class Key, class Resource>
	Key
	ResourceTraits<Key, Resource>::get_null_key() const
	{
		return NULL;
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::duplicate_resource::duplicate_resource(const key_type &key) :
		invalid_argument("duplicate resource"),
		m_key(key)
	{
	}

	template <class Key, class Resource>
	auto
	ResourceHandle<Key, Resource>::duplicate_resource::get_key() const -> key_type
	{
		return m_key;
	}

	template <class Key, class Resource>
	void
	ResourceHandle<Key, Resource>::clean_locked()
	{
		// Must be called with lock held
		for (auto i = s_map.begin(); i != s_map.end(); ) {
			auto this_i = i;
			++i;
			if (this_i->second.expired()) {
				s_map.erase(this_i);
			}
		}
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::resource_ptr_type
	ResourceHandle<Key, Resource>::insert(const key_type &key)
	{
		// no Resources for null keys
		if (s_traits.is_null_key(key)) {
			return resource_ptr_type();
		}
		unique_lock<mutex> lock(s_map_mutex);
		auto found = s_map.find(key);
		if (found != s_map.end()) {
			resource_ptr_type rv = found->second.lock();
			if (rv) {
				// Use existing Resource
				return rv;
			} else {
				// It's OK for the map to temporarily contain an expired weak_ptr to some dead Resource
				clean_locked();
			}
		}
		// not found or expired, throw any existing ref away and make a new one
		resource_ptr_type rpt = s_traits.make_resource(key);
		// store weak_ptr in map
		s_map[key] = rpt;
		// return shared_ptr
		return rpt;
	};

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::resource_ptr_type
	ResourceHandle<Key, Resource>::insert(const resource_ptr_type &res)
	{
		// no Resources for null keys
		if (!res) {
			return resource_ptr_type();
		}
		key_type key = s_traits.get_key(*res);
		if (s_traits.is_null_key(key)) {
			return resource_ptr_type();
		}
		unique_lock<mutex> lock(s_map_mutex);
		// find Resource for non-null key
		auto found = s_map.find(key);
		if (found != s_map.end()) {
			resource_ptr_type rv = found->second.lock();
			// It's OK for the map to temporarily contain an expired weak_ptr to some dead Resource...
			if (rv) {
				// ...but not a duplicate Resource.
				if (rv.owner_before(res) || res.owner_before(rv)) {
					throw duplicate_resource(key);
				}
				// Use the existing Resource (discard the caller's).
				return rv;
			} else {
				// Clean out expired weak_ptrs
				clean_locked();
			}
		}
		// not found or expired, make a new one or replace old one
		s_map[key] = res;
		return res;
	};

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::ResourceHandle(const key_type &key)
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		m_ptr = insert(key);
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>&
	ResourceHandle<Key, Resource>::operator=(const key_type &key)
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		m_ptr = insert(key);
		return *this;
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::ResourceHandle(const resource_ptr_type &res)
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		m_ptr = insert(res);
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>&
	ResourceHandle<Key, Resource>::operator=(const resource_ptr_type &res)
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		m_ptr = insert(res);
		return *this;
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::ResourceHandle(const ResourceHandle &that)
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		m_ptr = that.m_ptr;
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::ResourceHandle(ResourceHandle &&that)
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		swap(m_ptr, that.m_ptr);
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource> &
	ResourceHandle<Key, Resource>::operator=(ResourceHandle &&that)
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		m_ptr = that.m_ptr;
		that.m_ptr.reset();
		return *this;
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource> &
	ResourceHandle<Key, Resource>::operator=(const ResourceHandle &that)
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		m_ptr = that.m_ptr;
		return *this;
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::~ResourceHandle()
	{
		unique_lock<mutex> lock_ptr(s_ptr_mutex);
		// No pointer, nothing to do
		if (!m_ptr) {
			return;
		}
		// Save key so we can clean the map
		auto key = s_traits.get_key(*m_ptr);
		// Save pointer so we can release lock before deleting
		auto ptr_copy = m_ptr;
		m_ptr.reset();
		// Release lock
		lock_ptr.unlock();
		// Delete our (possibly last) reference to pointer
		ptr_copy.reset();
		// Remove weak_ptr from map if it has expired
		// (and not been replaced in the meantime)
		unique_lock<mutex> lock_map(s_map_mutex);
		auto found = s_map.find(key);
		if (found != s_map.end() && found->second.expired()) {
			s_map.erase(key);
		}
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::resource_ptr_type
	ResourceHandle<Key, Resource>::get_resource_ptr() const
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		// Make isolated copy of pointer with lock held, and return the copy
		auto rv = m_ptr;
		return rv;
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::resource_ptr_type
	ResourceHandle<Key, Resource>::operator->() const
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		if (!m_ptr) {
			THROW_ERROR(out_of_range, __PRETTY_FUNCTION__ << " called on null Resource");
		}
		// Make isolated copy of pointer with lock held, and return the copy
		auto rv = m_ptr;
		return rv;
	}

	template <class Key, class Resource>
	template <class T>
	shared_ptr<T>
	ResourceHandle<Key, Resource>::cast() const
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		shared_ptr<T> dp;
		if (!m_ptr) {
			return dp;
		}
		dp = dynamic_pointer_cast<T>(m_ptr);
		if (!dp) {
			throw bad_cast();
		}
		return dp;
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::key_type
	ResourceHandle<Key, Resource>::get_key() const
	{
		unique_lock<mutex> lock(s_ptr_mutex);
		if (!m_ptr) {
			return s_traits.get_null_key();
		} else {
			return s_traits.get_key(*m_ptr);
		}
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::operator key_type() const
	{
		return get_key();
	}

	template <class Key, class Resource>
	bool
	ResourceHandle<Key, Resource>::operator!() const
	{
		return s_traits.is_null_key(operator key_type());
	}

	// Apparently GCC wants these to be used before they are defined.
	template <class Key, class Resource>
	ResourceTraits<Key, Resource> ResourceHandle<Key, Resource>::s_traits;

	template <class Key, class Resource>
	mutex ResourceHandle<Key, Resource>::s_map_mutex;

	template <class Key, class Resource>
	mutex ResourceHandle<Key, Resource>::s_ptr_mutex;

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::map_type ResourceHandle<Key, Resource>::s_map;


}

#endif // RESOURCE_H
