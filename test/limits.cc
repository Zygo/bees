#include "tests.h"
#include "crucible/error.h"
#include "crucible/limits.h"

#include <cassert>

using namespace crucible;

// Like catch_all but don't bother printing anything
static
int
silent_catch_all(const function<void()> &f)
{
	try {
		f();
		return 0;
	} catch (const exception &) {
		return 1;
	} catch (...) {
		return -1;
	}
}


#define SHOULD_FAIL(expr) assert(1 == silent_catch_all([&]() { (expr); }))

#define SHOULD_PASS(expr, result) assert(0 == silent_catch_all([&]() { assert((result) == (expr)); }))

static
void
test_cast_signed_negative_to_unsigned()
{
	off_t tv = -1;
	SHOULD_FAIL(ranged_cast<uint64_t>(tv));
	SHOULD_FAIL(ranged_cast<uint32_t>(tv));
	SHOULD_FAIL(ranged_cast<uint16_t>(tv));
	SHOULD_FAIL(ranged_cast<uint8_t>(tv));
	SHOULD_FAIL(ranged_cast<unsigned long long>(tv));
	SHOULD_FAIL(ranged_cast<unsigned long>(tv));
	SHOULD_FAIL(ranged_cast<unsigned int>(tv));
	SHOULD_FAIL(ranged_cast<unsigned short>(tv));
	SHOULD_FAIL(ranged_cast<unsigned char>(tv));
}

static
void
test_cast_1_to_things()
{
	auto tv = 1;
	SHOULD_PASS(ranged_cast<off_t>(tv), 1);
	SHOULD_PASS(ranged_cast<uint64_t>(tv), 1);
	SHOULD_PASS(ranged_cast<uint32_t>(tv), 1);
	SHOULD_PASS(ranged_cast<uint16_t>(tv), 1);
	SHOULD_PASS(ranged_cast<uint8_t>(tv), 1);
	SHOULD_PASS(ranged_cast<int64_t>(tv), 1);
	SHOULD_PASS(ranged_cast<int32_t>(tv), 1);
	SHOULD_PASS(ranged_cast<int16_t>(tv), 1);
	SHOULD_PASS(ranged_cast<int8_t>(tv), 1);
	SHOULD_PASS(ranged_cast<unsigned long long>(tv), 1);
	SHOULD_PASS(ranged_cast<unsigned long>(tv), 1);
	SHOULD_PASS(ranged_cast<unsigned int>(tv), 1);
	SHOULD_PASS(ranged_cast<unsigned short>(tv), 1);
	SHOULD_PASS(ranged_cast<unsigned char>(tv), 1);
	SHOULD_PASS(ranged_cast<signed long long>(tv), 1);
	SHOULD_PASS(ranged_cast<signed long>(tv), 1);
	SHOULD_PASS(ranged_cast<signed int>(tv), 1);
	SHOULD_PASS(ranged_cast<signed short>(tv), 1);
	SHOULD_PASS(ranged_cast<signed char>(tv), 1);
}

static
void
test_cast_128_to_things()
{
	auto tv = 128;
	SHOULD_PASS(ranged_cast<off_t>(tv), 128);
	SHOULD_PASS(ranged_cast<uint64_t>(tv), 128);
	SHOULD_PASS(ranged_cast<uint32_t>(tv), 128);
	SHOULD_PASS(ranged_cast<uint16_t>(tv), 128);
	SHOULD_PASS(ranged_cast<uint8_t>(tv), 128);
	SHOULD_PASS(ranged_cast<int64_t>(tv), 128);
	SHOULD_PASS(ranged_cast<int32_t>(tv), 128);
	SHOULD_PASS(ranged_cast<int16_t>(tv), 128);
	SHOULD_FAIL(ranged_cast<int8_t>(tv));
	SHOULD_PASS(ranged_cast<unsigned long long>(tv), 128);
	SHOULD_PASS(ranged_cast<unsigned long>(tv), 128);
	SHOULD_PASS(ranged_cast<unsigned int>(tv), 128);
	SHOULD_PASS(ranged_cast<unsigned short>(tv), 128);
	SHOULD_PASS(ranged_cast<unsigned char>(tv), 128);
	SHOULD_PASS(ranged_cast<signed long long>(tv), 128);
	SHOULD_PASS(ranged_cast<signed long>(tv), 128);
	SHOULD_PASS(ranged_cast<signed int>(tv), 128);
	SHOULD_PASS(ranged_cast<signed short>(tv), 128);
	SHOULD_FAIL(ranged_cast<signed char>(tv));
}

