#include "tests.h"

#include "crucible/execpipe.h"

#include <ios>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

#include <unistd.h>

using namespace crucible;
using namespace std;

#if 1 // Needs rework
static inline
void
test_hello_world()
{
	// alarm(9);
	Fd fd = popen([]() { return system("echo Hello, World!"); });
	char buf[1024];
	size_t rv = -1;
	read_partial_or_die(fd, buf, rv);
	assert(rv > 0);
	string b(buf, buf + rv - 1);
	// cerr << "hello_world says: '" << b << "'" << endl;
	assert(b == "Hello, World!");
}

static inline
void
test_read_limit(size_t limit = 4096)
{
	alarm(9);
	Fd fd = popen([]() { return system("yes Hello!"); });
	try {
		string b = read_all(fd, limit);
	} catch (out_of_range &re) {
		return;
	}
	assert(!"no exception thrown by read_all");
}
#endif

namespace crucible {
	extern bool assert_no_leaked_fds();
};

int
main(int, char**)
{
#if 1
	RUN_A_TEST(test_hello_world());
	assert(assert_no_leaked_fds());
	RUN_A_TEST(test_read_limit(4095));
	RUN_A_TEST(test_read_limit(4096));
	RUN_A_TEST(test_read_limit(4097));
	assert(assert_no_leaked_fds());
#endif

	exit(EXIT_SUCCESS);
}
