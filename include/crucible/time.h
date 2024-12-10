#ifndef CRUCIBLE_TIME_H
#define CRUCIBLE_TIME_H

#include "crucible/error.h"

#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <ostream>

namespace crucible {

	double nanosleep(double secs);

	class Timer {
		chrono::high_resolution_clock::time_point m_start;

	public:
		Timer();
		double age() const;
		chrono::high_resolution_clock::time_point get() const;
		double report(int precision = 1000) const;
		void reset();
		double lap();
		bool operator<(double d) const;
		bool operator>(double d) const;
	};

	ostream &operator<<(ostream &os, const Timer &t);

	class RateLimiter {
		Timer	m_timer;
		double	m_rate;
		double	m_burst;
		double  m_tokens = 0.0;
		mutable mutex m_mutex;

		void update_tokens();
		RateLimiter() = delete;
	public:
		RateLimiter(double rate, double burst);
		RateLimiter(double rate);
		void sleep_for(double cost = 1.0);
		double sleep_time(double cost = 1.0);
		bool is_ready();
		void borrow(double cost = 1.0);
		void rate(double new_rate);
		double rate() const;
	};

	class RateEstimator {
		mutable mutex m_mutex;
		mutable condition_variable m_condvar;
		Timer m_timer;
		double m_num = 0.0;
		double m_den = 0.0;
		uint64_t m_last_count = numeric_limits<uint64_t>::max();
		Timer m_last_update;
		const double m_decay = 0.99;
		Timer m_last_decay;
		double m_min_delay;
		double m_max_delay;

		chrono::duration<double> duration_unlocked(uint64_t relative_count) const;
		chrono::high_resolution_clock::time_point time_point_unlocked(uint64_t absolute_count) const;
		double rate_unlocked() const;
		pair<double, double> ratio_unlocked() const;
		void update_unlocked(uint64_t new_count);
	public:
		RateEstimator(double min_delay = 1, double max_delay = 3600);

		// Block until count reached
		void wait_for(uint64_t new_count_relative) const;
		void wait_until(uint64_t new_count_absolute) const;

		// Computed rates and ratios
		double rate() const;
		pair<double, double> ratio() const;

		// Inspect raw num/den
		pair<double, double> raw() const;

		// Write count
		void update(uint64_t new_count);

		// Ignore counts that go backwards
		void update_monotonic(uint64_t new_count);

		// Read count
		uint64_t count() const;

		/// Increment count (like update(count() + more), but atomic)
		void increment(uint64_t more = 1);

		// Convert counts to chrono types
		chrono::high_resolution_clock::time_point time_point(uint64_t absolute_count) const;
		chrono::duration<double> duration(uint64_t relative_count) const;

		// Polling delay until count reached (limited by min/max delay)
		double seconds_for(uint64_t new_count_relative) const;
		double seconds_until(uint64_t new_count_absolute) const;
	};

	ostream &
	operator<<(ostream &os, const RateEstimator &re);

}

#endif // CRUCIBLE_TIME_H
