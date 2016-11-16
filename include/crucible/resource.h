#ifndef CRUCIBLE_RESOURCE_H
#define CRUCIBLE_RESOURCE_H

#include "crucible/error.h"

#include <cassert>
#include <map>
#include <memory>
#include <mutex>
#include <iostream>

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

		class ResourceHolder {
			resource_ptr_type m_ptr;
		public:
			~ResourceHolder();
			ResourceHolder(resource_ptr_type that);
			ResourceHolder(const ResourceHolder &that) = default;
			ResourceHolder(ResourceHolder &&that) = default;
			ResourceHolder& operator=(ResourceHolder &&that) = default;
			ResourceHolder& operator=(const ResourceHolder &that) = default;
			resource_ptr_type get_resource_ptr() const;
		};

		using holder_ptr_type = shared_ptr<ResourceHolder>;
		using weak_holder_ptr_type = weak_ptr<ResourceHolder>;
		using map_type = map<key_type, weak_holder_ptr_type>;

		// The only instance variable
		holder_ptr_type m_ptr;

		// A bunch of static variables and functions
		static mutex &s_mutex();
		static shared_ptr<map_type> s_map();
		static holder_ptr_type insert(const key_type &key);
		static holder_ptr_type insert(const resource_ptr_type &res);
		static void erase(const key_type &key);
		static ResourceTraits<Key, Resource> s_traits;

	public:

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

		// default constructor is public
		ResourceHandle() = default;

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
		// this version throws and is probably not thread safe
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
	ResourceHandle<Key, Resource>::ResourceHolder::ResourceHolder(resource_ptr_type that) :
		m_ptr(that)
	{
		// Cannot insert ourselves here since our shared_ptr does not exist yet.
	}

	template <class Key, class Resource>
	mutex &
	ResourceHandle<Key, Resource>::s_mutex()
	{
		static mutex gcc_won_t_instantiate_this_either;
		return gcc_won_t_instantiate_this_either;
	}

	template <class Key, class Resource>
	shared_ptr<typename ResourceHandle<Key, Resource>::map_type>
	ResourceHandle<Key, Resource>::s_map()
	{
		static shared_ptr<map_type> gcc_won_t_instantiate_the_damn_static_vars;
		if (!gcc_won_t_instantiate_the_damn_static_vars) {
			gcc_won_t_instantiate_the_damn_static_vars = make_shared<map_type>();
		}
		return gcc_won_t_instantiate_the_damn_static_vars;
	}

	template <class Key, class Resource>
	void
	ResourceHandle<Key, Resource>::erase(const key_type &key)
	{
		unique_lock<mutex> lock(s_mutex());
		// Resources are allowed to set their Keys to null.
		if (s_traits.is_null_key(key)) {
			// Clean out any dead weak_ptr objects.
			for (auto i = s_map()->begin(); i != s_map()->end(); ) {
				if (! (*i).second.lock()) {
					i = s_map()->erase(i);
				} else {
					++i;
				}
			}
			return;
		}
		auto erased = s_map()->erase(key);
		if (erased != 1) {
			cerr << __PRETTY_FUNCTION__ << ": WARNING: s_map()->erase(" << key << ") returned " << erased << " != 1" << endl;
		}
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::ResourceHolder::~ResourceHolder()
	{
		if (!m_ptr) {
			// Probably something harmless like a failed constructor.
			cerr << __PRETTY_FUNCTION__ << ": WARNING: destroying null m_ptr" << endl;
			return;
		}
		Key key = s_traits.get_key(*m_ptr);
		ResourceHandle::erase(key);
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::holder_ptr_type
	ResourceHandle<Key, Resource>::insert(const key_type &key)
	{
		// no Resources for null keys
		if (s_traits.is_null_key(key)) {
			return holder_ptr_type();
		}
		unique_lock<mutex> lock(s_mutex());
		// find ResourceHolder for non-null key
		auto found = s_map()->find(key);
		if (found != s_map()->end()) {
			holder_ptr_type rv = (*found).second.lock();
			// a weak_ptr may have expired
			if (rv) {
				return rv;
			}
		}
		// not found or expired, throw any existing ref away and make a new one
		resource_ptr_type rpt = s_traits.make_resource(key);
		holder_ptr_type hpt = make_shared<ResourceHolder>(rpt);
		// store weak_ptr in map
		(*s_map())[key] = hpt;
		// return shared_ptr
		return hpt;
	};

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::holder_ptr_type
	ResourceHandle<Key, Resource>::insert(const resource_ptr_type &res)
	{
		// no Resource, no ResourceHolder.
		if (!res) {
			return holder_ptr_type();
		}
		// no ResourceHolders for null keys either.
		key_type key = s_traits.get_key(*res);
		if (s_traits.is_null_key(key)) {
			return holder_ptr_type();
		}
		unique_lock<mutex> lock(s_mutex());
		// find ResourceHolder for non-null key
		auto found = s_map()->find(key);
		if (found != s_map()->end()) {
			holder_ptr_type rv = (*found).second.lock();
			// The map doesn't own the ResourceHolders, the ResourceHandles do.
			// It's OK for the map to contain an expired weak_ptr to some dead ResourceHolder...
			if (rv) {
				// found ResourceHolder, look at pointer
				resource_ptr_type rp = rv->get_resource_ptr();
				// We do not store references to null Resources.
				assert(rp);
				// Key retrieved for an existing object must match key searched or be null.
				key_type found_key = s_traits.get_key(*rp);
				bool found_key_is_null = s_traits.is_null_key(found_key);
				assert(found_key_is_null || found_key == key);
				if (!found_key_is_null) {
					// We do not store references to duplicate resources.
					if (rp.owner_before(res) || res.owner_before(rp)) {
						cerr << "inserting new Resource with existing Key " << key << " not allowed at " << __PRETTY_FUNCTION__ << endl;;
						abort();
						// THROW_ERROR(out_of_range, "inserting new Resource with existing Key " << key << " not allowed at " << __PRETTY_FUNCTION__);
					}
					// rv is good, return it
					return rv;
				}
			}
		}
		// not found or expired, make a new one
		holder_ptr_type rv = make_shared<ResourceHolder>(res);
		s_map()->insert(make_pair(key, weak_holder_ptr_type(rv)));
		// no need to check s_map result, we are either replacing a dead weak_ptr or adding a new one
		return rv;
	};

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::ResourceHandle(const key_type &key)
	{
		m_ptr = insert(key);
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>&
	ResourceHandle<Key, Resource>::operator=(const key_type &key)
	{
		m_ptr = insert(key);
		return *this;
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>::ResourceHandle(const resource_ptr_type &res)
	{
		m_ptr = insert(res);
	}

	template <class Key, class Resource>
	ResourceHandle<Key, Resource>&
	ResourceHandle<Key, Resource>::operator=(const resource_ptr_type &res)
	{
		m_ptr = insert(res);
		return *this;
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::resource_ptr_type
	ResourceHandle<Key, Resource>::ResourceHolder::get_resource_ptr() const
	{
		return m_ptr;
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::resource_ptr_type
	ResourceHandle<Key, Resource>::get_resource_ptr() const
	{
		if (!m_ptr) {
			return resource_ptr_type();
		}
		return m_ptr->get_resource_ptr();
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::resource_ptr_type
	ResourceHandle<Key, Resource>::operator->() const
	{
		resource_ptr_type rp = get_resource_ptr();
		if (!rp) {
			THROW_ERROR(out_of_range, __PRETTY_FUNCTION__ << " called on null Resource");
		}
		return rp;
	}

	template <class Key, class Resource>
	template <class T>
	shared_ptr<T>
	ResourceHandle<Key, Resource>::cast() const
	{
		shared_ptr<T> dp;
		resource_ptr_type rp = get_resource_ptr();
		if (!rp) {
			return dp;
		}
		dp = dynamic_pointer_cast<T>(rp);
		if (!dp) {
			throw bad_cast();
		}
		return dp;
	}

	template <class Key, class Resource>
	typename ResourceHandle<Key, Resource>::key_type
	ResourceHandle<Key, Resource>::get_key() const
	{
		resource_ptr_type rp = get_resource_ptr();
		if (!rp) {
			return s_traits.get_null_key();
		} else {
			return s_traits.get_key(*rp);
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

	template <class Key, class Resource>
	ResourceTraits<Key, Resource> ResourceHandle<Key, Resource>::s_traits;


}

#endif // RESOURCE_H
