#include "crucible/error.h"
#include "crucible/fd.h"
#include "crucible/fs.h"
#include "crucible/string.h"
#include "crucible/time.h"

#include <algorithm>
#include <cassert>
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

// until we reboot some machines
// it's time to reboot some machines
// not until the latest memory leak is fixed
// OK Go
#if 1
#define EXTENT_SAME_CLASS BtrfsExtentSame
static const bool ALWAYS_ALIGN = false;
#else
#define EXTENT_SAME_CLASS BtrfsExtentSameByClone
static const bool ALWAYS_ALIGN = false;
#endif

static const int EXTENT_ALIGNMENT = 4096;

// Not much point in exceeding this.  btrfs should (?)  merge adjacent
// extents, non-adjacent extents have to be separate anyway, and it has
// really nasty latency when it gets big.
// const off_t max_step_size = BTRFS_MAX_DEDUPE_LEN;
const off_t max_step_size = 128 * 1024 * 1024;

// Not a good idea to go below 4K
const off_t min_step_size = 4096;

// Give up fairly early - 1MB
const off_t max_contiguous_differences = 1 * 1024 * 1024;

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
bool
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
	uint64_t contiguous_differences = 0;
	uint64_t total_differences = 0;
	uint64_t total_shared = 0;
	uint64_t total_holes = 0;

	bool fatal_error = false;

	off_t p, len;
	ostringstream oss;
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
			<< "total " << common_size
			<< (total_deduped ? " **DUP** " : " dup ") << total_deduped
			<< " diff " << total_differences
			<< " shared " << total_shared
			<< " holes " << total_holes
			<< " off " << p
			<< " len " << len
			<< ' ';

		PhysicalBlockRange incumbent_pbr(incumbent_fd, p, len);
		PhysicalBlockRange candidate_pbr(candidate_fd, p, len);

		if (incumbent_pbr.m_start == candidate_pbr.m_start) {
			off_t shared_len = min(incumbent_pbr.m_end - incumbent_pbr.m_start, candidate_pbr.m_end - candidate_pbr.m_start);
			this_step_size = max(min_step_size, min(shared_len, common_size - p));
			total_shared += this_step_size;
			contiguous_differences = 0;
			len = shared_len;
			// At this point, if we see anything shared, it's because we already deduped the whole thing
			// unless it's a hole.  We do have those.
			if (incumbent_pbr.m_start) {
				// break;
			} else {
				total_holes += shared_len;
			}
		} else {

			// These might be triggering a locking bug
			// ...actually we found the locking bug and it's somewhere else
			// ...though there may be a memory leak bug so let's try turning this back off again
			// ...nope, seems to be a kernel bug triggered by git
			// ...but we still run out of RAM, hard, on some machines running this code.  But not all.
			// ...let's avoid the scary syscalls until we prove they work, OK?
			// ...ok let's go looking for scary syscall behavior now
			// ...no hangs but they don't seem to be helping, or are helping negatively
			// ...long stalls here, see if they go away
			// OK we were totally doing the wrong thing here.  "common_size - p", indeed.
			// DIE_IF_MINUS_ONE(posix_fadvise(incumbent_fd, p, common_size - p, POSIX_FADV_WILLNEED));
			// DIE_IF_MINUS_ONE(posix_fadvise(candidate_fd, p, common_size - p, POSIX_FADV_WILLNEED));
			DIE_IF_MINUS_ONE(readahead(incumbent_fd, p, len));
			DIE_IF_MINUS_ONE(readahead(candidate_fd, p, len));

			EXTENT_SAME_CLASS bes(incumbent_fd, p, len);
			bes.add(candidate_fd, p);
			bes.do_ioctl();

			int status = bes.m_info[0].status;

			if (status == 0) {
				++status_ok;
				total_deduped += bes.m_info[0].bytes_deduped;
				contiguous_differences = 0;
				if (step_size * 2 <= max_step_size) {
					step_size *= 2;
				}
			} else {
				if (status < 0) {
					oss << " (" << strerror(-status) << ", errno = " << -status << ")" << endl;
					++status_err;
					switch (-status) {
						case EXDEV:
							oss << " (fatal error)" << endl;
							THROW_ERRNO(-status);
							break;
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
					contiguous_differences += step_size;
					if (total_deduped == 0 && contiguous_differences > max_contiguous_differences) {
						oss << " (giving up, contiguous_differences = " << contiguous_differences
						    << ", max_contiguous_differences = " << max_contiguous_differences << ")" << endl;
						break;
					}
				}
			}
		}

		p += len;
	}
	cerr << oss.str() << "\r"
		<< "total " << common_size
		<< (total_deduped ? " **DUP** " : " dup ") << total_deduped
		<< " diff " << total_differences
		<< " shared " << total_shared
		<< " holes " << total_holes
		<< " off " << p
		<< " len " << len
		<< "     "
		<< endl;

	return status_ok > 0 && status_err == 0 && status_different == 0;
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
	Fd candidate_fd = open_or_die(argv[2], O_RDWR);

	int rv;
	if (bees_same_file(incumbent_fd, candidate_fd)) {
		rv = EXIT_SUCCESS;
	} else {
		// any run that doesn't end with terminate() is success
		// rv = EXIT_FAILURE;
		rv = EXIT_SUCCESS;
	}

// Let's try not doing this to see if our memory leaks go away
// OK we have memory leak fixes, bring on the extra testing
// OK it's slow and unnecessary in this context
#if 0
	catch_all([&]() {
		PhysicalBlockRange pbr(incumbent_fd, 0);
		cerr << "pbr = " << to_hex(pbr.m_start) << ".." << to_hex(pbr.m_end) << endl;
		set<uint64_t> inodes_seen;
		set<string> paths_seen;
		if (pbr.m_start) {
			BtrfsIoctlLogicalInoArgs lia(pbr.m_start);
			lia.do_ioctl(incumbent_fd);
			// cerr << &lia;
                	// [0] = BtrfsInodeOffsetRoot {  .m_inum = 10544359, .m_offset = 0x0, .m_root = 257},
			for (auto i : lia.m_iors) {
				auto seen_inode = inodes_seen.insert(i.m_inum);
				if (!seen_inode.second) {
					continue;
				}
				cerr << "Root " << i.m_root << " Inode " << i.m_inum << " Offset " << to_hex(i.m_offset) << "\n";
				catch_all([&]() {
					// cerr << "Inode " << i.m_inum << ":\n";
					BtrfsIoctlInoPathArgs ipa(i.m_inum);
					ipa.do_ioctl(incumbent_fd);
					for (auto p : ipa.m_paths) {
						auto seen_path = paths_seen.insert(p);
						if (seen_path.second) {
							cerr << "\tPath " << p << "\n";
						}
					}
					// Not useful without the rest of the root tree
					// BtrfsIoctlInoLookupArgs ila(BTRFS_FIRST_FREE_OBJECTID);
					// ila.do_ioctl(incumbent_fd);
					// cerr << "ila = '" << ila.name << "'\n";
				});
			}
		}
	});
#endif

	return rv;
}
