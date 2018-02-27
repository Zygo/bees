#include "tests.h"

#include "crucible/progress.h"

#include <cassert>

#include <unistd.h>

using namespace crucible;
using namespace std;

void
test_progress()
{
	ProgressTracker<uint64_t> pt(123);
	auto hold = pt.hold(234);
	auto hold2 = pt.hold(345);
	assert(pt.begin() == 123);
	assert(pt.end() == 345);
	auto hold3 = pt.hold(456);
	assert(pt.begin() == 123);
	assert(pt.end() == 456);
	hold2.reset();
	assert(pt.begin() == 123);
	assert(pt.end() == 456);
	hold.reset();
	assert(pt.begin() == 345);
	assert(pt.end() == 456);
	hold3.reset();
	assert(pt.begin() == 456);
	assert(pt.end() == 456);
}

int
main(int, char**)
{
	RUN_A_TEST(test_progress());

	exit(EXIT_SUCCESS);
}
