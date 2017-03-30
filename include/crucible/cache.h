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
		using Time = size_t;
		using Value = pair<Time, Return>;
	private:
		Func		m_fn;
		Time		m_ctr;
		map<Key, Value>	m_map;
		LockSet<Key>	m_lockset;
		size_t		m_max_size;
		mutex		m_mutex;

		bool check_overflow();
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
		m_ctr(0),
		m_max_size(max_size)
	{
	}

	template <class Return, class... Arguments>
	bool
	LRUCache<Return, Arguments...>::check_overflow()
	{
		if (m_map.size() <= m_max_size) {
			return false;
		}
		vector<pair<Key, Time>> key_times;
		key_times.reserve(m_map.size());
		for (auto i : m_map) {
			key_times.push_back(make_pair(i.first, i.second.first));
		}
		sort(key_times.begin(), key_times.end(), [](const pair<Key, Time> &a, const pair<Key, Time> &b) {
			return a.second < b.second;
		});
		for (size_t i = 0; i < key_times.size() / 2; ++i) {
			m_map.erase(key_times[i].first);
		}
		return true;
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::max_size(size_t new_max_size)
	{
		unique_lock<mutex> lock(m_mutex);
		m_max_size = new_max_size;
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
	}

	template <class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::prune(function<bool(const Return &)> pred)
	{
		unique_lock<mutex> lock(m_mutex);
		for (auto it = m_map.begin(); it != m_map.end(); ) {
			auto next_it = ++it;
			if (pred(it.second.second)) {
				m_map.erase(it);
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
				auto ctr_copy = m_ctr++;
				lock.unlock();
				Value v(ctr_copy, m_fn(args...));

				// Reacquire cache lock and insert return value
				lock.lock();
				tie(found, inserted) = m_map.insert(make_pair(k, v));

				// We hold a lock on this key so we are the ones to insert it
				THROW_CHECK0(runtime_error, inserted);

				// Release key lock, keep the cache lock
				key_lock.unlock();

				// Check to see if we have too many items and reduce if so.
				if (check_overflow()) {
					// Reset iterator
					found = m_map.find(k);
				}
			}
		}

		// Item should be in cache now
		THROW_CHECK0(runtime_error, found != m_map.end());

		// We are using this object so update the timestamp
		if (!inserted) {
			found->second.first = m_ctr++;
		}
		// Make copy before releasing lock
		auto rv = found->second.second;
		return rv;
	}

	template<class Return, class... Arguments>
	void
	LRUCache<Return, Arguments...>::expire(Arguments... args)
	{
		Key k(args...);
		unique_lock<mutex> lock(m_mutex);
		m_map.erase(k);
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

				// No, we hold key and cache locks, but item not in cache.
				// Release cache lock and insert the provided return value
				auto ctr_copy = m_ctr++;
				Value v(ctr_copy, r);
				tie(found, inserted) = m_map.insert(make_pair(k, v));

				// We hold a lock on this key so we are the ones to insert it
				THROW_CHECK0(runtime_error, inserted);

				// Release key lock and clean out overflow
				key_lock.unlock();

				// Check to see if we have too many items and reduce if so.
				if (check_overflow()) {
					// Reset iterator
					found = m_map.find(k);
				}
			}
		}

		// Item should be in cache now
		THROW_CHECK0(runtime_error, found != m_map.end());

		// We are using this object so update the timestamp
		if (!inserted) {
			found->second.first = m_ctr++;
		}
	}
}

#endif // CRUCIBLE_CACHE_H
