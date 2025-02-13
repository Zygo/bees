#include "tests.h"

#include "crucible/seeker.h"

#include <set>
#include <vector>

#include <unistd.h>

using namespace crucible;

static
set<uint64_t>
seeker_finder(const vector<uint64_t> &vec, uint64_t lower, uint64_t upper)
{
	set<uint64_t> s(vec.begin(), vec.end());
	auto lb = s.lower_bound(lower);
	auto ub = lb;
	if (ub != s.end()) ++ub;
	if (ub != s.end()) ++ub;
	for (; ub != s.end(); ++ub) {
		if (*ub > upper) {
			break;
		}
	}
	return set<uint64_t>(lb, ub);
}

static bool test_fails = false;

static
void
seeker_test(const vector<uint64_t> &vec, uint64_t const target, bool const always_out = false)
{
	cerr << "Find " << target << " in {";
	for (auto i : vec) {
		cerr << " " << i;
	}
	cerr << " } = ";
	size_t loops = 0;
	tl_seeker_debug_str = make_shared<ostringstream>();
	bool local_test_fails = false;
	bool excepted = catch_all([&]() {
		const auto found = seek_backward(target, [&](uint64_t lower, uint64_t upper) {
			++loops;
			return seeker_finder(vec, lower, upper);
		}, uint64_t(32));
		cerr << found;
		uint64_t my_found = 0;
		for (auto i : vec) {
			if (i <= target) {
				my_found = i;
			}
		}
		if (found == my_found) {
			cerr << " (correct)";
		} else {
			cerr << " (INCORRECT - right answer is " << my_found << ")";
			local_test_fails = true;
		}
	});
	cerr << " (" << loops << " loops)" << endl;
	if (excepted || local_test_fails || always_out) {
		cerr << dynamic_pointer_cast<ostringstream>(tl_seeker_debug_str)->str();
	}
	test_fails = test_fails || local_test_fails;
	tl_seeker_debug_str.reset();
}

static
void
test_seeker()
{
	seeker_test(vector<uint64_t> { 0, 1, 2, 3, 4, 5 }, 3);
	seeker_test(vector<uint64_t> { 0, 1, 2, 3, 4, 5 }, 5);
	seeker_test(vector<uint64_t> { 0, 1, 2, 3, 4, 5 }, 0);
	seeker_test(vector<uint64_t> { 0, 1, 2, 3, 4, 5 }, 1);
	seeker_test(vector<uint64_t> { 0, 1, 2, 3, 4, 5 }, 4);
	seeker_test(vector<uint64_t> { 0, 1, 2, 3, 4, 5 }, 2);

	seeker_test(vector<uint64_t> { 11, 22, 33, 44, 55 }, 2);
	seeker_test(vector<uint64_t> { 11, 22, 33, 44, 55 }, 25);
	seeker_test(vector<uint64_t> { 11, 22, 33, 44, 55 }, 52);
	seeker_test(vector<uint64_t> { 11, 22, 33, 44, 55 }, 99);
	seeker_test(vector<uint64_t> { 11, 22, 33, 44, 55, 56 }, 99);
	seeker_test(vector<uint64_t> { 11, 22, 33, 44, 55 }, 1);
	seeker_test(vector<uint64_t> { 11, 22, 33, 44, 55 }, 55);
	seeker_test(vector<uint64_t> { 11 }, 55);
	seeker_test(vector<uint64_t> { 11 }, 10);
	seeker_test(vector<uint64_t> { 55 }, 55);
	seeker_test(vector<uint64_t> { }, 55);
	seeker_test(vector<uint64_t> { 55 }, numeric_limits<uint64_t>::max());
	seeker_test(vector<uint64_t> { 55 }, numeric_limits<uint64_t>::max() - 1);
	seeker_test(vector<uint64_t> { }, numeric_limits<uint64_t>::max());
	seeker_test(vector<uint64_t> { 0, numeric_limits<uint64_t>::max() }, numeric_limits<uint64_t>::max());
	seeker_test(vector<uint64_t> { 0, numeric_limits<uint64_t>::max() }, numeric_limits<uint64_t>::max() - 1);
	seeker_test(vector<uint64_t> { 0, numeric_limits<uint64_t>::max() - 1 }, numeric_limits<uint64_t>::max());

	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 0);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 1);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 2);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 3);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 4);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 5);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 6);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 7);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 8);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, 9);
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() );
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() - 1 );
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() - 2 );
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() - 3 );
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() - 4 );
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() - 5 );
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() - 6 );
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() - 7 );
	seeker_test(vector<uint64_t> { 0, 1, 2, 4, 8 }, numeric_limits<uint64_t>::max() - 8 );

	// Pulled from a bees debug log
	seeker_test(vector<uint64_t> {
		6821962845,
		6821962848,
		6821963411,
		6821963422,
		6821963536,
		6821963539,
		6821963835, // <- appeared during the search, causing an exception
		6821963841,
		6822575316,
	}, 6821971036, true);
}


int main(int, const char **)
{

	RUN_A_TEST(test_seeker());

	return test_fails ? EXIT_FAILURE : EXIT_SUCCESS;
}
