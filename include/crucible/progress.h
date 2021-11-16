#ifndef CRUCIBLE_PROGRESS_H
#define CRUCIBLE_PROGRESS_H

#include "crucible/error.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace crucible {
	using namespace std;

	template <class T>
	class ProgressTracker {
		struct ProgressTrackerState;
		class ProgressHolderState;
	public:
		using value_type = T;
		using ProgressHolder = shared_ptr<ProgressHolderState>;

		ProgressTracker(const value_type &v);
		value_type begin() const;
		value_type end() const;

		ProgressHolder hold(const value_type &v);

	friend class ProgressHolderState;

	private:
		struct ProgressTrackerState {
			using key_type = pair<value_type, ProgressHolderState *>;
			mutex			m_mutex;
			map<key_type, bool>	m_in_progress;
			value_type		m_begin;
			value_type		m_end;
		};

		class ProgressHolderState {
			shared_ptr<ProgressTrackerState>	m_state;
			const value_type			m_value;
		public:
			ProgressHolderState(shared_ptr<ProgressTrackerState> state, const value_type &v);
			~ProgressHolderState();
			value_type get() const;
		};


		shared_ptr<ProgressTrackerState>	m_state;
	};

	template <class T>
	typename ProgressTracker<T>::value_type
	ProgressTracker<T>::begin() const
	{
		unique_lock<mutex> lock(m_state->m_mutex);
		return m_state->m_begin;
	}

	template <class T>
	typename ProgressTracker<T>::value_type
	ProgressTracker<T>::end() const
	{
		unique_lock<mutex> lock(m_state->m_mutex);
		return m_state->m_end;
	}

	template <class T>
	typename ProgressTracker<T>::value_type
	ProgressTracker<T>::ProgressHolderState::get() const
	{
		return m_value;
	}

	template <class T>
	ProgressTracker<T>::ProgressTracker(const ProgressTracker::value_type &t) :
		m_state(make_shared<ProgressTrackerState>())
	{
		m_state->m_begin = t;
		m_state->m_end = t;
	}

	template <class T>
	ProgressTracker<T>::ProgressHolderState::ProgressHolderState(shared_ptr<ProgressTrackerState> state, const value_type &v) :
		m_state(state),
		m_value(v)
	{
		unique_lock<mutex> lock(m_state->m_mutex);
		m_state->m_in_progress[make_pair(m_value, this)] = true;
		if (m_state->m_end < m_value) {
			m_state->m_end = m_value;
		}
	}

	template <class T>
	ProgressTracker<T>::ProgressHolderState::~ProgressHolderState()
	{
		unique_lock<mutex> lock(m_state->m_mutex);
		m_state->m_in_progress[make_pair(m_value, this)] = false;
		auto p = m_state->m_in_progress.begin();
		while (p != m_state->m_in_progress.end()) {
			if (p->second) {
				break;
			}
			if (m_state->m_begin < p->first.first) {
				m_state->m_begin = p->first.first;
			}
			m_state->m_in_progress.erase(p);
			p = m_state->m_in_progress.begin();
		}
	}

	template <class T>
	shared_ptr<typename ProgressTracker<T>::ProgressHolderState>
	ProgressTracker<T>::hold(const value_type &v)
	{
		return make_shared<ProgressHolderState>(m_state, v);
	}

}

#endif // CRUCIBLE_PROGRESS_H
