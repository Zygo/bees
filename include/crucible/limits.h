#ifndef CRUCIBLE_LIMITS_H
#define CRUCIBLE_LIMITS_H

#include "crucible/error.h"

#include <limits>
#include <typeinfo>

namespace crucible {
	using namespace std;

	template <class To, class From>
	To
	ranged_cast(From f)
	{
		if (typeid(From) == typeid(To)) {
			return f;
		}

		To t;
		static string f_info = typeid(f).name();
		static string t_info = typeid(t).name();

		if (numeric_limits<From>::max() > numeric_limits<To>::max() && numeric_limits<From>::max() < numeric_limits<To>::max()) {
			THROW_ERROR(out_of_range,
				"ranged_cast: can't compare limits of types " << f_info << " and " << t_info << ", template specialization required");
		}

		if (numeric_limits<From>::max() > numeric_limits<To>::max() && f > static_cast<From>(numeric_limits<To>::max())) {
			THROW_ERROR(out_of_range,
				"ranged_cast: " << f_info << "(" << f << ") out of range of target type " << t_info);
		}

		if (!numeric_limits<To>::is_signed && numeric_limits<From>::is_signed && f < 0) {
			THROW_ERROR(out_of_range,
				"ranged_cast: " << f_info << "(" << f << ") out of range of unsigned target type " << t_info);
		}

		t = static_cast<To>(f);

		From f2 = static_cast<From>(t);
		if (f2 != f) {
			THROW_ERROR(out_of_range,
				"ranged_cast: " << f_info << "(" << f << ") -> " << t_info << " failed: result value " << f2);
		}

		return t;
	}
};

#endif // CRUCIBLE_LIMITS_H
