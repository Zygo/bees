#include "crucible/time.h"

#include "crucible/error.h"
#include "crucible/process.h"

#include <algorithm>
#include <thread>

#include <cmath>
#include <ctime>

namespace crucible {

	double
	nanosleep(double secs)
	{
		if (secs <= 0) return secs;

		struct timespec req;
		req.tv_sec = time_t(floor(secs));
		req.tv_nsec = long((secs - floor(secs)) * 1000000000);

		// Just silently ignore weirdo values for now
		if (req.tv_sec < 0) return secs;
		if (req.tv_sec > 1000000000) return secs;
		if (req.tv_nsec < 0) return secs;
		if (req.tv_nsec > 1000000000) return secs;

		struct timespec rem;
		rem.tv_sec = 0;
		rem.tv_nsec = 0;

		int nanosleep_rv = ::nanosleep(&req, &rem);
		if (nanosleep_rv) {
			THROW_ERRNO("nanosleep (" << secs << ") { tv_sec = " << req.tv_sec << ", tv_nsec = " << req.tv_nsec << " }");
		}
		return rem.tv_sec + (double(rem.tv_nsec) / 1000000000.0);
	}

	Timer::Timer() :
		m_start(chrono::high_resolution_clock::now())
	{
	}

	double
	Timer::age() const
	{
		chrono::high_resolution_clock::time_point end = chrono::high_resolution_clock::now();
		return chrono::duration<double>(end - m_start).count();
	}

	double
	Timer::report(int precision) const
	{
		return ceil(age() * precision) / precision;
	}

	void
	Timer::reset()
	{
		m_start = chrono::high_resolution_clock::now();
	}

	chrono::high_resolution_clock::time_point
	Timer::get() const
	{
		return m_start;
	}

	double
	Timer::lap()
	{
		auto end = chrono::high_resolution_clock::now();
		double rv = chrono::duration<double>(end - m_start).count();
		m_start = end;
		return rv;
	}

	ostream &
	operator<<(ostream &os, const Timer &t)
	{
		return os << t.report();
	}

	bool
	Timer::operator<(double d) const
	{
		return age() < d;
	}

	bool
	Timer::operator>(double d) const
	{
		return age() > d;
	}

	RateLimiter::RateLimiter(double rate, double burst) :
		m_rate(rate),
		m_burst(burst)
	{
		THROW_CHECK1(invalid_argument, m_rate, m_rate > 0);
		THROW_CHECK1(invalid_argument, m_burst, m_burst >= 0);
	}

	RateLimiter::RateLimiter(double rate) :
		m_rate(rate),
		m_burst(rate)
	{
		THROW_CHECK1(invalid_argument, m_rate, m_rate > 0);
		THROW_CHECK1(invalid_argument, m_burst, m_burst >= 0);
	}

	void
	RateLimiter::update_tokens()
	{
		double delta = m_timer.lap();
		m_tokens += delta * m_rate;
		if (m_tokens > m_burst) {
			m_tokens = m_burst;
		}
	}

	double
	RateLimiter::sleep_time(double cost)
	{
		THROW_CHECK1(invalid_argument, m_rate, m_rate > 0);
		borrow(cost);
		unique_lock<mutex> lock(m_mutex);
		update_tokens();
		if (m_tokens >= 0) {
			return 0;
		}
		return -m_tokens / m_rate;
	}

	void
	RateLimiter::sleep_for(double cost)
	{
		double time_to_sleep = sleep_time(cost);
		if (time_to_sleep > 0.0) {
			nanosleep(time_to_sleep);
		} else {
			return;
		}
	}

	bool
	RateLimiter::is_ready()
	{
		unique_lock<mutex> lock(m_mutex);
		update_tokens();
		return m_tokens >= 0;
	}

	void
	RateLimiter::borrow(double cost)
	{
		unique_lock<mutex> lock(m_mutex);
		m_tokens -= cost;
	}

	void
	RateLimiter::rate(double const new_rate)
	{
		THROW_CHECK1(invalid_argument, new_rate, new_rate > 0);
		unique_lock<mutex> lock(m_mutex);
		m_rate = new_rate;
	}

