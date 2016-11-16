#ifndef CRUCIBLE_BOOL_H
#define CRUCIBLE_BOOL_H

namespace crucible {
	struct DefaultBool {
		bool m_b;
		DefaultBool(bool init = false) : m_b(init) {}
		operator bool() const { return m_b; }
		bool &operator=(const bool &that) { return m_b = that; }
	};
}

#endif // CRUCIBLE_BOOL_H
