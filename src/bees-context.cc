#include "bees.h"

#include "crucible/cleanup.h"
#include "crucible/limits.h"
#include "crucible/string.h"
#include "crucible/task.h"

#include <fstream>
#include <iostream>
#include <vector>

// round
#include <cmath>
// struct rusage
#include <sys/resource.h>

// struct sigset
#include <signal.h>

using namespace crucible;
using namespace std;

BeesFdCache::BeesFdCache(shared_ptr<BeesContext> ctx) :
	m_ctx(ctx)
{
	m_root_cache.func([&](uint64_t root) -> Fd {
		Timer open_timer;
		auto rv = m_ctx->roots()->open_root_nocache(root);
		BEESCOUNTADD(open_root_ms, open_timer.age() * 1000);
		return rv;
	});
	m_root_cache.max_size(BEES_ROOT_FD_CACHE_SIZE);
	m_file_cache.func([&](uint64_t root, uint64_t ino) -> Fd {
		Timer open_timer;
		auto rv = m_ctx->roots()->open_root_ino_nocache(root, ino);
		BEESCOUNTADD(open_ino_ms, open_timer.age() * 1000);
		return rv;
	});
	m_file_cache.max_size(BEES_FILE_FD_CACHE_SIZE);
}

void
BeesFdCache::clear()
{
	BEESLOGDEBUG("Clearing root FD cache with size " << m_root_cache.size() << " to enable subvol delete");
	BEESNOTE("Clearing root FD cache with size " << m_root_cache.size());
	m_root_cache.clear();
	BEESCOUNT(root_clear);

	BEESLOGDEBUG("Clearing open FD cache with size " << m_file_cache.size() << " to enable file delete");
	BEESNOTE("Clearing open FD cache with size " << m_file_cache.size());
	m_file_cache.clear();
	BEESCOUNT(open_clear);
}

Fd
BeesFdCache::open_root(uint64_t root)
{
	return m_root_cache(root);
}

Fd
BeesFdCache::open_root_ino(uint64_t root, uint64_t ino)
{
	return m_file_cache(root, ino);
}

void
BeesContext::dump_status()
{
	auto status_charp = getenv("BEESSTATUS");
	if (!status_charp) return;
	string status_file(status_charp);
	BEESLOGINFO("Writing status to file '" << status_file << "' every " << BEES_STATUS_INTERVAL << " sec");
	Timer total_timer;
	while (!m_stop_status) {
		BEESNOTE("writing status to file '" << status_file << "'");
		ofstream ofs(status_file + ".tmp");

		auto thisStats = BeesStats::s_global;
		ofs << "TOTAL:\n";
		ofs << "\t" << thisStats << "\n";
		auto avg_rates = thisStats / total_timer.age();
		ofs << "RATES:\n";
		ofs << "\t" << avg_rates << "\n";

		const auto load_stats = TaskMaster::get_current_load();
		ofs << "THREADS (work queue " << TaskMaster::get_queue_count() << " of " << Task::instance_count() << " tasks, " << TaskMaster::get_thread_count() << " workers, load: current " << load_stats.current_load << " target " << load_stats.thread_target << " average " << load_stats.loadavg << "):\n";
		for (auto t : BeesNote::get_status()) {
			ofs << "\ttid " << t.first << ": " << t.second << "\n";
		}
#if 0
		// Huge amount of data, not a lot of information (yet)
		ofs << "WORKERS:\n";
		TaskMaster::print_workers(ofs);
		ofs << "QUEUE:\n";
		TaskMaster::print_queue(ofs);
#endif

		ofs << "PROGRESS:\n";
		ofs << get_progress();

		ofs.close();

		BEESNOTE("renaming status file '" << status_file << "'");
		rename((status_file + ".tmp").c_str(), status_file.c_str());

		BEESNOTE("idle " << BEES_STATUS_INTERVAL);
		unique_lock<mutex> lock(m_stop_mutex);
		if (m_stop_status) {
			return;
		}
		m_stop_condvar.wait_for(lock, chrono::duration<double>(BEES_STATUS_INTERVAL));
	}
}

void
BeesContext::set_progress(const string &str)
{
	unique_lock<mutex> lock(m_progress_mtx);
	m_progress_str = str;
}

string
BeesContext::get_progress()
{
	unique_lock<mutex> lock(m_progress_mtx);
	if (m_progress_str.empty()) {
		return "[No progress estimate available]\n";
	}
	return m_progress_str;
}

void
BeesContext::show_progress()
{
	auto lastStats = BeesStats::s_global;
	Timer stats_timer;
	Timer all_timer;
	while (!stop_requested()) {
		BEESNOTE("idle " << BEES_PROGRESS_INTERVAL);

		unique_lock<mutex> lock(m_stop_mutex);
		if (m_stop_requested) {
			return;
		}
		m_stop_condvar.wait_for(lock, chrono::duration<double>(BEES_PROGRESS_INTERVAL));

		// Snapshot stats and timer state
		auto thisStats = BeesStats::s_global;
		auto stats_age = stats_timer.age();
		auto all_age = all_timer.age();
		stats_timer.lap();

		BEESNOTE("logging event counter totals for last " << all_timer);
		BEESLOGINFO("TOTAL COUNTS (" << all_age << "s):\n\t" << thisStats);

		BEESNOTE("logging event counter rates for last " << all_timer);
		auto avg_rates = thisStats / all_age;
		BEESLOGINFO("TOTAL RATES (" << all_age << "s):\n\t" << avg_rates);

		BEESNOTE("logging event counter delta counts for last " << stats_age);
		BEESLOGINFO("DELTA COUNTS (" << stats_age << "s):");

		auto deltaStats = thisStats - lastStats;
		BEESLOGINFO("\t" << deltaStats / stats_age);

		BEESNOTE("logging event counter delta rates for last " << stats_age);
		BEESLOGINFO("DELTA RATES (" << stats_age << "s):");

		auto deltaRates = deltaStats / stats_age;
		BEESLOGINFO("\t" << deltaRates);

		BEESNOTE("logging current thread status");
		const auto load_stats = TaskMaster::get_current_load();
		BEESLOGINFO("THREADS (work queue " << TaskMaster::get_queue_count() << " of " << Task::instance_count() << " tasks, " << TaskMaster::get_thread_count() << " workers, load: current " << load_stats.current_load << " target " << load_stats.thread_target << " average " << load_stats.loadavg << "):");
		for (auto t : BeesNote::get_status()) {
			BEESLOGINFO("\ttid " << t.first << ": " << t.second);
		}

		// No need to log progress here, it is logged when set

		lastStats = thisStats;
	}
}