static
void
test_cast_256_to_things()
{
	auto tv = 256;
	SHOULD_PASS(ranged_cast<off_t>(tv), 256);
	SHOULD_PASS(ranged_cast<uint64_t>(tv), 256);
	SHOULD_PASS(ranged_cast<uint32_t>(tv), 256);
	SHOULD_PASS(ranged_cast<uint16_t>(tv), 256);
	SHOULD_FAIL(ranged_cast<uint8_t>(tv));
	SHOULD_PASS(ranged_cast<int64_t>(tv), 256);
	SHOULD_PASS(ranged_cast<int32_t>(tv), 256);
	SHOULD_PASS(ranged_cast<int16_t>(tv), 256);
	SHOULD_FAIL(ranged_cast<int8_t>(tv));
	SHOULD_PASS(ranged_cast<unsigned long long>(tv), 256);
	SHOULD_PASS(ranged_cast<unsigned long>(tv), 256);
	SHOULD_PASS(ranged_cast<unsigned int>(tv), 256);
	SHOULD_PASS(ranged_cast<unsigned short>(tv), 256);
	SHOULD_FAIL(ranged_cast<unsigned char>(tv));
	SHOULD_PASS(ranged_cast<signed long long>(tv), 256);
	SHOULD_PASS(ranged_cast<signed long>(tv), 256);
	SHOULD_PASS(ranged_cast<signed int>(tv), 256);
	SHOULD_PASS(ranged_cast<signed short>(tv), 256);
	SHOULD_FAIL(ranged_cast<signed char>(tv));
}

static
void
test_cast_0x80000000_to_things()
{
	auto sv = 0x80000000LL;
	auto uv = 0x80000000ULL;
	SHOULD_PASS(ranged_cast<off_t>(sv), sv);
	SHOULD_PASS(ranged_cast<uint64_t>(uv), uv);
	SHOULD_PASS(ranged_cast<uint32_t>(uv), uv);
	SHOULD_FAIL(ranged_cast<uint16_t>(uv));
	SHOULD_FAIL(ranged_cast<uint8_t>(uv));
	SHOULD_PASS(ranged_cast<int64_t>(sv), sv);
	SHOULD_FAIL(ranged_cast<int32_t>(sv));
	SHOULD_FAIL(ranged_cast<int16_t>(sv));
	SHOULD_FAIL(ranged_cast<int8_t>(sv));
	SHOULD_PASS(ranged_cast<unsigned long long>(uv), uv);
	SHOULD_PASS(ranged_cast<unsigned long>(uv), uv);
	SHOULD_PASS(ranged_cast<unsigned int>(uv), uv);
	SHOULD_FAIL(ranged_cast<unsigned short>(uv));
	SHOULD_FAIL(ranged_cast<unsigned char>(uv));
	SHOULD_PASS(ranged_cast<signed long long>(sv), sv);
	SHOULD_PASS(ranged_cast<signed long>(sv), sv);
	SHOULD_FAIL(ranged_cast<signed short>(sv));
	SHOULD_FAIL(ranged_cast<signed char>(sv));
	if (sizeof(int) == 4) {
		SHOULD_FAIL(ranged_cast<signed int>(sv));
	} else if (sizeof(int) == 8) {
		SHOULD_PASS(ranged_cast<signed int>(sv), sv);
	} else {
		assert(!"unhandled case, please add code here");
	}
}

