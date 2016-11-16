#include "crucible/time.h"

#include "crucible/error.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <thread>

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

	void
	Timer::set(const chrono::high_resolution_clock::time_point &start)
	{
		m_start = start;
	}

	void
	Timer::set(double delta)
	{
		m_start += chrono::duration_cast<chrono::high_resolution_clock::duration>(chrono::duration<double>(delta));
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
	}

	RateLimiter::RateLimiter(double rate) :
		m_rate(rate),
		m_burst(rate)
	{
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

	void
	RateLimiter::sleep_for(double cost)
	{
		borrow(cost);
		while (1) {
			unique_lock<mutex> lock(m_mutex);
			update_tokens();
			if (m_tokens >= 0) {
				return;
			}
			double sleep_time(-m_tokens / m_rate);
			lock.unlock();
			if (sleep_time > 0.0) {
				nanosleep(sleep_time);
			} else {
				return;
			}
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

}