Fd
BeesContext::home_fd()
{
	if (!!m_home_fd) {
		return m_home_fd;
	}

	const char *base_dir = getenv("BEESHOME");
	if (!base_dir) {
		base_dir = ".beeshome";
	}
	m_home_fd = openat(root_fd(), base_dir, FLAGS_OPEN_DIR);
	if (!m_home_fd) {
		THROW_ERRNO("openat: " << name_fd(root_fd()) << " / " << base_dir);
	}
	return m_home_fd;
}

bool
BeesContext::is_root_ro(uint64_t const root)
{
	return roots()->is_root_ro(root);
}

bool
BeesContext::dedup(const BeesRangePair &brp_in)
{
	// TOOLONG and NOTE can retroactively fill in the filename details, but LOG can't
	BEESNOTE("dedup " << brp_in);
	BEESTRACE("dedup " << brp_in);

	if (is_root_ro(brp_in.second.fid().root())) {
		// BEESLOGDEBUG("WORKAROUND: dst root " << (brp_in.second.fid().root()) << " is read-only);
		BEESCOUNT(dedup_workaround_btrfs_send);
		return false;
	}

	auto brp = brp_in;
	brp.first.fd(shared_from_this());
	brp.second.fd(shared_from_this());

	BEESTOOLONG("dedup " << brp);

	BeesAddress first_addr(brp.first.fd(), brp.first.begin());
	BeesAddress second_addr(brp.second.fd(), brp.second.begin());

	const auto first_gpoz = first_addr.get_physical_or_zero();
	const auto second_gpoz = second_addr.get_physical_or_zero();
	if (first_gpoz == second_gpoz) {
		BEESLOGDEBUG("equal physical addresses " << first_addr << " and " << second_addr << " in dedup");
		BEESCOUNT(bug_dedup_same_physical);
	}

	THROW_CHECK1(invalid_argument, brp, !brp.first.overlaps(brp.second));
	THROW_CHECK1(invalid_argument, brp, brp.first.size() == brp.second.size());

	BEESCOUNT(dedup_try);

	BEESNOTE("waiting to dedup " << brp);
	auto lock = MultiLocker::get_lock("dedupe");

	BEESLOGINFO("dedup: src " << pretty(brp.first.size())  << " [" << to_hex(brp.first.begin())  << ".." << to_hex(brp.first.end())  << "] {" << first_addr  << "} " << name_fd(brp.first.fd()) << "\n"
		 << "       dst " << pretty(brp.second.size()) << " [" << to_hex(brp.second.begin()) << ".." << to_hex(brp.second.end()) << "] {" << second_addr << "} " << name_fd(brp.second.fd()));
	BEESNOTE("dedup: src " << pretty(brp.first.size())  << " [" << to_hex(brp.first.begin())  << ".." << to_hex(brp.first.end())  << "] {" << first_addr  << "} " << name_fd(brp.first.fd()) << "\n"
		 << "       dst " << pretty(brp.second.size()) << " [" << to_hex(brp.second.begin()) << ".." << to_hex(brp.second.end()) << "] {" << second_addr << "} " << name_fd(brp.second.fd()));

	while (true) {
		try {
			Timer dedup_timer;
			const bool rv = btrfs_extent_same(brp.first.fd(), brp.first.begin(), brp.first.size(), brp.second.fd(), brp.second.begin());
			BEESCOUNTADD(dedup_ms, dedup_timer.age() * 1000);

			if (rv) {
				BEESCOUNT(dedup_hit);
				BEESCOUNTADD(dedup_bytes, brp.first.size());
			} else {
				BEESCOUNT(dedup_miss);
				BEESLOGINFO("NO Dedup! " << brp);
			}

			lock.reset();
			bees_throttle(dedup_timer.age(), "dedup");
			return rv;
		} catch (const std::system_error &e) {
			if (e.code().value() == EAGAIN) {
				BEESNOTE("dedup waiting for btrfs send on " << brp.second);
				BEESLOGDEBUG("dedup waiting for btrfs send on " << brp.second);
				roots()->wait_for_transid(1);
			} else {
				throw;
			}
		}
	}
}

BeesRangePair
BeesContext::dup_extent(const BeesFileRange &src, const shared_ptr<BeesTempFile> &tmpfile)
{
	BEESTRACE("dup_extent " << src);
	BEESCOUNTADD(dedup_copy, src.size());
	return BeesRangePair(tmpfile->make_copy(src), src);
}

void
BeesContext::rewrite_file_range(const BeesFileRange &bfr)
{
	auto m_ctx = shared_from_this();
	BEESNOTE("Rewriting bfr " << bfr);
	auto rewrite_tmpfile = tmpfile();
	BeesRangePair dup_brp(dup_extent(BeesFileRange(bfr.fd(), bfr.begin(), min(bfr.file_size(), bfr.end())), rewrite_tmpfile));
	// BEESLOG("\tdup_brp " << dup_brp);
	BeesBlockData orig_bbd(bfr.fd(), bfr.begin(), min(BLOCK_SIZE_SUMS, bfr.size()));
	// BEESLOG("\torig_bbd " << orig_bbd);
	BeesBlockData dup_bbd(dup_brp.first.fd(), dup_brp.first.begin(), min(BLOCK_SIZE_SUMS, dup_brp.first.size()));
	// BEESLOG("BeesResolver br(..., " << bfr << ")");
	BEESTRACE("BeesContext::rewrite_file_range calling BeesResolver " << bfr);
	BeesResolver br(m_ctx, BeesAddress(bfr.fd(), bfr.begin()));
	BEESTRACE("BeesContext::rewrite_file_range calling replace_src " << dup_bbd);
	// BEESLOG("\treplace_src " << dup_bbd);
	br.replace_src(dup_bbd);
	BEESCOUNT(scan_rewrite);

	// All the blocks are now somewhere else so scan again.
	// We do this immediately instead of waiting for a later generation scan
	// because the blocks we rewrote are likely duplicates of blocks from this
	// generation that we are about to scan.  Pretty ugly but effective as an
	// interim solution while we wait for tree-2 extent scanning.
	auto hash_table = m_ctx->hash_table();
	BtrfsExtentWalker ew(bfr.fd(), bfr.begin(), root_fd());
	for (off_t next_p = bfr.begin(); next_p < bfr.end(); ) {
		off_t p = next_p;
		next_p += BLOCK_SIZE_SUMS;
		ew.seek(p);
		Extent e = ew.current();
		BEESTRACE("next_p " << to_hex(next_p) << " p " << to_hex(p) << " e " << e);
		BeesBlockData bbd(bfr.fd(), p, min(BLOCK_SIZE_SUMS, e.end() - p));
		BeesAddress addr(e, p);
		bbd.addr(addr);
		if (!addr.is_magic() && !bbd.is_data_zero()) {
			hash_table->push_random_hash_addr(bbd.hash(), bbd.addr());
			BEESCOUNT(scan_reinsert);
		}
	}
}

