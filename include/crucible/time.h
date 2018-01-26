#ifndef CRUCIBLE_TIME_H
#define CRUCIBLE_TIME_H

#include "crucible/error.h"

#include <chrono>
#include <mutex>
#include <ostream>

namespace crucible {

	double nanosleep(double secs);

	class Timer {
		chrono::high_resolution_clock::time_point m_start;

	public:
		Timer();
		double age() const;
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
		mutex	m_mutex;

		void update_tokens();
		RateLimiter() = delete;
	public:
		RateLimiter(double rate, double burst);
		RateLimiter(double rate);
		void sleep_for(double cost = 1.0);
		bool is_ready();
		void borrow(double cost = 1.0);
	};

}

#endif // CRUCIBLE_TIME_H