static
void
test_cast_0xffffffff_to_things()
{
	auto sv = 0xffffffffLL;
	auto uv = 0xffffffffULL;
	SHOULD_PASS(ranged_cast<off_t>(sv), sv);
	SHOULD_PASS(ranged_cast<uint64_t>(uv), uv);
	SHOULD_PASS(ranged_cast<uint32_t>(uv), uv);
	SHOULD_FAIL(ranged_cast<uint16_t>(uv));
	SHOULD_FAIL(ranged_cast<uint8_t>(uv));
	SHOULD_PASS(ranged_cast<int64_t>(sv), sv);
	SHOULD_FAIL(ranged_cast<int32_t>(sv));
	SHOULD_FAIL(ranged_cast<int16_t>(sv));
	SHOULD_FAIL(ranged_cast<int8_t>(sv));
	SHOULD_PASS(ranged_cast<unsigned long long>(uv), uv);
	SHOULD_PASS(ranged_cast<unsigned long>(uv), uv);
	SHOULD_PASS(ranged_cast<unsigned int>(uv), uv);
	SHOULD_FAIL(ranged_cast<unsigned short>(uv));
	SHOULD_FAIL(ranged_cast<unsigned char>(uv));
	SHOULD_PASS(ranged_cast<signed long long>(sv), sv);
	SHOULD_PASS(ranged_cast<signed long>(sv), sv);
	SHOULD_FAIL(ranged_cast<signed short>(sv));
	SHOULD_FAIL(ranged_cast<signed char>(sv));
	if (sizeof(int) == 4) {
		SHOULD_FAIL(ranged_cast<signed int>(sv));
	} else if (sizeof(int) == 8) {
		SHOULD_PASS(ranged_cast<signed int>(sv), sv);
	} else {
		assert(!"unhandled case, please add code here");
	}
}

static
void
test_cast_0xfffffffff_to_things()
{
	auto sv = 0xfffffffffLL;
	auto uv = 0xfffffffffULL;
	SHOULD_PASS(ranged_cast<off_t>(sv), sv);
	SHOULD_PASS(ranged_cast<uint64_t>(uv), uv);
	SHOULD_FAIL(ranged_cast<uint32_t>(uv));
	SHOULD_FAIL(ranged_cast<uint16_t>(uv));
	SHOULD_FAIL(ranged_cast<uint8_t>(uv));
	SHOULD_PASS(ranged_cast<int64_t>(sv), sv);
	SHOULD_FAIL(ranged_cast<int32_t>(sv));
	SHOULD_FAIL(ranged_cast<int16_t>(sv));
	SHOULD_FAIL(ranged_cast<int8_t>(sv));
	SHOULD_PASS(ranged_cast<unsigned long long>(uv), uv);
	SHOULD_FAIL(ranged_cast<unsigned short>(uv));
	SHOULD_FAIL(ranged_cast<unsigned char>(uv));
	SHOULD_PASS(ranged_cast<signed long long>(sv), sv);
	SHOULD_FAIL(ranged_cast<signed short>(sv));
	SHOULD_FAIL(ranged_cast<signed char>(sv));
	if (sizeof(int) == 4) {
		SHOULD_FAIL(ranged_cast<signed int>(sv));
		SHOULD_FAIL(ranged_cast<unsigned int>(uv));
	} else if (sizeof(int) == 8) {
		SHOULD_PASS(ranged_cast<signed int>(sv), sv);
		SHOULD_PASS(ranged_cast<unsigned int>(uv), uv);
	} else {
		assert(!"unhandled case, please add code here");
	}
	if (sizeof(long) == 4) {
		SHOULD_FAIL(ranged_cast<signed long>(sv));
		SHOULD_FAIL(ranged_cast<unsigned long>(uv));
	} else if (sizeof(long) == 8) {
		SHOULD_PASS(ranged_cast<signed long>(sv), sv);
		SHOULD_PASS(ranged_cast<unsigned long>(uv), uv);
	} else {
		assert(!"unhandled case, please add code here");
	}
}