struct BeesSeenRange {
	uint64_t bytenr;
	off_t offset;
	off_t length;
};

static
bool
operator<(const BeesSeenRange &bsr1, const BeesSeenRange &bsr2)
{
	return tie(bsr1.bytenr, bsr1.offset, bsr1.length) < tie(bsr2.bytenr, bsr2.offset, bsr2.length);
}

static
__attribute__((unused))
ostream&
operator<<(ostream &os, const BeesSeenRange &tup)
{
	return os << "BeesSeenRange { " << to_hex(tup.bytenr) << ", " << to_hex(tup.offset) << "+" << pretty(tup.length) << " }";
}

void
BeesContext::scan_one_extent(const BeesFileRange &bfr, const Extent &e)
{
	BEESNOTE("Scanning " << pretty(e.size()) << " "
		<< to_hex(e.begin()) << ".." << to_hex(e.end())
		<< " " << name_fd(bfr.fd()) );
	BEESTRACE("scan extent " << e);
	BEESTRACE("scan bfr " << bfr);
	BEESCOUNT(scan_extent);

	Timer one_timer;

	// We keep moving this method around
	auto m_ctx = shared_from_this();

	shared_ptr<BeesHashTable> hash_table = m_ctx->hash_table();

	if (e.flags() & ~(
		FIEMAP_EXTENT_ENCODED | FIEMAP_EXTENT_UNKNOWN |
		FIEMAP_EXTENT_DELALLOC | FIEMAP_EXTENT_LAST |
		FIEMAP_EXTENT_SHARED | FIEMAP_EXTENT_NOT_ALIGNED |
		FIEMAP_EXTENT_DATA_INLINE | Extent::HOLE |
		Extent::OBSCURED | Extent::PREALLOC
	)) {
		BEESCOUNT(scan_interesting);
		BEESLOGINFO("Interesting extent flags " << e << " from fd " << name_fd(bfr.fd()));
	}

	if (e.flags() & Extent::HOLE) {
		// Nothing here, dispose of this early
		BEESCOUNT(scan_hole);
		return;
	}

	if (e.flags() & Extent::PREALLOC) {
		// Prealloc is all zero and we replace it with a hole.
		// No special handling is required here.  Nuke it and move on.
		BEESLOGINFO("prealloc extent " << e);
		// Must not extend past EOF
		auto extent_size = min(e.end(), bfr.file_size()) - e.begin();
		// Must hold tmpfile until dedupe is done
		const auto tmpfile = m_ctx->tmpfile();
		BeesFileRange prealloc_bfr(tmpfile->make_hole(extent_size));
		// Apparently they can both extend past EOF
		BeesFileRange copy_bfr(bfr.fd(), e.begin(), e.begin() + extent_size);
		BeesRangePair brp(prealloc_bfr, copy_bfr);
		// Raw dedupe here - nothing else to do with this extent, nothing to merge with
		if (m_ctx->dedup(brp)) {
			BEESCOUNT(dedup_prealloc_hit);
			BEESCOUNTADD(dedup_prealloc_bytes, e.size());
			return;
		} else {
			BEESCOUNT(dedup_prealloc_miss);
		}
	}

	// If we already read this extent and inserted it into the hash table, no need to read it again
	static mutex s_seen_mutex;
	unique_lock<mutex> lock_seen(s_seen_mutex);
	const BeesSeenRange tup = {
		.bytenr = e.bytenr(),
		.offset = e.offset(),
		.length = e.size(),
	};
	static set<BeesSeenRange> s_seen;
	if (s_seen.size() > BEES_MAX_EXTENT_REF_COUNT) {
		s_seen.clear();
		BEESCOUNT(scan_seen_clear);
	}
	const auto seen_rv = s_seen.find(tup) != s_seen.end();
	if (!seen_rv) {
		BEESCOUNT(scan_seen_miss);
	} else {
		// BEESLOGDEBUG("Skip " << tup << " " << e);
		BEESCOUNT(scan_seen_hit);
		return;
	}
	lock_seen.unlock();

	// OK we need to read extent now
	bees_readahead(bfr.fd(), bfr.begin(), bfr.size());

	map<off_t, pair<BeesHash, BeesAddress>> insert_map;
	set<off_t> dedupe_set;
	set<off_t> zero_set;

	// Pretty graphs
	off_t block_count = ((e.size() + BLOCK_MASK_SUMS) & ~BLOCK_MASK_SUMS) / BLOCK_SIZE_SUMS;
	BEESTRACE(e << " block_count " << block_count);
	string bar(block_count, '#');

	// List of dedupes found
	list<BeesRangePair> dedupe_list;
	list<BeesFileRange> copy_list;
	list<pair<BeesHash, BeesAddress>> front_hash_list;
	list<uint64_t> invalidate_addr_list;

	off_t next_p = e.begin();
	for (off_t p = e.begin(); p < e.end(); p += BLOCK_SIZE_SUMS) {

		const off_t bar_p = (p - e.begin()) / BLOCK_SIZE_SUMS;
		BeesAddress addr(e, p);

		// This extent should consist entirely of non-magic blocks
		THROW_CHECK1(invalid_argument, addr, !addr.is_magic());

		// Get block data
		BeesBlockData bbd(bfr.fd(), p, min(BLOCK_SIZE_SUMS, e.end() - p));
		bbd.addr(addr);
		BEESCOUNT(scan_block);

		BEESTRACE("scan bbd " << bbd);

		// Calculate the hash first because it lets us shortcut on is_data_zero
		BEESNOTE("scan hash " << bbd);
		const BeesHash hash = bbd.hash();

		// Weed out zero blocks
		BEESNOTE("is_data_zero " << bbd);
		const bool data_is_zero = bbd.is_data_zero();
		if (data_is_zero) {
			bar.at(bar_p) = '0';
			zero_set.insert(p);
			BEESCOUNT(scan_zero);
			continue;
		}

		// Schedule this block for insertion if we decide to keep this extent.
		BEESCOUNT(scan_hash_preinsert);
		BEESTRACE("Pushing hash " << hash << " addr " << addr << " bbd " << bbd);
		insert_map.insert(make_pair(p, make_pair(hash, addr)));
		bar.at(bar_p) = 'i';

		// Ensure we fill in the entire insert_map without skipping any non-zero blocks
		if (p < next_p) continue;

		BEESNOTE("lookup hash " << bbd);
		const auto found = hash_table->find_cell(hash);
		BEESCOUNT(scan_lookup);

		set<BeesAddress> found_addrs;
		list<BeesAddress> ordered_addrs;

		for (const auto &i : found) {
			BEESTRACE("found (hash, address): " << i);
			BEESCOUNT(scan_found);

			// Hash has to match
			THROW_CHECK2(runtime_error, i.e_hash, hash, i.e_hash == hash);

			// We know that there is at least one copy of the data and where it is.
			// Filter out anything that can't possibly match before we pull out the
			// LOGICAL_INO hammer.
			BeesAddress found_addr(i.e_addr);

			// If address already in hash table, move on to next extent.
			// Only extents that are scanned but not modified are inserted, so if there's
			// a matching hash:address pair in the hash table:
			// 1.  We have already scanned this extent.
			// 2.  We may have already created references to this extent.
			// 3.  We won't scan this extent again.
			// The current extent is effectively "pinned" and can't be modified
			// without rescanning all the existing references.
			if (found_addr.get_physical_or_zero() == addr.get_physical_or_zero()) {
				// No log message because this happens to many thousands of blocks
				// when bees is interrupted.
				// BEESLOGDEBUG("Found matching hash " << hash << " at same address " << addr << ", skipping " << bfr);
				BEESCOUNT(scan_already);
				return;
			}

			// Address is a duplicate.
			// Check this early so we don't have duplicate counts.
			if (!found_addrs.insert(found_addr).second) {
				BEESCOUNT(scan_twice);
				continue;
			}

			// Block must have matching EOF alignment
			if (found_addr.is_unaligned_eof() != addr.is_unaligned_eof()) {
				BEESCOUNT(scan_malign);
				continue;
			}

			// Hash is toxic
			if (found_addr.is_toxic()) {
				BEESLOGDEBUG("WORKAROUND: abandoned toxic match for hash " << hash << " addr " << found_addr << " matching bbd " << bbd);
				// Don't push these back in because we'll never delete them.
				// Extents may become non-toxic so give them a chance to expire.
				// hash_table->push_front_hash_addr(hash, found_addr);
				BEESCOUNT(scan_toxic_hash);
				return;
			}

			// Put this address in the list without changing hash table order
			ordered_addrs.push_back(found_addr);
		}

		// Cheap filtering is now out of the way, now for some heavy lifting
		for (auto found_addr : ordered_addrs) {
			// Hash table says there's a matching block on the filesystem.
			// Go find refs to it.
			BEESNOTE("resolving " << found_addr << " matched " << bbd);
			BEESTRACE("resolving " << found_addr << " matched " << bbd);
			BEESTRACE("BeesContext::scan_one_extent calling BeesResolver " << found_addr);
			BeesResolver resolved(m_ctx, found_addr);
			// Toxic extents are really toxic
			if (resolved.is_toxic()) {
				BEESLOGDEBUG("WORKAROUND: discovered toxic match at found_addr " << found_addr << " matching bbd " << bbd);
				BEESCOUNT(scan_toxic_match);
				// Make sure we never see this hash again.
				// It has become toxic since it was inserted into the hash table.
				found_addr.set_toxic();
				hash_table->push_front_hash_addr(hash, found_addr);
				return;
			} else if (!resolved.count()) {
				BEESCOUNT(scan_resolve_zero);
				// Didn't find a block at the table address, address is dead
				BEESLOGDEBUG("Erasing stale addr " << addr << " hash " << hash);
				hash_table->erase_hash_addr(hash, found_addr);
				continue;
			} else {
				BEESCOUNT(scan_resolve_hit);
			}

			// `resolved` contains references to a block on the filesystem that still exists.
			bar.at(bar_p) = 'M';

			BEESNOTE("finding one match (out of " << resolved.count() << ") at " << resolved.addr() << " for " << bbd);
			BEESTRACE("finding one match (out of " << resolved.count() << ") at " << resolved.addr() << " for " << bbd);
			auto replaced_brp = resolved.replace_dst(bbd);
			BeesFileRange &replaced_bfr = replaced_brp.second;
			BEESTRACE("next_p " << to_hex(next_p) << " -> replaced_bfr " << replaced_bfr);

			// If we did find a block, but not this hash, correct the hash table and move on
			if (resolved.found_hash()) {
				BEESCOUNT(scan_hash_hit);
			} else {
				BEESLOGDEBUG("Erasing stale hash " << hash << " addr " << resolved.addr());
				hash_table->erase_hash_addr(hash, resolved.addr());
				BEESCOUNT(scan_hash_miss);
				continue;
			}

			// We found a block and it was a duplicate
			if (resolved.found_dup()) {
				THROW_CHECK0(runtime_error, replaced_bfr);
				BEESCOUNT(scan_dup_hit);

				// Save this match.  If a better match is found later,
				// it will be replaced.
				dedupe_list.push_back(replaced_brp);

				// Push matching block to front of LRU
				front_hash_list.push_back(make_pair(hash, resolved.addr()));

				// This is the block that matched in the replaced bfr
				bar.at(bar_p) = '=';

				// Invalidate resolve cache so we can count refs correctly
				invalidate_addr_list.push_back(resolved.addr());
				invalidate_addr_list.push_back(bbd.addr());

				// next_p may be past EOF so check p only
				THROW_CHECK2(runtime_error, p, replaced_bfr, p < replaced_bfr.end());

				// We may find duplicate ranges of various lengths, so make sure
				// we don't pick a smaller one
				next_p = max(next_p, replaced_bfr.end());

				// Stop after one dedupe is found.  If there's a longer matching range
				// out there, we'll find a matching block after the end of this range,
				// since the longer range is longer than this one.
				break;
			} else {
				BEESCOUNT(scan_dup_miss);
			}
		}
	}

	bool force_insert = false;

	// We don't want to punch holes into compressed extents, unless:
	// 1.  There was dedupe of non-zero blocks, so we always have to copy the rest of the extent
	// 2.  The entire extent is zero and the whole thing can be replaced with a single hole
	const bool extent_compressed = e.flags() & FIEMAP_EXTENT_ENCODED;
	if (extent_compressed && dedupe_list.empty() && !insert_map.empty()) {
		// BEESLOGDEBUG("Compressed extent with non-zero data and no dedupe, skipping");
		BEESCOUNT(scan_compressed_no_dedup);
		force_insert = true;
	}

	// FIXME:  dedupe_list contains a lot of overlapping matches.  Get rid of all but one.
	list<BeesRangePair> dedupe_list_out;
	dedupe_list.sort([](const BeesRangePair &a, const BeesRangePair &b) {
		return b.second.size() < a.second.size();
	});
	// Shorten each dedupe brp by removing any overlap with earlier (longer) extents in list
	for (auto i : dedupe_list) {
		bool insert_i = true;
		BEESTRACE("i = " << i << " insert_i " << insert_i);
		for (const auto &j : dedupe_list_out) {
			BEESTRACE("j = " << j);
			// No overlap, try next one
			if (j.second.end() <= i.second.begin() || j.second.begin() >= i.second.end()) {
				continue;
			}
			// j fully overlaps or is the same as i, drop i
			if (j.second.begin() <= i.second.begin() && j.second.end() >= i.second.end()) {
				insert_i = false;
				break;
			}
			// i begins outside j, i ends inside j, remove the end of i
			if (i.second.end() > j.second.begin() && i.second.begin() <= j.second.begin()) {
				const auto delta = i.second.end() - j.second.begin();
				if (delta == i.second.size()) {
					insert_i = false;
					break;
				}
				i.shrink_end(delta);
				continue;
			}
			// i begins inside j, ends outside j, remove the begin of i
			if (i.second.begin() < j.second.end() && i.second.end() >= j.second.end()) {
				const auto delta = j.second.end() - i.second.begin();
				if (delta == i.second.size()) {
					insert_i = false;
					break;
				}
				i.shrink_begin(delta);
				continue;
			}
			// i fully overlaps j, split i into two parts, push the other part onto dedupe_list
			if (j.second.begin() > i.second.begin() && j.second.end() < i.second.end()) {
				auto other_i = i;
				const auto end_left_delta = i.second.end() - j.second.begin();
				const auto begin_right_delta = i.second.begin() - j.second.end();
				i.shrink_end(end_left_delta);
				other_i.shrink_begin(begin_right_delta);
				dedupe_list.push_back(other_i);
				continue;
			}
			// None of the sbove.  Oops!
			THROW_CHECK0(runtime_error, false);
		}
		if (insert_i) {
			dedupe_list_out.push_back(i);
		}
	}
	dedupe_list = dedupe_list_out;
	dedupe_list_out.clear();

	// Count total dedupes
	uint64_t bytes_deduped = 0;
	for (const auto &i : dedupe_list) {
		// Remove deduped blocks from insert map and zero map
		for (off_t ip = i.second.begin(); ip < i.second.end(); ip += BLOCK_SIZE_SUMS) {
			BEESCOUNT(scan_dup_block);
			dedupe_set.insert(ip);
			zero_set.erase(ip);
		}
		bytes_deduped += i.second.size();
	}

	// Copy all blocks of the extent that were not deduped or zero, but don't copy an entire extent
	uint64_t bytes_zeroed = 0;
	if (!force_insert) {
		BEESTRACE("Rewriting extent " << e);
		off_t last_p = e.begin();
		off_t p = last_p;
		off_t next_p = last_p;
		BEESTRACE("next_p " << to_hex(next_p) << " p " << to_hex(p) << " last_p " << to_hex(last_p));
		for (next_p = e.begin(); next_p < e.end(); ) {
			p = next_p;
			next_p = min(next_p + BLOCK_SIZE_SUMS, e.end());

			// Can't be both dedupe and zero
			THROW_CHECK2(runtime_error, zero_set.count(p), dedupe_set.count(p), zero_set.count(p) + dedupe_set.count(p) < 2);
			if (zero_set.count(p)) {
				bytes_zeroed += next_p - p;
			}
			// BEESLOG("dedupe_set.count(" << to_hex(p) << ") " << dedupe_set.count(p));
			if (dedupe_set.count(p)) {
				if (p - last_p > 0) {
					THROW_CHECK2(runtime_error, p, e.end(), p <= e.end());
					copy_list.push_back(BeesFileRange(bfr.fd(), last_p, p));
				}
				last_p = next_p;
			}
		}
		BEESTRACE("last");
		if (next_p > last_p) {
			THROW_CHECK2(runtime_error, next_p, e.end(), next_p <= e.end());
			copy_list.push_back(BeesFileRange(bfr.fd(), last_p, next_p));
		}
	}

	// Don't copy an entire extent
	if (!bytes_zeroed && copy_list.size() == 1 && copy_list.begin()->size() == e.size()) {
		copy_list.clear();
	}

	// Count total copies
	uint64_t bytes_copied = 0;
	for (const auto &i : copy_list) {
		bytes_copied += i.size();
	}

	BEESTRACE("bar: " << bar);

	// Don't do nuisance dedupes part 1:  free more blocks than we create
	THROW_CHECK3(runtime_error, bytes_copied, bytes_zeroed, bytes_deduped, bytes_copied >= bytes_zeroed);
	const auto cost_copy = bytes_copied - bytes_zeroed;
	const auto gain_dedupe = bytes_deduped + bytes_zeroed;
	if (cost_copy > gain_dedupe) {
		BEESLOGDEBUG("Too many bytes copied (" << pretty(bytes_copied) << ") for bytes deduped (" << pretty(bytes_deduped) << ") and holes punched (" << pretty(bytes_zeroed) << "), skipping extent");
		BEESCOUNT(scan_skip_bytes);
		force_insert = true;
	}

	// Don't do nuisance dedupes part 2:  nobody needs more than 100 dedupe/copy ops in one extent
	if (dedupe_list.size() + copy_list.size() > 100) {
		BEESLOGDEBUG("Too many dedupe (" << dedupe_list.size() << ") and copy (" << copy_list.size() << ") operations, skipping extent");
		BEESCOUNT(scan_skip_ops);
		force_insert = true;
	}

	// Track whether we rewrote anything
	bool extent_modified = false;

	// If we didn't delete the dedupe list, do the dedupes now
	for (const auto &i : dedupe_list) {
		BEESNOTE("dedup " << i);
		if (force_insert || m_ctx->dedup(i)) {
			BEESCOUNT(replacedst_dedup_hit);
			THROW_CHECK0(runtime_error, i.second);
			for (off_t ip = i.second.begin(); ip < i.second.end(); ip += BLOCK_SIZE_SUMS) {
				if (ip >= e.begin() && ip < e.end()) {
					off_t bar_p = (ip - e.begin()) / BLOCK_SIZE_SUMS;
					if (bar.at(bar_p) != '=') {
						if (ip == i.second.begin()) {
							bar.at(bar_p) = '<';
						} else if (ip + BLOCK_SIZE_SUMS >= i.second.end()) {
							bar.at(bar_p) = '>';
						} else {
							bar.at(bar_p) = 'd';
						}
					}
				}
			}
			extent_modified = !force_insert;
		} else {
			BEESLOGINFO("dedup failed: " << i);
			BEESCOUNT(replacedst_dedup_miss);
			// User data changed while we were looking up the extent, or we have a bug.
			// We can't fix this, but we can immediately stop wasting effort.
			return;
		}
	}

	// Then the copy/rewrites
	for (const auto &i : copy_list) {
		if (!force_insert) {
			rewrite_file_range(i);
			extent_modified = true;
		}
		for (auto p = i.begin(); p < i.end(); p += BLOCK_SIZE_SUMS) {
			off_t bar_p = (p - e.begin()) / BLOCK_SIZE_SUMS;
			// Leave zeros as-is because they aren't really copies
			if (bar.at(bar_p) != '0') {
				bar.at(bar_p) = '+';
			}
		}
	}

	if (!force_insert) {
		// Push matched hashes to front
		for (const auto &i : front_hash_list) {
			hash_table->push_front_hash_addr(i.first, i.second);
			BEESCOUNT(scan_push_front);
		}
		// Invalidate cached resolves
		for (const auto &i : invalidate_addr_list) {
			m_ctx->invalidate_addr(i);
		}
	}

	// Don't insert hashes pointing to an extent we just deleted
	if (!extent_modified) {
		// We did not rewrite the extent and it contained data, so insert it.
		// BEESLOGDEBUG("Inserting " << insert_map.size() << " hashes from " << bfr);
		for (const auto &i : insert_map) {
			hash_table->push_random_hash_addr(i.second.first, i.second.second);
			off_t bar_p = (i.first - e.begin()) / BLOCK_SIZE_SUMS;
			if (bar.at(bar_p) == 'i') {
				bar.at(bar_p) = '.';
			}
			BEESCOUNT(scan_hash_insert);
		}
	}

	// Visualize
	if (bar != string(block_count, '.')) {
		BEESLOGINFO(
			(force_insert ? "skip" : "scan") << ": "
			<< pretty(e.size()) << " "
			<< dedupe_list.size() << "d" << copy_list.size() << "c"
			<< ((bytes_zeroed + BLOCK_SIZE_SUMS - 1) / BLOCK_SIZE_SUMS) << "p"
			<< (extent_compressed ? "z " : " ")
			<< one_timer << "s {"
			<< to_hex(e.bytenr()) << "+" << to_hex(e.offset()) << "} "
			<< to_hex(e.begin()) << " [" << bar << "] " << to_hex(e.end())
			<< ' ' << name_fd(bfr.fd())
		);
	}

	// Put this extent into the recently seen list if we didn't rewrite it,
	// and remove it if we did.
	lock_seen.lock();
	if (extent_modified) {
		s_seen.erase(tup);
		BEESCOUNT(scan_seen_erase);
	} else {
		// BEESLOGDEBUG("Seen " << tup << " " << e);
		s_seen.insert(tup);
		BEESCOUNT(scan_seen_insert);
	}
	lock_seen.unlock();

	// Now causes 75% loss of performance in benchmarks
	// bees_unreadahead(bfr.fd(), bfr.begin(), bfr.size());
}

