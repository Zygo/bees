#ifndef CRUCIBLE_PROGRESS_H
#define CRUCIBLE_PROGRESS_H

#include "crucible/error.h"

#include <functional>
#include <memory>
#include <mutex>
#include <set>

#include <cassert>

namespace crucible {
	using namespace std;

	/// A class to track progress of multiple workers using only two points:
	/// the first and last incomplete state.  The first incomplete
	/// state can be recorded as a checkpoint to resume later on.
	/// The last completed state is the starting point for workers that
	/// need something to do.
	template <class T>
	class ProgressTracker {
		struct ProgressTrackerState;
		class ProgressHolderState;
	public:
		using value_type = T;
		using ProgressHolder = shared_ptr<ProgressHolderState>;

		/// Create ProgressTracker with initial begin and end state 'v'.
		ProgressTracker(const value_type &v);

		/// The first incomplete state.  This is not "sticky",
		/// it will revert to the end state if there are no
		/// items in progress.
		value_type begin() const;

		/// The last incomplete state.  This is "sticky",
		/// it can only increase and never decrease.
		value_type end() const;

		ProgressHolder hold(const value_type &v);

	friend class ProgressHolderState;

	private:
		struct ProgressTrackerState {
			using key_type = pair<value_type, ProgressHolderState *>;
			mutex			m_mutex;
			set<key_type>		m_in_progress;
			value_type		m_begin;
			value_type		m_end;
		};

		class ProgressHolderState {
			shared_ptr<ProgressTrackerState>	m_state;
			const value_type			m_value;
			using key_type = typename ProgressTrackerState::key_type;
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
		const auto rv = m_state->m_in_progress.insert(key_type(m_value, this));
		THROW_CHECK1(runtime_error, m_value, rv.second);
		// Set the beginning to the first existing in-progress item
		m_state->m_begin = m_state->m_in_progress.begin()->first;
		// If this value is past the end, move the end, but don't go backwards
		if (m_state->m_end < m_value) {
			m_state->m_end = m_value;
		}
	}

	template <class T>
	ProgressTracker<T>::ProgressHolderState::~ProgressHolderState()
	{
		unique_lock<mutex> lock(m_state->m_mutex);
		const auto rv = m_state->m_in_progress.erase(key_type(m_value, this));
		// THROW_CHECK2(runtime_error, m_value, rv, rv == 1);
		assert(rv == 1);
		if (m_state->m_in_progress.empty()) {
			// If we made the list empty, then m_begin == m_end
			m_state->m_begin = m_state->m_end;
		} else {
			// If we deleted the first element, then m_begin = current first element
			m_state->m_begin = m_state->m_in_progress.begin()->first;
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
