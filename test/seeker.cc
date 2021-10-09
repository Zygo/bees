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
		if (*ub > upper) break;
	}
	return set<uint64_t>(lb, ub);
}

static bool test_fails = false;

static
void
seeker_test(const vector<uint64_t> &vec, size_t target)
{
	cerr << "Find " << target << " in {";
	for (auto i : vec) {
		cerr << " " << i;
	}
	cerr << " } = ";
	size_t loops = 0;
	bool excepted = catch_all([&]() {
		auto found = seek_backward(target, [&](uint64_t lower, uint64_t upper) {
			++loops;
			return seeker_finder(vec, lower, upper);
		});
		cerr << found;
		size_t my_found = 0;
		for (auto i : vec) {
			if (i <= target) {
				my_found = i;
			}
		}
		if (found == my_found) {
			cerr << " (correct)";
		} else {
			cerr << " (INCORRECT - right answer is " << my_found << ")";
			test_fails = true;
		}
	});
	cerr << " (" << loops << " loops)" << endl;
	if (excepted) {
		test_fails = true;
	}
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
}


int main(int, const char **)
{

	RUN_A_TEST(test_seeker());

	return test_fails ? EXIT_FAILURE : EXIT_SUCCESS;
}