shared_ptr<Exclusion>
BeesContext::get_inode_mutex(const uint64_t inode)
{
	return m_inode_locks(inode);
}

bool
BeesContext::scan_forward(const BeesFileRange &bfr_in)
{
	BEESTRACE("scan_forward " << bfr_in);
	BEESCOUNT(scan_forward);

	Timer scan_timer;

	// Silently filter out blacklisted files
	if (is_blacklisted(bfr_in.fid())) {
		BEESCOUNT(scan_blacklisted);
		return false;
	}

	// Reconstitute FD
	BEESNOTE("scan open " << bfr_in);
	auto bfr = bfr_in;
	bfr.fd(shared_from_this());

	BEESNOTE("scan extent " << bfr);

	// No FD?  Well, that was quick.
	if (!bfr.fd()) {
		// BEESLOGINFO("No FD in " << root_path() << " for " << bfr);
		BEESCOUNT(scanf_no_fd);
		return false;
	}

	// Sanity check
	if (bfr.begin() >= bfr.file_size()) {
		BEESLOGDEBUG("past EOF: " << bfr);
		BEESCOUNT(scanf_eof);
		return false;
	}

	BtrfsExtentWalker ew(bfr.fd(), bfr.begin(), root_fd());

	Extent e;
	bool start_over = false;
	catch_all([&]() {
		while (!stop_requested() && !start_over) {
			e = ew.current();

			catch_all([&]() {
				uint64_t extent_bytenr = e.bytenr();
				auto extent_mutex = m_extent_locks(extent_bytenr);
				const auto extent_lock = extent_mutex->try_lock(Task::current_task());
				if (!extent_lock) {
					// BEESLOGDEBUG("Deferring extent bytenr " << to_hex(extent_bytenr) << " from " << bfr);
					BEESCOUNT(scanf_deferred_extent);
					start_over = true;
					return; // from closure
				}
				Timer one_extent_timer;
				scan_one_extent(bfr, e);
				// BEESLOGDEBUG("Scanned " << e << " " << bfr);
				BEESCOUNTADD(scanf_extent_ms, one_extent_timer.age() * 1000);
				BEESCOUNT(scanf_extent);
			});

			if (e.end() >= bfr.end()) {
				break;
			}

			if (!ew.next()) {
				break;
			}
		}
	});

	BEESCOUNTADD(scanf_total_ms, scan_timer.age() * 1000);
	BEESCOUNT(scanf_total);

	return start_over;
}

