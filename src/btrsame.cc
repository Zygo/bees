#include "crucible/error.h"
#include "crucible/fd.h"
#include "crucible/fs.h"
#include "crucible/string.h"
#include "crucible/time.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE             /* for readahead() */
#endif
#include <fcntl.h>

#include <sys/stat.h>
#include <unistd.h>

using namespace crucible;
using namespace std;

#define EXTENT_SAME_CLASS BtrfsExtentSame
static const bool ALWAYS_ALIGN = false;
static const bool OPEN_RDONLY = true;

static const int EXTENT_ALIGNMENT = 4096;

// const off_t max_step_size = BTRFS_MAX_DEDUPE_LEN;
// btrfs maximum extent size is 128M, there is nothing to gain by going larger;
// however, going smaller will create a bunch of adjacent split extent refs.
const off_t max_step_size = 128 * 1024 * 1024;

// Not a good idea to go below 4K
const off_t min_step_size = 4096;

struct PhysicalBlockRange {
	uint64_t m_start, m_end;

	PhysicalBlockRange(int fd, uint64_t offset, uint64_t len = 4096);
};

PhysicalBlockRange::PhysicalBlockRange(int fd, uint64_t offset, uint64_t len) :
	m_start(0), m_end(0)
{
	Fiemap emap(offset, len);
	emap.do_ioctl(fd);

	if (emap.m_extents.empty()) {
		// No extents in range, we are in a hole after the last extent
		m_start = 0;
		m_end = len;
		return;
	}

	const FiemapExtent &fe = emap.m_extents.at(0);

	if (offset < fe.fe_logical) {
		// Extent begins after offset, we are in a hole before the extent
		m_start = 0;
		m_end = fe.fe_logical - offset;
		return;
	}

	// TODO:  reject preallocated and delallocated extents too
	// TODO:  well preallocated might be OK for dedup

	uint64_t extent_offset = offset - fe.fe_logical;

	m_start = fe.fe_physical + extent_offset;
	uint64_t phys_length = fe.fe_length - extent_offset;
	m_end = m_start + phys_length;
}

static
bool
verbose()
{
	static bool done = false;
	static bool verbose;
	if (!done) {
		verbose = getenv("BTRSAME_VERBOSE");
		done = true;
	}
	return verbose;
}

static
string
pretty(double d)
{
	static const char * units[] = { "", "K", "M", "G", "T", "P", "E" };
	static const char * *units_stop = units + sizeof(units) / sizeof(units[0]) - 1;
	const char * *unit = units;
	while (d >= 1024 && unit < units_stop) {
		d /= 1024;
		++unit;
	}
	ostringstream oss;
	oss << (round(d * 1000.0) / 1000.0) << *unit;
	return oss.str();
}

