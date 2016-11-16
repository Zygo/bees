#ifndef CRUCIBLE_TESTS_H
#define CRUCIBLE_TESTS_H

#undef NDEBUG

#include <iostream>

#define RUN_A_TEST(test) do { \
	std::cerr << "Testing " << #test << "..." << std::flush; \
	do { test ; } while (0); \
	std::cerr << "OK" << std::endl; \
} while (0)

#endif // CRUCIBLE_TESTS_H