BeesResolveAddrResult::BeesResolveAddrResult()
{
}

shared_ptr<BtrfsIoctlLogicalInoArgs>
BeesContext::logical_ino(const uint64_t logical, const bool all_refs)
{
	const auto rv = m_logical_ino_pool();
	rv->set_logical(logical);
	rv->set_flags(all_refs ? BTRFS_LOGICAL_INO_ARGS_IGNORE_OFFSET : 0);
	return rv;
}

BeesResolveAddrResult
BeesContext::resolve_addr_uncached(BeesAddress addr)
{
	THROW_CHECK1(invalid_argument, addr, !addr.is_magic());
	THROW_CHECK0(invalid_argument, !!root_fd());

	// If we look at per-thread CPU usage we get a better estimate of
	// how badly btrfs is performing without confounding factors like
	// transaction latency, competing threads, and freeze/SIGSTOP
	// pausing the bees process.

	const auto log_ino_ptr = logical_ino(addr.get_physical_or_zero(), false);
	auto &log_ino = *log_ino_ptr;

	// Time how long this takes
	Timer resolve_timer;

	struct rusage usage_before;
	struct rusage usage_after;
	{
		BEESNOTE("waiting to resolve addr " << addr << " with LOGICAL_INO");
		auto lock = MultiLocker::get_lock("logical_ino");

		// Get this thread's system CPU usage
		DIE_IF_MINUS_ONE(getrusage(RUSAGE_THREAD, &usage_before));

		// Restart timer now that we're no longer waiting for lock
		resolve_timer.reset();
		BEESTOOLONG("Resolving addr " << addr << " in " << root_path() << " refs " << log_ino.m_iors.size());
		BEESNOTE("resolving addr " << addr << " with LOGICAL_INO");
		if (log_ino.do_ioctl_nothrow(root_fd())) {
			BEESCOUNT(resolve_ok);
		} else {
			BEESCOUNT(resolve_fail);
		}
		DIE_IF_MINUS_ONE(getrusage(RUSAGE_THREAD, &usage_after));
		const auto resolve_timer_age = resolve_timer.age();
		BEESCOUNTADD(resolve_ms, resolve_timer_age * 1000);
		lock.reset();
		bees_throttle(resolve_timer_age, "resolve_addr");
	}

	const double sys_usage_delta =
		(usage_after.ru_stime.tv_sec + usage_after.ru_stime.tv_usec / 1000000.0) -
		(usage_before.ru_stime.tv_sec + usage_before.ru_stime.tv_usec / 1000000.0);

	const double user_usage_delta =
		(usage_after.ru_utime.tv_sec + usage_after.ru_utime.tv_usec / 1000000.0) -
		(usage_before.ru_utime.tv_sec + usage_before.ru_utime.tv_usec / 1000000.0);

	const auto rt_age = resolve_timer.age();

	BeesResolveAddrResult rv;

	// Avoid performance problems - pretend resolve failed if there are too many refs
	const size_t rv_count = log_ino.m_iors.size();
	if (!rv_count) {
		BEESLOGDEBUG("LOGICAL_INO returned 0 refs at " << to_hex(addr));
		BEESCOUNT(resolve_empty);
	}
	if (rv_count < BEES_MAX_EXTENT_REF_COUNT) {
		rv.m_biors = vector<BtrfsInodeOffsetRoot>(log_ino.m_iors.begin(), log_ino.m_iors.end());
	} else {
		BEESLOGINFO("addr " << addr << " refs " << rv_count << " overflows configured ref limit " << BEES_MAX_EXTENT_REF_COUNT);
		BEESCOUNT(resolve_overflow);
	}

	// Avoid crippling performance bug
	if (sys_usage_delta < BEES_TOXIC_SYS_DURATION) {
		rv.m_is_toxic = false;
	} else {
		BEESLOGDEBUG("WORKAROUND: toxic address: addr = " << addr << ", sys_usage_delta = " << round(sys_usage_delta* 1000.0) / 1000.0 << ", user_usage_delta = " << round(user_usage_delta * 1000.0) / 1000.0 << ", rt_age = " << rt_age << ", refs " << rv_count);
		BEESCOUNT(resolve_toxic);
		rv.m_is_toxic = true;
	}

	// Count how many times this happens so we can figure out how
	// important this case is
	static const size_t max_logical_ino_v1_refs = 2730; // (65536 - header_len) / (sizeof(uint64_t) * 3)
	static size_t most_refs_ever = max_logical_ino_v1_refs;
	if (rv_count > most_refs_ever) {
		BEESLOGINFO("addr " << addr << " refs " << rv_count << " beats previous record " << most_refs_ever);
		most_refs_ever = rv_count;
	}
	if (rv_count > max_logical_ino_v1_refs) {
		BEESCOUNT(resolve_large);
	}

	return rv;
}

