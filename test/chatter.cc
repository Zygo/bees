#include "tests.h"

#include "crucible/chatter.h"

#include <ios>
#include <cassert>
#include <cstring>
#include <cstdlib>

#include <unistd.h>

using namespace crucible;

static
void
test_chatter_one()
{
	cerr << endl;
	CHATTER("simple chatter case");
}

static
void
test_chatter_two()
{
	cerr << endl;
	CHATTER("two lines\nof chatter");
}

static
void
test_chatter_three()
{
	cerr << endl;
	Chatter c(0, "tct");
	c << "More complicated";
	c << "\ncase with\n";
	c << "some \\ns";
}

int
main(int, char**)
{
	RUN_A_TEST(test_chatter_one());
	RUN_A_TEST(test_chatter_two());
	RUN_A_TEST(test_chatter_three());

	exit(EXIT_SUCCESS);
}
