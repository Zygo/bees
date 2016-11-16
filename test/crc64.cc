#include "tests.h"
#include "crucible/crc64.h"

#include <cassert>

using namespace crucible;

static
void
test_getcrc64_strings()
{
	assert(Digest::CRC::crc64("John") == 5942451273432301568);
	assert(Digest::CRC::crc64("Paul") == 5838402100630913024);
	assert(Digest::CRC::crc64("George") == 6714394476893704192);
	assert(Digest::CRC::crc64("Ringo") == 6038837226071130112);
	assert(Digest::CRC::crc64("") == 0);
	assert(Digest::CRC::crc64("\377\277\300\200") == 15615382887346470912ULL);
}

static
void
test_getcrc64_byte_arrays()
{
	assert(Digest::CRC::crc64("John", 4) == 5942451273432301568);
	assert(Digest::CRC::crc64("Paul", 4) == 5838402100630913024);
	assert(Digest::CRC::crc64("George", 6) == 6714394476893704192);
	assert(Digest::CRC::crc64("Ringo", 5) == 6038837226071130112);
	assert(Digest::CRC::crc64("", 0) == 0);
	assert(Digest::CRC::crc64("\377\277\300\200", 4) == 15615382887346470912ULL);
}

int
main(int, char**)
{
	RUN_A_TEST(test_getcrc64_strings());
	RUN_A_TEST(test_getcrc64_byte_arrays());

	exit(EXIT_SUCCESS);
}
