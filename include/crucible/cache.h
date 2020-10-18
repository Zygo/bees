#ifndef CRUCIBLE_CACHE_H
#define CRUCIBLE_CACHE_H

#include "crucible/lockset.h"

#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <tuple>

namespace crucible {
	using namespace std;

	template <class Return, class... Arguments>
	class LRUCache {
	public:
		using Key = tuple<Arguments...>;
		using Func = function<Return(Arguments...)>;
	private:
		struct Value {
			Key key;
			Return ret;
		};

		using ListIter = typename list<Value>::iterator;

		Func			m_fn;
		list<Value>		m_list;
		map<Key, ListIter>	m_map;
		LockSet<Key>		m_lockset;
		size_t			m_max_size;
		mutex			m_mutex;

		void check_overflow();
		void recent_use(ListIter vp);
		void erase_item(ListIter vp);
		void erase_key(const Key &k);
		Return insert_item(Func fn, Arguments... args);
	public:
		LRUCache(Func f = Func(), size_t max_size = 100);

		void func(Func f);
		void max_size(size_t new_max_size);

		Return operator()(Arguments... args);
		Return refresh(Arguments... args);
		void expire(Arguments... args);
		void insert(const Return &r, Arguments... args);
		void clear();
	};

	template <class Return, class... Arguments>
	LRUCache<Return, Arguments...>::LRUCache(Func f, size_t max_size) :
		m_fn(f),
		m_max_size(max_size)
	{
	}

	template <class Return, class... Arguments>
	Return
	LRUCache<Return, Arguments...>::insert_item(Func fn, Arguments... args)
	{
		Key k(args...);

		// Do we have it cached?
		unique_lock<mutex> lock(m_mutex);
		auto found = m_map.find(k);
		if (found == m_map.end()) {
			// No, release cache lock and acquire key lock
			lock.unlock();
			auto key_lock = m_lockset.make_lock(k);

			// Did item appear in cache while we were waiting for key?
			lock.lock();
			found = m_map.find(k);
			if (found == m_map.end()) {

				// No, we now hold key and cache locks, but item not in cache.
				// Release cache lock and call the function
				lock.unlock();

				// Create new value
				Value v {
					.key = k,
					.ret = fn(args...),
				};

				// Reacquire cache lock
				lock.lock();

				// Make room
				check_overflow();

				// Insert return value at back of LRU list (hot end)
				auto new_item = m_list.insert(m_list.end(), v);

				// Insert return value in map
				bool inserted = false;
				tie(found, inserted) = m_map.insert(make_pair(v.key, new_item));

				// We (should be) holding a lock on this key so we are the ones to insert it
				THROW_CHECK0(runtime_error, inserted);
			}

			// Item should be in cache now
			THROW_CHECK0(runtime_error, found != m_map.end());
		} else {
			// Move to end of LRU
			recent_use(found->second);
		}

		// Return cached object
		return found->second->ret;
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::erase_item(ListIter vp)
	{
		if (vp != m_list.end()) {
			m_map.erase(vp->key);
			m_list.erase(vp);
		}
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::erase_key(const Key &k)
	{
		auto map_item = m_map.find(k);
		if (map_item != m_map.end()) {
			auto list_item = map_item->second;
			m_map.erase(map_item);
			m_list.erase(list_item);
		}
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::check_overflow()
	{
		// Erase items at front of LRU list (cold end) until max size reached or list empty
		while (m_map.size() >= m_max_size && !m_list.empty()) {
			erase_item(m_list.begin());
		}
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::recent_use(ListIter vp)
	{
		// Splice existing items at back of LRU list (hot end)
		auto next_vp = vp;
		++next_vp;
		m_list.splice(m_list.end(), m_list, vp, next_vp);
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::max_size(size_t new_max_size)
	{
		unique_lock<mutex> lock(m_mutex);
		m_max_size = new_max_size;
		// FIXME:  this really reduces the cache size to new_max_size - 1
		// because every other time we call this method, it is immediately
		// followed by insert.
		check_overflow();
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::func(Func func)
	{
		unique_lock<mutex> lock(m_mutex);
		m_fn = func;
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::clear()
	{
		// Move the map and list onto the stack, then destroy it after we've released the lock
		// so that we don't block other threads if the list's destructors are expensive
		decltype(m_list) new_list;
		decltype(m_map) new_map;
		unique_lock<mutex> lock(m_mutex);
		m_list.swap(new_list);
		m_map.swap(new_map);
		lock.unlock();
	}

	template<class Return, class... Arguments>
	Return
	LRUCache<Return, Arguments...>::operator()(Arguments... args)
	{
		return insert_item(m_fn, args...);
	}

	template<class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::expire(Arguments... args)
	{
		unique_lock<mutex> lock(m_mutex);
		erase_key(Key(args...));
	}

	template<class Return, class... Arguments>
	Return
	LRUCache<Return, Arguments...>::refresh(Arguments... args)
	{
		expire(args...);
		return operator()(args...);
	}

	template<class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::insert(const Return &r, Arguments... args)
	{
		insert_item([&](Arguments...) -> Return { return r; }, args...);
	}
}

#endif // CRUCIBLE_CACHE_H