BeesResolveAddrResult
BeesContext::resolve_addr(BeesAddress addr)
{
	// All compressed offset addresses resolve to the same physical addr, so use that value for the cache
	return m_resolve_cache(addr.get_physical_or_zero());
}

void
BeesContext::invalidate_addr(BeesAddress addr)
{
	return m_resolve_cache.expire(addr.get_physical_or_zero());
}

void
BeesContext::resolve_cache_clear()
{
	BEESNOTE("clearing resolve cache with size " << m_resolve_cache.size());
	BEESLOGDEBUG("Clearing resolve cache with size " << m_resolve_cache.size());
	return m_resolve_cache.clear();
}

void
BeesContext::set_root_fd(Fd fd)
{
	uint64_t root_fd_treeid = btrfs_get_root_id(fd);
	BEESLOGINFO("set_root_fd " << name_fd(fd));
	BEESTRACE("set_root_fd " << name_fd(fd));
	THROW_CHECK1(invalid_argument, root_fd_treeid, root_fd_treeid == BTRFS_FS_TREE_OBJECTID);
	Stat st(fd);
	THROW_CHECK1(invalid_argument, st.st_ino, st.st_ino == BTRFS_FIRST_FREE_OBJECTID);
	m_root_fd = fd;

	// 65536 is big enough for two max-sized extents.
	// Need enough total space in the cache for the maximum number of active threads.
	m_resolve_cache.max_size(65536);
	m_resolve_cache.func([&](BeesAddress addr) -> BeesResolveAddrResult {
		return resolve_addr_uncached(addr);
	});
}

