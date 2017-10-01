#ifndef CRUCIBLE_CLEANUP_H
#define CRUCIBLE_CLEANUP_H

#include <functional>

namespace crucible {
	using namespace std;

	class Cleanup {
		function<void()> m_cleaner;
	public:
		Cleanup(function<void()> func);
		~Cleanup();
	};

}

#endif // CRUCIBLE_CLEANUP_H
