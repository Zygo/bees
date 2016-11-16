#include "tests.h"

#include "crucible/process.h"

#include <ios>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

#include <unistd.h>

using namespace crucible;
using namespace std;

static inline
int
return_value(int val)
{
	// cerr << "pid " << getpid() << " returning " << val << endl;
	return val;
}

static inline
int
return_value_2(int val, int val2)
{
	return val + val2;
}

static inline
void
test_fork_return(int val)
{
	Pid child(return_value, val);
	assert(child == child->get_id());
	assert(child == child->native_handle());
	int status = child->join();
	int rv_status = WEXITSTATUS(status);
	assert(WIFEXITED(status));
	assert(rv_status == val);
}

static inline
void
test_fork_return(int val, int val2)
{
	Pid child(return_value_2, val, val2);
	int status = child->join();
	int rv_status = WEXITSTATUS(status);
	assert(WIFEXITED(status));
	assert(rv_status == val + val2);
}

int
main(int, char**)
{
	RUN_A_TEST(test_fork_return(0));
	RUN_A_TEST(test_fork_return(1));
	RUN_A_TEST(test_fork_return(9));
	RUN_A_TEST(test_fork_return(2, 3));
	RUN_A_TEST(test_fork_return(7, 9));

	exit(EXIT_SUCCESS);
}
