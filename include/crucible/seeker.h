#ifndef _CRUCIBLE_SEEKER_H_
#define _CRUCIBLE_SEEKER_H_

#include "crucible/error.h"

#include <algorithm>
#include <limits>

#include <cstdint>

#if 0
#include <iostream>
#include <sstream>
#define DINIT(__x) __x
#define DLOG(__x) do { logs << __x << std::endl; } while (false)
#define DOUT(__err) do { __err << logs.str(); } while (false)
#else
#define DINIT(__x) do {} while (false)
#define DLOG(__x) do {} while (false)
#define DOUT(__x) do {} while (false)
#endif

namespace crucible {
	using namespace std;

	// Requirements for Container<Pos> Fetch(Pos lower, Pos upper):
	// - fetches objects in Pos order, starting from lower (must be >= lower)
	// - must return upper if present, may or may not return objects after that
	// - returns a container of Pos objects with begin(), end(), rbegin(), rend()
	// - container must iterate over objects in Pos order
	// - uniqueness of Pos objects not required
	// - should store the underlying data as a side effect
	//
	// Requirements for Pos:
	// - should behave like an unsigned integer type
	// - must have specializations in numeric_limits<T> for digits, max(), min()
	// - must support +, -, -=, and related operators
	// - must support <, <=, ==, and related operators
	// - must support Pos / 2 (only)
	//
	// Requirements for seek_backward:
	// - calls Fetch to search Pos space near target_pos
	// - if no key exists with value <= target_pos, returns the minimum Pos value
	// - returns the highest key value <= target_pos
	// - returned key value may not be part of most recent Fetch result
	// - 1 loop iteration when target_pos exists

	template <class Fetch, class Pos = uint64_t>
	Pos
	seek_backward(Pos const target_pos, Fetch fetch, Pos min_step = 1, size_t max_loops = numeric_limits<size_t>::max())
	{
		DINIT(ostringstream logs);
		try {
			static const Pos end_pos = numeric_limits<Pos>::max();
			// TBH this probably won't work if begin_pos != 0, i.e. any signed type
			static const Pos begin_pos = numeric_limits<Pos>::min();
			// Run a binary search looking for the highest key below target_pos.
			// Initial upper bound of the search is target_pos.
			// Find initial lower bound by doubling the size of the range until a key below target_pos
			// is found, or the lower bound reaches the beginning of the search space.
			// If the lower bound search reaches the beginning of the search space without finding a key,
			// return the beginning of the search space; otherwise, perform a binary search between
			// the bounds now established.
			Pos lower_bound = 0;
			Pos upper_bound = target_pos;
			bool found_low = false;
			Pos probe_pos = target_pos;
			// We need one loop for each bit of the search space to find the lower bound,
			// one loop for each bit of the search space to find the upper bound,
			// and one extra loop to confirm the boundary is correct.
			for (size_t loop_count = min(numeric_limits<Pos>::digits * size_t(2) + 1, max_loops); loop_count; --loop_count) {
				DLOG("fetch(probe_pos = " << probe_pos << ", target_pos = " << target_pos << ")");
				auto result = fetch(probe_pos, target_pos);
				const Pos low_pos = result.empty() ? end_pos : *result.begin();
				const Pos high_pos = result.empty() ? end_pos : *result.rbegin();
				DLOG(" = " << low_pos << ".." << high_pos);
				// check for correct behavior of the fetch function
				THROW_CHECK2(out_of_range, high_pos, probe_pos, probe_pos <= high_pos);
				THROW_CHECK2(out_of_range, low_pos, probe_pos, probe_pos <= low_pos);
				THROW_CHECK2(out_of_range, low_pos, high_pos, low_pos <= high_pos);
				if (!found_low) {
					// if target_pos == end_pos then we will find it in every empty result set,
					// so in that case we force the lower bound to be lower than end_pos
					if ((target_pos == end_pos) ? (low_pos < target_pos) : (low_pos <= target_pos)) {
						// found a lower bound, set the low bound there and switch to binary search
						found_low = true;
						lower_bound = low_pos;
						DLOG("found_low = true, lower_bound = " << lower_bound);
					} else {
						// still looking for lower bound
						// if probe_pos was begin_pos then we can stop with no result
						if (probe_pos == begin_pos) {
							DLOG("return: probe_pos == begin_pos " << begin_pos);
							return begin_pos;
						}
						// double the range size, or use the distance between objects found so far
						THROW_CHECK2(out_of_range, upper_bound, probe_pos, probe_pos <= upper_bound);
						// already checked low_pos <= high_pos above
						const Pos want_delta = max(upper_bound - probe_pos, min_step);
						// avoid underflowing the beginning of the search space
						const Pos have_delta = min(want_delta, probe_pos - begin_pos);
						THROW_CHECK2(out_of_range, want_delta, have_delta, have_delta <= want_delta);
						// move probe and try again
						probe_pos = probe_pos - have_delta;
						DLOG("probe_pos " << probe_pos << " = probe_pos - have_delta " << have_delta << " (want_delta " << want_delta << ")");
						continue;
					}
				}
				if (low_pos <= target_pos && target_pos <= high_pos) {
					// have keys on either side of target_pos in result
					// search from the high end until we find the highest key below target
					for (auto i = result.rbegin(); i != result.rend(); ++i) {
						// more correctness checking for fetch
						THROW_CHECK2(out_of_range, *i, probe_pos, probe_pos <= *i);
						if (*i <= target_pos) {
							DLOG("return: *i " << *i << " <= target_pos " << target_pos);
							return *i;
						}
					}
					// if the list is empty then low_pos = high_pos = end_pos
					// if target_pos = end_pos also, then we will execute the loop
					// above but not find any matching entries.
					THROW_CHECK0(runtime_error, result.empty());
				}
				if (target_pos <= low_pos) {
					// results are all too high, so probe_pos..low_pos is too high
					// lower the high bound to the probe pos
					upper_bound = probe_pos;
					DLOG("upper_bound = probe_pos " << probe_pos);
				}
				if (high_pos < target_pos) {
					// results are all too low, so probe_pos..high_pos is too low
					// raise the low bound to the high_pos
					DLOG("lower_bound = high_pos " << high_pos);
					lower_bound = high_pos;
				}
				// compute a new probe pos at the middle of the range and try again
				// we can't have a zero-size range here because we would not have set found_low yet
				THROW_CHECK2(out_of_range, lower_bound, upper_bound, lower_bound <= upper_bound);
				const Pos delta = (upper_bound - lower_bound) / 2;
				probe_pos = lower_bound + delta;
				if (delta < 1) {
					// nothing can exist in the range (lower_bound, upper_bound)
					// and an object is known to exist at lower_bound
					DLOG("return: probe_pos == lower_bound " << lower_bound);
					return lower_bound;
				}
				THROW_CHECK2(out_of_range, lower_bound, probe_pos, lower_bound <= probe_pos);
				THROW_CHECK2(out_of_range, upper_bound, probe_pos, probe_pos <= upper_bound);
				DLOG("loop: lower_bound " << lower_bound << ", probe_pos " << probe_pos << ", upper_bound " << upper_bound);
			}
			THROW_ERROR(runtime_error, "FIXME: should not reach this line: "
				"lower_bound..upper_bound " << lower_bound << ".." << upper_bound << ", "
				"found_low " << found_low);
		} catch (...) {
			DOUT(cerr);
			throw;
		}
	}
}

#endif // _CRUCIBLE_SEEKER_H_

