#ifndef CRUCIBLE_TIME_H
#define CRUCIBLE_TIME_H

#include "crucible/error.h"

#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <ostream>

namespace crucible {

	/// Sleep for approximately @p secs seconds.
	/// Returns the actual elapsed sleep time in seconds.
	double nanosleep(double secs);

	/// Elapsed-time stopwatch.  Measures wall-clock time since
	/// construction or the last reset() call.
	class Timer {
		chrono::high_resolution_clock::time_point m_start;

	public:
		Timer();
		/// Elapsed time in seconds since construction or last reset().
		double age() const;
		/// Raw time point recorded at construction or last reset().
		chrono::high_resolution_clock::time_point get() const;
		/// Elapsed time in seconds, rounded to the nearest 1/@p precision seconds.
		double report(int precision = 1000) const;
		/// Reset the start time to now.
		void reset();
		/// Return elapsed time in seconds and reset the start time to now.
		double lap();
		/// True if elapsed time is less than @p d seconds.
		bool operator<(double d) const;
		/// True if elapsed time is greater than @p d seconds.
		bool operator>(double d) const;
	};

	/// Format elapsed time as a human-readable string.
	ostream &operator<<(ostream &os, const Timer &t);

	/// Token-bucket rate limiter.  Tokens accumulate at a fixed rate
	/// up to a configurable burst capacity.  Callers sleep until
	/// enough tokens are available to cover the requested cost.
	class RateLimiter {
		Timer	m_timer;
		double	m_rate;
		double	m_burst;
		double  m_tokens = 0.0;
		mutable mutex m_mutex;

		void update_tokens();
		RateLimiter() = delete;
	public:
		/// Construct with @p rate tokens per second and @p burst maximum token capacity.
		RateLimiter(double rate, double burst);
		/// Construct with @p rate tokens per second; burst capacity equals the rate.
		RateLimiter(double rate);
		/// Block until @p cost tokens are available, then consume them.
		void sleep_for(double cost = 1.0);
		/// Return seconds to wait for @p cost tokens to become available.
		double sleep_time(double cost = 1.0);
		/// Return true if at least one token is available without sleeping.
		bool is_ready();
		/// Consume @p cost tokens immediately without sleeping; balance may go negative.
		void borrow(double cost = 1.0);
		/// Set the token accumulation rate in tokens per second.
		void rate(double new_rate);
		/// Return the current token accumulation rate in tokens per second.
		double rate() const;
	};

	/// Adaptive rate estimator for a monotonically increasing integer counter.
	/// Tracks a decaying weighted average of the counter's rate of change and
	/// provides blocking waits and time estimates for future count values.
	///
	/// Decay is applied continuously in wall-clock time: each call to update()
	/// applies a factor of @c pow(decay, elapsed_seconds) to the accumulated
	/// numerator and denominator, where @c elapsed_seconds is the time since the
	/// previous update.  This ensures that the effective half-life of the
	/// estimate is independent of how frequently update() is called.
	///
	/// The @p decay parameter is the per-second decay factor.  The default 0.99
	/// gives a time constant of ~100 seconds (half-life ~69 seconds).
	class RateEstimator {
		mutable mutex m_mutex;
		mutable condition_variable m_condvar;
		Timer m_timer;
		double m_num = 0.0;
		double m_den = 0.0;
		uint64_t m_last_count = numeric_limits<uint64_t>::max();
		Timer m_last_update;
		double m_decay;
		Timer m_last_decay;
		double m_min_delay;
		double m_max_delay;

		chrono::duration<double> duration_unlocked(uint64_t relative_count) const;
		chrono::high_resolution_clock::time_point time_point_unlocked(uint64_t absolute_count) const;
		double rate_unlocked() const;
		pair<double, double> ratio_unlocked() const;
		void update_unlocked(uint64_t new_count);
	public:
		/// Construct with @p min_delay and @p max_delay bounding the polling
		/// interval in seconds used by wait_for() and wait_until(), and
		/// @p decay as the per-second exponential decay factor (default 0.99).
		RateEstimator(double min_delay = 1, double max_delay = 3600, double decay = 0.99);

		/// Block until the count has increased by at least @p new_count_relative.
		void wait_for(uint64_t new_count_relative) const;
		/// Block until the count reaches or exceeds @p new_count_absolute.
		void wait_until(uint64_t new_count_absolute) const;

		/// Estimated rate of change in counts per second.
		double rate() const;
		/// Weighted numerator and denominator underlying the rate estimate.
		pair<double, double> ratio() const;

		/// Raw (unweighted) internal numerator and denominator of the rate estimate.
		pair<double, double> raw() const;

		/// Record a new absolute count value and wake any waiting threads.
		void update(uint64_t new_count);

		/// Record a new count value, ignoring values less than the current count.
		void update_monotonic(uint64_t new_count);

		/// Return the most recently recorded count value.
		uint64_t count() const;

		/// Atomically increment the count by @p more and wake any waiting threads.
		void increment(uint64_t more = 1);

		/// Estimate the wall-clock time when the count will reach @p absolute_count.
		chrono::high_resolution_clock::time_point time_point(uint64_t absolute_count) const;
		/// Estimate the time duration for the count to increase by @p relative_count.
		chrono::duration<double> duration(uint64_t relative_count) const;

		/// Estimate seconds until the count increases by @p new_count_relative,
		/// clamped to [min_delay, max_delay].
		double seconds_for(uint64_t new_count_relative) const;
		/// Estimate seconds until the count reaches @p new_count_absolute,
		/// clamped to [min_delay, max_delay].
		double seconds_until(uint64_t new_count_absolute) const;
	};

	/// Format the RateEstimator state as a human-readable string.
	ostream &
	operator<<(ostream &os, const RateEstimator &re);

}

#endif // CRUCIBLE_TIME_H
