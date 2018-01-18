#ifndef CRUCIBLE_CACHE_H
#define CRUCIBLE_CACHE_H

#include "crucible/lockset.h"

#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

namespace crucible {
	using namespace std;

	template <class Return, class... Arguments>
	class LRUCache {
	public:
		using Key = tuple<Arguments...>;
		using Func = function<Return(Arguments...)>;
	private:
		struct Value {
			Value *fp = nullptr;
			Value *bp = nullptr;
			Key key;
			Return ret;
			Value(Key k, Return r) : key(k), ret(r) { }
			// Crash early!
			~Value() { fp = bp = nullptr; };
		};

		Func		m_fn;
		map<Key, Value>	m_map;
		LockSet<Key>	m_lockset;
		size_t		m_max_size;
		mutex		m_mutex;
		Value		*m_last = nullptr;

		void check_overflow();
		void move_to_front(Value *vp);
		void erase_one(Value *vp);
	public:
		LRUCache(Func f = Func(), size_t max_size = 100);

		void func(Func f);
		void max_size(size_t new_max_size);

		Return operator()(Arguments... args);
		Return refresh(Arguments... args);
		void expire(Arguments... args);
		void prune(function<bool(const Return &)> predicate);
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
	void
	LRUCache<Return, Arguments...>::erase_one(Value *vp)
	{
		THROW_CHECK0(invalid_argument, vp);
		Value *vp_bp = vp->bp;
		THROW_CHECK0(runtime_error, vp_bp);
		Value *vp_fp = vp->fp;
		THROW_CHECK0(runtime_error, vp_fp);
		vp_fp->bp = vp_bp;
		vp_bp->fp = vp_fp;
		// If we delete the head of the list then advance the head by one
		if (vp == m_last) {
			// If the head of the list is also the tail of the list then clear m_last
			if (vp_fp == m_last) {
				m_last = nullptr;
			} else {
				m_last = vp_fp;
			}
		}
		m_map.erase(vp->key);
		if (!m_last) {
			THROW_CHECK0(runtime_error, m_map.empty());
		} else {
			THROW_CHECK0(runtime_error, !m_map.empty());
		}
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::check_overflow()
	{
		while (m_map.size() >= m_max_size) {
			THROW_CHECK0(runtime_error, m_last);
			THROW_CHECK0(runtime_error, m_last->bp);
			erase_one(m_last->bp);
		}
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::move_to_front(Value *vp)
	{
		if (!m_last) {
			// Create new LRU list
			m_last = vp->fp = vp->bp = vp;
		} else if (m_last != vp) {
			Value *vp_fp = vp->fp;
			Value *vp_bp = vp->bp;
			if (vp_fp && vp_bp) {
				// There are at least two and we are removing one that isn't m_last
				// Connect adjacent nodes to each other (has no effect if vp is new), removing vp from list
				vp_fp->bp = vp_bp;
				vp_bp->fp = vp_fp;
			} else {
				// New insertion, both must be null
				THROW_CHECK0(runtime_error, !vp_fp);
				THROW_CHECK0(runtime_error, !vp_bp);
			}
			// Splice new node into list
			Value *last_bp = m_last->bp;
			THROW_CHECK0(runtime_error, last_bp);
			// New elemnt points to both ends of list
			vp->fp = m_last;
			vp->bp = last_bp;
			// Insert vp as fp from the end of the list
			last_bp->fp = vp;
			// Insert vp as bp from the second from the start of the list
			m_last->bp = vp;
			// Update start of list
			m_last = vp;
		}
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
		unique_lock<mutex> lock(m_mutex);
		m_map.clear();
		m_last = nullptr;
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::prune(function<bool(const Return &)> pred)
	{
		unique_lock<mutex> lock(m_mutex);
		for (auto it = m_map.begin(); it != m_map.end(); ) {
			auto next_it = ++it;
			if (pred(it.second.ret)) {
				erase_one(&it.second);
			}
			it = next_it;
		}
	}

	template<class Return, class... Arguments>
	Return
	LRUCache<Return, Arguments...>::operator()(Arguments... args)
	{
		Key k(args...);
		bool inserted = false;

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

				// No, we hold key and cache locks, but item not in cache.
				// Release cache lock and call function
				lock.unlock();

				// Create new value
				Value v(k, m_fn(args...));

				// Reacquire cache lock
				lock.lock();

				// Make room
				check_overflow();

				// Reacquire cache lock and insert return value
				tie(found, inserted) = m_map.insert(make_pair(k, v));

				// We hold a lock on this key so we are the ones to insert it
				THROW_CHECK0(runtime_error, inserted);

				// Release key lock, keep the cache lock
				key_lock.unlock();

			}
		}

		// Item should be in cache now
		THROW_CHECK0(runtime_error, found != m_map.end());

		// (Re)insert at head of LRU
		move_to_front(&(found->second));

		// Make copy before releasing lock
		auto rv = found->second.ret;
		return rv;
	}

	template<class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::expire(Arguments... args)
	{
		Key k(args...);
		unique_lock<mutex> lock(m_mutex);
		auto found = m_map.find(k);
		if (found != m_map.end()) {
			erase_one(&found->second);
		}
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
		Key k(args...);
		bool inserted = false;

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

				// Make room
				check_overflow();

				// No, we hold key and cache locks, but item not in cache.
				// Insert the provided return value (no need to unlock here)
				Value v(k, r);
				tie(found, inserted) = m_map.insert(make_pair(k, v));

				// We hold a lock on this key so we are the ones to insert it
				THROW_CHECK0(runtime_error, inserted);
			}
		}

		// Item should be in cache now
		THROW_CHECK0(runtime_error, found != m_map.end());

		// (Re)insert at head of LRU
		move_to_front(&(found->second));
	}
}

#endif // CRUCIBLE_CACHE_H
