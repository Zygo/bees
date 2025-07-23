#ifndef CRUCIBLE_TESTS_H
#define CRUCIBLE_TESTS_H

#undef NDEBUG

#include <exception>
#include <functional>
#include <iostream>

#define RUN_A_TEST(test) do { \
	std::cerr << "Testing " << #test << "..." << std::flush; \
	do { test ; } while (0); \
	std::cerr << "OK" << std::endl; \
} while (0)

#define SHOULD_THROW(expr) assert(1 == silent_catch_all([&]() { (expr); }))

// Like catch_all but don't bother printing anything
static
int
__attribute__((unused))
silent_catch_all(const std::function<void()> &f)
{
	try {
		f();
		return 0;
	} catch (const std::exception &) {
		return 1;
	} catch (...) {
		return -1;
	}
}

#define ASSERT_EQUAL(a, b) do { \
	const string a_rv = (a); \
	const string b_rv = (b); \
	cerr << "a: '" << a_rv << "' (" << #a << ")\nb: '" << b_rv << "' (" << #b << ")\n"; \
	assert(a_rv == b_rv); \
} while (false)

#endif // CRUCIBLE_TESTS_H
