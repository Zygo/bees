#include "tests.h"

#include "crucible/progress.h"

#include <cassert>

#include <unistd.h>

using namespace crucible;
using namespace std;

void
test_progress()
{
	// On create, begin == end == constructor argument
	ProgressTracker<uint64_t> pt(123);
	assert(pt.begin() == 123);
	assert(pt.end() == 123);

	// Holding a position past the end increases the end (and moves begin to match)
	auto hold345 = pt.hold(345);
	assert(pt.begin() == 345);
	assert(pt.end() == 345);

	// Holding a position before begin reduces begin, without changing end
	auto hold234 = pt.hold(234);
	assert(pt.begin() == 234);
	assert(pt.end() == 345);

	// Holding a position past the end increases the end, without affecting begin
	auto hold456 = pt.hold(456);
	assert(pt.begin() == 234);
	assert(pt.end() == 456);

	// Releasing a position in the middle affects neither begin nor end
	hold345.reset();
	assert(pt.begin() == 234);
	assert(pt.end() == 456);

	// Hold another position in the middle to test begin moving forward
	auto hold400 = pt.hold(400);

	// Releasing a position at the beginning moves begin forward
	hold234.reset();
	assert(pt.begin() == 400);
	assert(pt.end() == 456);

	// Releasing a position at the end doesn't move end backward
	hold456.reset();
	assert(pt.begin() == 400);
	assert(pt.end() == 456);

	// Releasing a position in the middle doesn't move end backward but does move begin forward
	hold400.reset();
	assert(pt.begin() == 456);
	assert(pt.end() == 456);

}

int
main(int, char**)
{
	RUN_A_TEST(test_progress());

	exit(EXIT_SUCCESS);
}