static
void
test_cast_0x8000000000000000_to_things()
{
	auto sv = 0x8000000000000000LL;
	auto uv = 0x8000000000000000ULL;
	SHOULD_FAIL(ranged_cast<off_t>(sv));
	SHOULD_PASS(ranged_cast<uint64_t>(uv), uv);
	SHOULD_FAIL(ranged_cast<uint32_t>(uv));
	SHOULD_FAIL(ranged_cast<uint16_t>(uv));
	SHOULD_FAIL(ranged_cast<uint8_t>(uv));
	SHOULD_FAIL(ranged_cast<int64_t>(sv));
	SHOULD_FAIL(ranged_cast<int32_t>(sv));
	SHOULD_FAIL(ranged_cast<int16_t>(sv));
	SHOULD_FAIL(ranged_cast<int8_t>(sv));
	SHOULD_PASS(ranged_cast<unsigned long long>(uv), uv);
	SHOULD_FAIL(ranged_cast<unsigned short>(uv));
	SHOULD_FAIL(ranged_cast<unsigned char>(uv));
	SHOULD_FAIL(ranged_cast<signed long long>(sv));
	SHOULD_FAIL(ranged_cast<signed long>(sv));
	SHOULD_FAIL(ranged_cast<signed int>(sv));
	SHOULD_FAIL(ranged_cast<signed short>(sv));
	SHOULD_FAIL(ranged_cast<signed char>(sv));
	if (sizeof(int) == 4) {
		SHOULD_FAIL(ranged_cast<unsigned int>(uv));
	} else if (sizeof(int) == 8) {
		SHOULD_PASS(ranged_cast<unsigned int>(uv), uv);
	} else {
		assert(!"unhandled case, please add code here");
	}
	if (sizeof(long) == 4) {
		SHOULD_FAIL(ranged_cast<unsigned long>(uv));
	} else if (sizeof(long) == 8) {
		SHOULD_PASS(ranged_cast<unsigned long>(uv), uv);
	} else {
		assert(!"unhandled case, please add code here");
	}
}

static
void
test_cast_0xffffffffffffffff_to_things()
{
	auto sv = 0xffffffffffffffffLL;
	auto uv = 0xffffffffffffffffULL;
	SHOULD_FAIL(ranged_cast<off_t>(sv));
	SHOULD_PASS(ranged_cast<uint64_t>(uv), uv);
	SHOULD_FAIL(ranged_cast<uint32_t>(uv));
	SHOULD_FAIL(ranged_cast<uint16_t>(uv));
	SHOULD_FAIL(ranged_cast<uint8_t>(uv));
	SHOULD_FAIL(ranged_cast<int64_t>(sv));
	SHOULD_FAIL(ranged_cast<int32_t>(sv));
	SHOULD_FAIL(ranged_cast<int16_t>(sv));
	SHOULD_FAIL(ranged_cast<int8_t>(sv));
	SHOULD_PASS(ranged_cast<unsigned long long>(uv), uv);
	SHOULD_FAIL(ranged_cast<unsigned short>(uv));
	SHOULD_FAIL(ranged_cast<unsigned char>(uv));
	SHOULD_FAIL(ranged_cast<signed long long>(sv));
	SHOULD_FAIL(ranged_cast<signed long>(sv));
	SHOULD_FAIL(ranged_cast<signed int>(sv));
	SHOULD_FAIL(ranged_cast<signed short>(sv));
	SHOULD_FAIL(ranged_cast<signed char>(sv));
	if (sizeof(int) == 4) {
		SHOULD_FAIL(ranged_cast<unsigned int>(uv));
	} else if (sizeof(int) == 8) {
		SHOULD_PASS(ranged_cast<unsigned int>(uv), uv);
	} else {
		assert(!"unhandled case, please add code here");
	}
	if (sizeof(long) == 4) {
		SHOULD_FAIL(ranged_cast<unsigned long>(uv));
	} else if (sizeof(long) == 8) {
		SHOULD_PASS(ranged_cast<unsigned long>(uv), uv);
	} else {
		assert(!"unhandled case, please add code here");
	}
}

// OK enough with the small values.  We want to know if 32-bit machines break.

int
main(int, char**)
{
	RUN_A_TEST(test_cast_signed_negative_to_unsigned());
	RUN_A_TEST(test_cast_1_to_things());
	RUN_A_TEST(test_cast_128_to_things());
	RUN_A_TEST(test_cast_256_to_things());
	RUN_A_TEST(test_cast_0x80000000_to_things());
	RUN_A_TEST(test_cast_0xffffffff_to_things());
	RUN_A_TEST(test_cast_0xfffffffff_to_things());
	RUN_A_TEST(test_cast_0x8000000000000000_to_things());
	RUN_A_TEST(test_cast_0xffffffffffffffff_to_things());

	exit(EXIT_SUCCESS);
}