static
void
bees_same_file(Fd incumbent_fd, Fd candidate_fd)
{
	Stat incumbent_stat(incumbent_fd);
	Stat candidate_stat(candidate_fd);
	off_t common_size = min(incumbent_stat.st_size, candidate_stat.st_size);

	// If we are using clone instead of extent-same then we can ignore
	// the alignment restriction for the last block of the dest file.
	// This only works when both files are the same size.
	if (ALWAYS_ALIGN || candidate_stat.st_size != incumbent_stat.st_size) {
		common_size &= ~(EXTENT_ALIGNMENT - 1);
	}

	if (verbose()) {
		cerr << "A size " << incumbent_stat.st_size << ", B size " << candidate_stat.st_size << ", common size " << common_size << endl;
	}

	off_t total_deduped = 0;
	int status_ok = 0, status_err = 0, status_different = 0;
	off_t step_size = max_step_size;
	uint64_t total_differences = 0;
	uint64_t total_shared = 0;
	uint64_t total_holes = 0;

	bool fatal_error = false;

	vector<uint8_t> silly_buffer(max_step_size);

	off_t p;
	off_t len = 0;
	ostringstream oss;
	Timer elapsed;
	Timer timer;
	for (p = 0; p < common_size && !fatal_error; ) {
		off_t this_step_size = step_size;
		len = min(common_size - p, step_size);

		if (timer > 1.0) {
			cerr << oss.str() << flush;
			timer.reset();
		}
		oss.str("");
		oss << "\r"
			<< "total " << pretty(common_size)
			<< (total_deduped ? " **DUP** " : " dup ") << pretty(total_deduped)
			<< " diff " << pretty(total_differences)
			<< " shared " << pretty(total_shared)
			<< " holes " << pretty(total_holes)
			<< " off " << pretty(p)
			<< " len " << pretty(len)
			<< " elapsed " << elapsed
			<< "   \b\b\b";

		PhysicalBlockRange incumbent_pbr(incumbent_fd, p, len);
		PhysicalBlockRange candidate_pbr(candidate_fd, p, len);

		if (incumbent_pbr.m_start == candidate_pbr.m_start) {
			off_t shared_len = min(incumbent_pbr.m_end - incumbent_pbr.m_start, candidate_pbr.m_end - candidate_pbr.m_start);
			this_step_size = max(min_step_size, min(shared_len, common_size - p));
			total_shared += this_step_size;
			len = shared_len;
			// At this point, if we see anything shared, it's because we already deduped the whole thing
			// unless it's a hole.  We do have those.
			if (!incumbent_pbr.m_start) {
				total_holes += shared_len;
			}
		} else {

			DIE_IF_MINUS_ONE(readahead(incumbent_fd, p, len));
			DIE_IF_MINUS_ONE(readahead(candidate_fd, p, len));
			// The above kernel calls request readahead, same as posix_fadvise ... MADV_WILLNEED
			// but in btrfs the readahead iops are scheduled at idle priority.
			// This is not what we want, so here we can use read to force non-idle priority
			// (or just use a scheduler that doesn't support io priority).
			// DIE_IF_MINUS_ONE(pread(incumbent_fd, silly_buffer.data(), len, p));
			// DIE_IF_MINUS_ONE(pread(candidate_fd, silly_buffer.data(), len, p));

			EXTENT_SAME_CLASS bes(incumbent_fd, p, len);
			bes.add(candidate_fd, p);
			bes.do_ioctl();

			// Don't need it any more, might either speed up page reclaim, or
			// make us block waiting for writeback.
			DIE_IF_MINUS_ONE(posix_fadvise(incumbent_fd, p, len, POSIX_FADV_DONTNEED));
			DIE_IF_MINUS_ONE(posix_fadvise(candidate_fd, p, len, POSIX_FADV_DONTNEED));

			int status = bes.m_info[0].status;

			if (status == 0) {
				++status_ok;
				total_deduped += bes.m_info[0].bytes_deduped;
				if (step_size * 2 <= max_step_size) {
					step_size *= 2;
				}
			} else {
				if (status < 0) {
					oss << " (" << strerror(-status) << ", errno = " << -status << ")" << endl;
					++status_err;
					switch (-status) {
						case EXDEV:
							oss << " (fatal error, paths are not on the same mount point?)" << endl;
							return;
					}
				} else if (status == BTRFS_SAME_DATA_DIFFERS) {
					++status_different;
				} else {
					++status_err;
				}
				if (step_size > min_step_size) {
					step_size = min_step_size;
					continue;
				} else {
					total_differences += step_size;
				}
			}
		}

		p += len;
	}
	cerr << oss.str() << "\r"
		<< "total " << pretty(common_size)
		<< (total_deduped ? " **DUP** " : " dup ") << pretty(total_deduped)
		<< " diff " << pretty(total_differences)
		<< " shared " << pretty(total_shared)
		<< " holes " << pretty(total_holes)
		<< " off " << pretty(p)
		<< " len " << pretty(len)
		<< " elapsed " << elapsed
		<< "     "
		<< endl;
}

int
main(int argc, char **argv)
{
	if (argc != 3) {
		cerr << "Usage: " << argv[0] << " file1 file2" << endl;
		cerr << "Uses the BTRFS_EXTENT_SAME ioctl to deduplicate file1 and file2" << endl;
		exit(EXIT_FAILURE);
	}

	if (verbose()) {
		cerr << "A: " << argv[1] << endl;
	}
	Fd incumbent_fd = open_or_die(argv[1], O_RDONLY);

	if (verbose()) {
		cerr << "B: " << argv[2] << endl;
	}
	Fd candidate_fd = open_or_die(argv[2], OPEN_RDONLY ? O_RDONLY : O_RDWR);

	bees_same_file(incumbent_fd, candidate_fd);

	// any run that doesn't end with terminate() is success,
	return EXIT_SUCCESS;
}