void
BeesContext::start()
{
	BEESLOGNOTICE("Starting bees main loop...");
	BEESNOTE("starting BeesContext");

	m_extent_locks.func([](uint64_t bytenr) {
		return make_shared<Exclusion>();
		(void)bytenr;
	});
	m_inode_locks.func([](const uint64_t fid) {
		return make_shared<Exclusion>();
		(void)fid;
	});
	m_progress_thread = make_shared<BeesThread>("progress_report");
	m_progress_thread = make_shared<BeesThread>("progress_report");
	m_status_thread = make_shared<BeesThread>("status_report");
	m_progress_thread->exec([=]() {
		show_progress();
	});
	m_status_thread->exec([=]() {
		dump_status();
	});

	// Set up temporary file pool
	m_tmpfile_pool.generator([=]() -> shared_ptr<BeesTempFile> {
		return make_shared<BeesTempFile>(shared_from_this());
	});
	m_logical_ino_pool.generator([]() {
		const auto extent_ref_size = sizeof(uint64_t) * 3;
		return make_shared<BtrfsIoctlLogicalInoArgs>(0, BEES_MAX_EXTENT_REF_COUNT * extent_ref_size + sizeof(btrfs_data_container));
	});
	m_tmpfile_pool.checkin([](const shared_ptr<BeesTempFile> &btf) {
		catch_all([&](){
			btf->reset();
		});
	});

	// Force these to exist now so we don't have recursive locking
	// operations trying to access them
	fd_cache();
	hash_table();

	// Kick off the crawlers
	roots()->start();
}