	double
	RateLimiter::rate() const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_rate;
	}

	RateEstimator::RateEstimator(double min_delay, double max_delay) :
		m_min_delay(min_delay),
		m_max_delay(max_delay)
	{
		THROW_CHECK1(invalid_argument, min_delay, min_delay > 0);
		THROW_CHECK1(invalid_argument, max_delay, max_delay > 0);
		THROW_CHECK2(invalid_argument, min_delay, max_delay, max_delay > min_delay);
	}

	void
	RateEstimator::update_unlocked(uint64_t new_count)
	{
		// Gradually reduce the effect of previous updates
		if (m_last_decay.age() > 1) {
			m_num *= m_decay;
			m_den *= m_decay;
			m_last_decay.reset();
		}
		// Add units over time to running totals
		auto increment = new_count - min(new_count, m_last_count);
		auto delta = max(0.0, m_last_update.lap());
		m_num += increment;
		m_den += delta;
		m_last_count = new_count;
		// If count increased, wake up any waiters
		if (delta > 0) {
			m_condvar.notify_all();
		}
	}

	void
	RateEstimator::update(uint64_t new_count)
	{
		unique_lock<mutex> lock(m_mutex);
		return update_unlocked(new_count);
	}

	void
	RateEstimator::update_monotonic(uint64_t new_count)
	{
		unique_lock<mutex> lock(m_mutex);
		if (m_last_count == numeric_limits<uint64_t>::max() || new_count > m_last_count) {
			return update_unlocked(new_count);
		} else {
			return update_unlocked(m_last_count);
		}
	}

	void
	RateEstimator::increment(const uint64_t more)
	{
		unique_lock<mutex> lock(m_mutex);
		return update_unlocked(m_last_count + more);
	}

	uint64_t
	RateEstimator::count() const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_last_count;
	}

	pair<double, double>
	RateEstimator::ratio_unlocked() const
	{
		auto num = max(m_num, 1.0);
		// auto den = max(m_den, 1.0);
		// Rate estimation slows down if there are no new units to count
		auto den = max(m_den + m_last_update.age(), 1.0);
		auto sec_per_count = den / num;
		if (sec_per_count < m_min_delay) {
			return make_pair(1.0, m_min_delay);
		}
		if (sec_per_count > m_max_delay) {
			return make_pair(1.0, m_max_delay);
		}
		return make_pair(num, den);
	}

	pair<double, double>
	RateEstimator::ratio() const
	{
		unique_lock<mutex> lock(m_mutex);
		return ratio_unlocked();
	}

	pair<double, double>
	RateEstimator::raw() const
	{
		unique_lock<mutex> lock(m_mutex);
		return make_pair(m_num, m_den);
	}

	double
	RateEstimator::rate_unlocked() const
	{
		auto r = ratio_unlocked();
		return r.first / r.second;
	}

	double
	RateEstimator::rate() const
	{
		unique_lock<mutex> lock(m_mutex);
		return rate_unlocked();
	}

	ostream &
	operator<<(ostream &os, const RateEstimator &re)
	{
		os << "RateEstimator { ";
		auto ratio = re.ratio();
		auto raw = re.raw();
		os << "count = " << re.count() << ", raw = " << raw.first << " / " << raw.second << ", ratio = " << ratio.first << " / " << ratio.second << ", rate = " << re.rate() << ", duration(1) = " << re.duration(1).count() << ", seconds_for(1) = " << re.seconds_for(1) << " }";
		return os;
	}

	chrono::duration<double>
	RateEstimator::duration_unlocked(uint64_t relative_count) const
	{
		auto dur = relative_count / rate_unlocked();
		dur = min(m_max_delay, dur);
		dur = max(m_min_delay, dur);
		return chrono::duration<double>(dur);
	}

	chrono::duration<double>
	RateEstimator::duration(uint64_t relative_count) const
	{
		unique_lock<mutex> lock(m_mutex);
		return duration_unlocked(relative_count);
	}

	chrono::high_resolution_clock::time_point
	RateEstimator::time_point_unlocked(uint64_t absolute_count) const
	{
		auto relative_count = absolute_count - min(m_last_count, absolute_count);
		auto relative_duration = duration_unlocked(relative_count);
		return m_last_update.get() + chrono::duration_cast<chrono::high_resolution_clock::duration>(relative_duration);
		// return chrono::high_resolution_clock::now() + chrono::duration_cast<chrono::high_resolution_clock::duration>(relative_duration);
	}

	chrono::high_resolution_clock::time_point
	RateEstimator::time_point(uint64_t absolute_count) const
	{
		unique_lock<mutex> lock(m_mutex);
		return time_point_unlocked(absolute_count);
	}

	void
	RateEstimator::wait_until(uint64_t new_count_absolute) const
	{
		unique_lock<mutex> lock(m_mutex);
		auto saved_count = m_last_count;
		while (saved_count <= m_last_count && m_last_count < new_count_absolute) {
			// Stop waiting if clock runs backwards
			saved_count = m_last_count;
			m_condvar.wait(lock);
		}
	}

	void
	RateEstimator::wait_for(uint64_t new_count_relative) const
	{
		unique_lock<mutex> lock(m_mutex);
		auto saved_count = m_last_count;
		auto new_count_absolute = m_last_count + new_count_relative;
		while (saved_count <= m_last_count && m_last_count < new_count_absolute) {
			// Stop waiting if clock runs backwards
			saved_count = m_last_count;
			m_condvar.wait(lock);
		}
	}

	double
	RateEstimator::seconds_for(uint64_t new_count_relative) const
	{
		unique_lock<mutex> lock(m_mutex);
		auto ts = time_point_unlocked(new_count_relative + m_last_count);
		auto delta_dur = ts - chrono::high_resolution_clock::now();
		return max(min(chrono::duration<double>(delta_dur).count(), m_max_delay), m_min_delay);
	}

	double
	RateEstimator::seconds_until(uint64_t new_count_absolute) const
	{
		unique_lock<mutex> lock(m_mutex);
		auto ts = time_point_unlocked(new_count_absolute);
		auto delta_dur = ts - chrono::high_resolution_clock::now();
		return max(min(chrono::duration<double>(delta_dur).count(), m_max_delay), m_min_delay);
	}

}
