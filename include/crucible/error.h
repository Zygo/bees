#ifndef CRUCIBLE_ERROR_H
#define CRUCIBLE_ERROR_H

// Common error-handling idioms for C library calls

#include <cerrno>
#include <cstring>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <unistd.h>

namespace crucible {
        using namespace std;

	// Common error-handling idioms for C library calls

        template <class T> T die_if_minus_errno(const char *expr, T rv)
	{
		if (rv < 0) {
			throw system_error(error_code(-rv, system_category()), expr);
		}
		return rv;
	}

        template <class T> T die_if_minus_one(const char *expr, T rv)
	{
		if (rv == -1) {
			throw system_error(error_code(errno, system_category()), expr);
		}
		return rv;
	}

        template <class T> T die_if_zero(const char *expr, T rv)
	{
		if (rv == 0) {
			throw system_error(error_code(errno, system_category()), expr);
		}
		return rv;
	}

        template <class T> T die_if_non_zero(const char *expr, T rv)
	{
		if (rv != 0) {
			throw system_error(error_code(errno, system_category()), expr);
		}
		return rv;
	}

	// Usage:  catch_all([&]() { /* insert body here */ } );
	// Executes body with exceptions caught and reported to cerr.
	// Returns:
	//     0 if f() returns
	//     non-zero if f() throws an exception
	//	-1 for unknown exception
	//	1 for std::exception or class derived thereof

	void set_catch_explainer(function<void(string s)> f);
	void default_catch_explainer(string s);
	int catch_all(const function<void()> &f, const function<void(string)> &explainer = default_catch_explainer);

	// catch_and_explain traps the exception, calls the explainer, then rethrows the original exception
	void catch_and_explain(const function<void()> &f, const function<void(string)> &explainer = default_catch_explainer);
};

// 0 on success, -errno on error.
// Covers most pthread functions.
#define DIE_IF_MINUS_ERRNO(expr) crucible::die_if_minus_errno(#expr, expr)

// -1 on error, all other values mean success.
#define DIE_IF_MINUS_ONE(expr)   crucible::die_if_minus_one(#expr, expr)

// 0 (or NULL) on error, all other values mean success.
#define DIE_IF_ZERO(expr)        crucible::die_if_zero(#expr, expr)

// 0 (or NULL) on success, all other values mean error.
#define DIE_IF_NON_ZERO(expr)    crucible::die_if_non_zero(#expr, expr)

// macro for throwing an error
#define THROW_ERROR(type, expr) do { \
	std::ostringstream _te_oss; \
	_te_oss << expr << " at " << __FILE__ << ":" << __LINE__; \
	throw type(_te_oss.str()); \
} while (0)

// macro for throwing a system_error with errno
#define THROW_ERRNO(expr) do { \
	std::ostringstream _te_oss; \
	_te_oss << expr << " at " << __FILE__ << ":" << __LINE__; \
	throw std::system_error(std::error_code(errno, std::system_category()), _te_oss.str()); \
} while (0)

// macro for throwing a system_error with some other variable
#define THROW_ERRNO_VALUE(value, expr) do { \
	std::ostringstream _te_oss; \
	_te_oss << expr << " at " << __FILE__ << ":" << __LINE__; \
	throw std::system_error(std::error_code((value), std::system_category()), _te_oss.str()); \
} while (0)

// macros for checking a constraint
#define THROW_CHECK0(type, expr) do { \
	if (!(expr)) { \
		THROW_ERROR(type, "failed constraint check (" << #expr << ")"); \
	} \
} while(0)

#define THROW_CHECK1(type, value, expr) do { \
	if (!(expr)) { \
		THROW_ERROR(type, #value << " = " << (value) << " failed constraint check (" << #expr << ")"); \
	} \
} while(0)

#define THROW_CHECK2(type, value1, value2, expr) do { \
	if (!(expr)) { \
		THROW_ERROR(type, #value1 << " = " << (value1) << ", " #value2 << " = " << (value2) \
			<< " failed constraint check (" << #expr << ")"); \
	} \
} while(0)

#define THROW_CHECK3(type, value1, value2, value3, expr) do { \
	if (!(expr)) { \
		THROW_ERROR(type, #value1 << " = " << (value1) << ", " #value2 << " = " << (value2) << ", " #value3 << " = " << (value3) \
			<< " failed constraint check (" << #expr << ")"); \
	} \
} while(0)

#define THROW_CHECK_BIN_OP(type, value1, op, value2) do { \
	if (!((value1) op (value2))) { \
		THROW_ERROR(type, "failed constraint check " << #value1 << " (" << (value1) << ") " << #op << " " << #value2 << " (" << (value2) << ")"); \
	} \
} while(0)

#define THROW_CHECK_PREFIX_OP(type, op, value1) do { \
	if (!(op (value1))) { \
		THROW_ERROR(type, "failed constraint check " << #op << " " << #value1 << " (" << (value1) << ")"); \
	} \
} while(0)

#define THROW_CHECK_RANGE(type, value_min, value_test, value_max) do { \
	if ((value_test) < (value_min) || (value_max) < (value_test)) { \
		THROW_ERROR(type, "failed constraint check " << #value_min << " (" << (value_min) << ") <= " #value_test << " (" << (value_test) \
			<< ") <= " << #value_max << " (" << (value_max) << ")"); \
	} \
} while(0)

#define THROW_CHECK_ARRAY_RANGE(type, value_min, value_test, value_max) do { \
	if ((value_test) < (value_min) || !((value_test) < (value_max))) { \
		THROW_ERROR(type, "failed constraint check " << #value_min << " (" << (value_min) << ") <= " #value_test << " (" << (value_test) \
			<< ") < " << #value_max << " (" << (value_max) << ")"); \
	} \
} while(0)

#endif // CRUCIBLE_ERROR_H