void
BeesContext::stop()
{
	Timer stop_timer;
	BEESLOGNOTICE("Stopping bees...");

	// Stop TaskConsumers without hurting the Task objects that carry the Crawl state
	BEESNOTE("pausing work queue");
	BEESLOGDEBUG("Pausing work queue");
	TaskMaster::pause();

	// Stop crawlers first so we get good progress persisted on disk
	BEESNOTE("stopping crawlers and flushing crawl state");
	BEESLOGDEBUG("Stopping crawlers and flushing crawl state");
	if (m_roots) {
		m_roots->stop_request();
	} else {
		BEESLOGDEBUG("Crawlers not running");
	}

	BEESNOTE("stopping and flushing hash table");
	BEESLOGDEBUG("Stopping and flushing hash table");
	if (m_hash_table) {
		m_hash_table->stop_request();
	} else {
		BEESLOGDEBUG("Hash table not running");
	}

	// Wait for crawler writeback to finish
	BEESNOTE("waiting for crawlers to stop");
	BEESLOGDEBUG("Waiting for crawlers to stop");
	if (m_roots) {
		m_roots->stop_wait();
	}

	// It is now no longer possible to update progress in $BEESHOME,
	// so we can destroy Tasks with reckless abandon.
	BEESNOTE("setting stop_request flag");
	BEESLOGDEBUG("Setting stop_request flag");
	unique_lock<mutex> lock(m_stop_mutex);
	m_stop_requested = true;
	m_stop_condvar.notify_all();
	lock.unlock();

	// Wait for hash table flush to complete
	BEESNOTE("waiting for hash table flush to stop");
	BEESLOGDEBUG("waiting for hash table flush to stop");
	if (m_hash_table) {
		m_hash_table->stop_wait();
	}

	// Write status once with this message...
	BEESNOTE("stopping status thread at " << stop_timer << " sec");
	lock.lock();
	m_stop_condvar.notify_all();
	lock.unlock();

	// then wake the thread up one more time to exit the while loop
	BEESLOGDEBUG("Waiting for status thread");
	lock.lock();
	m_stop_status = true;
	m_stop_condvar.notify_all();
	lock.unlock();
	m_status_thread->join();

	BEESLOGNOTICE("bees stopped in " << stop_timer << " sec");

	// Skip all destructors, do not pass GO, do not collect atexit() functions
	_exit(EXIT_SUCCESS);
}

bool
BeesContext::stop_requested() const
{
	unique_lock<mutex> lock(m_stop_mutex);
	return m_stop_requested;
}

void
BeesContext::blacklist_insert(const BeesFileId &fid)
{
	BEESLOGDEBUG("Adding " << fid << " to blacklist");
	unique_lock<mutex> lock(m_blacklist_mutex);
	m_blacklist.insert(fid);
}

void
BeesContext::blacklist_erase(const BeesFileId &fid)
{
	BEESLOGDEBUG("Removing " << fid << " from blacklist");
	unique_lock<mutex> lock(m_blacklist_mutex);
	m_blacklist.erase(fid);
}

bool
BeesContext::is_blacklisted(const BeesFileId &fid) const
{
	// Everything on root 1 is blacklisted (it is mostly free space cache), no locks necessary.
	if (fid.root() == 1) {
		return true;
	}
	unique_lock<mutex> lock(m_blacklist_mutex);
	return m_blacklist.find(fid) != m_blacklist.end();
}

shared_ptr<BeesTempFile>
BeesContext::tmpfile()
{
	unique_lock<mutex> lock(m_stop_mutex);
	lock.unlock();
	return m_tmpfile_pool();
}

shared_ptr<BeesFdCache>
BeesContext::fd_cache()
{
	unique_lock<mutex> lock(m_stop_mutex);
	if (!m_fd_cache) {
		m_fd_cache = make_shared<BeesFdCache>(shared_from_this());
	}
	return m_fd_cache;
}

shared_ptr<BeesRoots>
BeesContext::roots()
{
	unique_lock<mutex> lock(m_stop_mutex);
	if (!m_roots) {
		m_roots = make_shared<BeesRoots>(shared_from_this());
	}
	return m_roots;
}

shared_ptr<BeesHashTable>
BeesContext::hash_table()
{
	unique_lock<mutex> lock(m_stop_mutex);
	if (!m_hash_table) {
		m_hash_table = make_shared<BeesHashTable>(shared_from_this(), "beeshash.dat");
	}
	return m_hash_table;
}

void
BeesContext::set_root_path(string path)
{
	BEESLOGINFO("set_root_path " << path);
	m_root_path = path;
	set_root_fd(open_or_die(m_root_path, FLAGS_OPEN_DIR));
}
