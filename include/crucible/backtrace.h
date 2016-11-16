#ifndef CRUCIBLE_BACKTRACE_H
#define CRUCIBLE_BACKTRACE_H

#include <string>
#include <vector>

#include <execinfo.h>

namespace crucible {
	using namespace std;

	class Backtrace {
		vector<void *> m_buffer;
		mutable vector<string> m_result_stringvec;
		mutable char **m_result_cpp;
		int m_result_size;
		int m_desired_size;
	public:
		Backtrace(int size = 99);
		~Backtrace();
		const vector<string> &strings() const;
		const vector<void *> &voids() const;
		void symbols_fd(int fd) const;
		bool overflowed() const;
	};

}

#endif // CRUCIBLE_BACKTRACE_H
