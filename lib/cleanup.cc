#include <crucible/cleanup.h>

namespace crucible {

	Cleanup::Cleanup(function<void()> func) :
		m_cleaner(func)
	{
	}

	Cleanup::~Cleanup()
	{
		if (m_cleaner) {
			m_cleaner();
		}
	}

}
