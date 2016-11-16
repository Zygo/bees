#include "tests.h"

#include "crucible/path.h"

#include <ios>
#include <cassert>
#include <cstring>
#include <cstdlib>

#include <unistd.h>

using namespace crucible;

unsigned failures = 0;
static
void
test_path_basename(string input, string expected)
{
	string result = basename(input);
	if (expected != result) {
		std::cerr << "result was \"" << result << "\"" << std::endl;
		++failures;
	}
}

int
main(int, char**)
{
	RUN_A_TEST(test_path_basename("/foo/bar.c", "bar.c"));
	RUN_A_TEST(test_path_basename("/foo/bar/", ""));
	RUN_A_TEST(test_path_basename("/foo/", ""));
	RUN_A_TEST(test_path_basename("/", ""));
	RUN_A_TEST(test_path_basename("foo/bar.c", "bar.c"));
	RUN_A_TEST(test_path_basename("bar.c", "bar.c"));
	RUN_A_TEST(test_path_basename("", ""));

	assert(!failures);

	exit(EXIT_SUCCESS);
}
