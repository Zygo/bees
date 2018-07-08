#include "bees.h"

#include "crucible/limits.h"
#include "crucible/string.h"
#include "crucible/task.h"

#include <fstream>
#include <iostream>
#include <vector>

using namespace crucible;
using namespace std;

static inline
const char *
getenv_or_die(const char *name)
{
	const char *rv = getenv(name);
	if (!rv) {
		THROW_ERROR(runtime_error, "Environment variable " << name << " not defined");
	}
	return rv;
}

BeesFdCache::BeesFdCache()
{
	m_root_cache.func([&](shared_ptr<BeesContext> ctx, uint64_t root) -> Fd {
		Timer open_timer;
		auto rv = ctx->roots()->open_root_nocache(root);
		BEESCOUNTADD(open_root_ms, open_timer.age() * 1000);
		return rv;
	});
	m_root_cache.max_size(BEES_ROOT_FD_CACHE_SIZE);
	m_file_cache.func([&](shared_ptr<BeesContext> ctx, uint64_t root, uint64_t ino) -> Fd {
		Timer open_timer;
		auto rv = ctx->roots()->open_root_ino_nocache(root, ino);
		BEESCOUNTADD(open_ino_ms, open_timer.age() * 1000);
		return rv;
	});
	m_file_cache.max_size(BEES_FILE_FD_CACHE_SIZE);
}

void
BeesFdCache::clear()
{
	BEESNOTE("Clearing root FD cache to enable subvol delete");
	m_root_cache.clear();
	BEESCOUNT(root_clear);
	BEESNOTE("Clearing open FD cache to enable file delete");
	m_file_cache.clear();
	BEESCOUNT(open_clear);
}

Fd
BeesFdCache::open_root(shared_ptr<BeesContext> ctx, uint64_t root)
{
	return m_root_cache(ctx, root);
}

Fd
BeesFdCache::open_root_ino(shared_ptr<BeesContext> ctx, uint64_t root, uint64_t ino)
{
	return m_file_cache(ctx, root, ino);
}

void
BeesFdCache::insert_root_ino(shared_ptr<BeesContext> ctx, Fd fd)
{
	BeesFileId fid(fd);
	return m_file_cache.insert(fd, ctx, fid.root(), fid.ino());
}

void
BeesContext::dump_status()
{
	auto status_charp = getenv("BEESSTATUS");
	if (!status_charp) return;
	string status_file(status_charp);
	BEESLOGINFO("Writing status to file '" << status_file << "' every " << BEES_STATUS_INTERVAL << " sec");
	while (1) {
		BEESNOTE("waiting " << BEES_STATUS_INTERVAL);
		sleep(BEES_STATUS_INTERVAL);

		BEESNOTE("writing status to file '" << status_file << "'");
		ofstream ofs(status_file + ".tmp");

		auto thisStats = BeesStats::s_global;
		ofs << "TOTAL:\n";
		ofs << "\t" << thisStats << "\n";
		auto avg_rates = thisStats / m_total_timer.age();
		ofs << "RATES:\n";
		ofs << "\t" << avg_rates << "\n";

		ofs << "THREADS (work queue " << TaskMaster::get_queue_count() << " tasks):\n";
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

		ofs.close();

		BEESNOTE("renaming status file '" << status_file << "'");
		rename((status_file + ".tmp").c_str(), status_file.c_str());
	}
}

void
BeesContext::show_progress()
{
	auto lastProgressStats = BeesStats::s_global;
	auto lastStats = lastProgressStats;
	Timer stats_timer;
	while (1) {
		sleep(BEES_PROGRESS_INTERVAL);

		if (stats_timer.age() > BEES_STATS_INTERVAL) {
			stats_timer.lap();

			auto thisStats = BeesStats::s_global;
			auto avg_rates = lastStats / BEES_STATS_INTERVAL;
			BEESLOGINFO("TOTAL: " << thisStats);
			BEESLOGINFO("RATES: " << avg_rates);
			lastStats = thisStats;
		}

		BEESLOGINFO("ACTIVITY:");

		auto thisStats = BeesStats::s_global;
		auto deltaStats = thisStats - lastProgressStats;
		if (deltaStats) {
			BEESLOGINFO("\t" << deltaStats / BEES_PROGRESS_INTERVAL);
		};
		lastProgressStats = thisStats;

		BEESLOGINFO("THREADS:");

		for (auto t : BeesNote::get_status()) {
			BEESLOGINFO("\ttid " << t.first << ": " << t.second);
		}
	}
}

Fd
BeesContext::home_fd()
{
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

BeesContext::BeesContext(shared_ptr<BeesContext> parent) :
	m_parent_ctx(parent)
{
	if (m_parent_ctx) {
		m_fd_cache = m_parent_ctx->fd_cache();
	}
}

bool
BeesContext::dedup(const BeesRangePair &brp)
{
	// TOOLONG and NOTE can retroactively fill in the filename details, but LOG can't
	BEESNOTE("dedup " << brp);

	brp.first.fd(shared_from_this());
	brp.second.fd(shared_from_this());

	BEESTOOLONG("dedup " << brp);

	BeesAddress first_addr(brp.first.fd(), brp.first.begin());
	BeesAddress second_addr(brp.second.fd(), brp.second.begin());

	BEESLOGINFO("dedup: src " << pretty(brp.first.size())  << " [" << to_hex(brp.first.begin())  << ".." << to_hex(brp.first.end())  << "] {" << first_addr  << "} " << name_fd(brp.first.fd()) << "\n"
		 << "       dst " << pretty(brp.second.size()) << " [" << to_hex(brp.second.begin()) << ".." << to_hex(brp.second.end()) << "] {" << second_addr << "} " << name_fd(brp.second.fd()));

	if (first_addr.get_physical_or_zero() == second_addr.get_physical_or_zero()) {
		BEESLOGTRACE("equal physical addresses in dedup");
		BEESCOUNT(bug_dedup_same_physical);
	}

	THROW_CHECK1(invalid_argument, brp, !brp.first.overlaps(brp.second));
	THROW_CHECK1(invalid_argument, brp, brp.first.size() == brp.second.size());

	BEESCOUNT(dedup_try);
	Timer dedup_timer;
	bool rv = btrfs_extent_same(brp.first.fd(), brp.first.begin(), brp.first.size(), brp.second.fd(), brp.second.begin());
	BEESCOUNTADD(dedup_ms, dedup_timer.age() * 1000);

	if (rv) {
		BEESCOUNT(dedup_hit);
		BEESCOUNTADD(dedup_bytes, brp.first.size());
		thread_local BeesFileRange last_src_bfr;
		if (!last_src_bfr.overlaps(brp.first)) {
			BEESCOUNTADD(dedup_unique_bytes, brp.first.size());
			last_src_bfr = brp.first;
		}
	} else {
		BEESCOUNT(dedup_miss);
		BEESLOGWARN("NO Dedup! " << brp);
	}

	return rv;
}

BeesRangePair
BeesContext::dup_extent(const BeesFileRange &src)
{
	BEESTRACE("dup_extent " << src);
	BEESCOUNTADD(dedup_copy, src.size());
	return BeesRangePair(tmpfile()->make_copy(src), src);
}

void
BeesContext::rewrite_file_range(const BeesFileRange &bfr)
{
	auto m_ctx = shared_from_this();
	BEESNOTE("Rewriting bfr " << bfr);
	BeesRangePair dup_brp(dup_extent(BeesFileRange(bfr.fd(), bfr.begin(), min(bfr.file_size(), bfr.end()))));
	// BEESLOG("\tdup_brp " << dup_brp);
	BeesBlockData orig_bbd(bfr.fd(), bfr.begin(), min(BLOCK_SIZE_SUMS, bfr.size()));
	// BEESLOG("\torig_bbd " << orig_bbd);
	BeesBlockData dup_bbd(dup_brp.first.fd(), dup_brp.first.begin(), min(BLOCK_SIZE_SUMS, dup_brp.first.size()));
	// BEESLOG("BeesResolver br(..., " << bfr << ")");
	BEESTRACE("BeesContext::rewrite_file_range calling BeesResolver " << bfr);
	BeesResolver br(m_ctx, BeesAddress(bfr.fd(), bfr.begin()));
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

BeesFileRange
BeesContext::scan_one_extent(const BeesFileRange &bfr, const Extent &e)
{
	BEESNOTE("Scanning " << pretty(e.size()) << " "
		<< to_hex(e.begin()) << ".." << to_hex(e.end())
		<< " " << name_fd(bfr.fd()) );
	BEESTRACE("scan extent " << e);
	BEESCOUNT(scan_extent);

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
		BEESLOGWARN("Interesting extent flags " << e << " from fd " << name_fd(bfr.fd()));
	}

	if (e.flags() & Extent::HOLE) {
		// Nothing here, dispose of this early
		BEESCOUNT(scan_hole);
		return bfr;
	}

	if (e.flags() & Extent::PREALLOC) {
		// Prealloc is all zero and we replace it with a hole.
		// No special handling is required here.  Nuke it and move on.
		BEESLOGINFO("prealloc extent " << e);
		// Must not extend past EOF
		auto extent_size = min(e.end(), bfr.file_size()) - e.begin();
		BeesFileRange prealloc_bfr(m_ctx->tmpfile()->make_hole(extent_size));
		BeesRangePair brp(prealloc_bfr, bfr);
		// Raw dedup here - nothing else to do with this extent, nothing to merge with
		if (m_ctx->dedup(brp)) {
			BEESCOUNT(dedup_prealloc_hit);
			BEESCOUNTADD(dedup_prealloc_bytes, e.size());
			return bfr;
		} else {
			BEESCOUNT(dedup_prealloc_miss);
		}
	}

	// OK we need to read extent now
	readahead(bfr.fd(), bfr.begin(), bfr.size());

	map<off_t, pair<BeesHash, BeesAddress>> insert_map;
	set<off_t> noinsert_set;

	// Hole handling
	bool extent_compressed = e.flags() & FIEMAP_EXTENT_ENCODED;
	bool extent_contains_zero = false;
	bool extent_contains_nonzero = false;

	// Need to replace extent
	bool rewrite_extent = false;

	// Pretty graphs
	off_t block_count = ((e.size() + BLOCK_MASK_SUMS) & ~BLOCK_MASK_SUMS) / BLOCK_SIZE_SUMS;
	BEESTRACE(e << " block_count " << block_count);
	string bar(block_count, '#');

	for (off_t next_p = e.begin(); next_p < e.end(); ) {

		// Guarantee forward progress
		off_t p = next_p;
		next_p += BLOCK_SIZE_SUMS;

		off_t bar_p = (p - e.begin()) / BLOCK_SIZE_SUMS;
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
		BeesHash hash = bbd.hash();

		// Schedule this block for insertion if we decide to keep this extent.
		BEESCOUNT(scan_hash_preinsert);
		BEESTRACE("Pushing hash " << hash << " addr " << addr << " bbd " << bbd);
		insert_map.insert(make_pair(p, make_pair(hash, addr)));
		bar.at(bar_p) = 'R';

		// Weed out zero blocks
		BEESNOTE("is_data_zero " << bbd);
		bool extent_is_zero = bbd.is_data_zero();
		if (extent_is_zero) {
			bar.at(bar_p) = '0';
			if (extent_compressed) {
				if (!extent_contains_zero) {
					// BEESLOG("compressed zero bbd " << bbd << "\n\tin extent " << e);
				}
				extent_contains_zero = true;
				// Do not attempt to lookup hash of zero block
				continue;
			} else {
				BEESLOGINFO("zero bbd " << bbd << "\n\tin extent " << e);
				BEESCOUNT(scan_zero_uncompressed);
				rewrite_extent = true;
				break;
			}
		} else {
			if (extent_contains_zero && !extent_contains_nonzero) {
				// BEESLOG("compressed nonzero bbd " << bbd << "\n\tin extent " << e);
			}
			extent_contains_nonzero = true;
		}

		BEESNOTE("lookup hash " << bbd);
		auto found = hash_table->find_cell(hash);
		BEESCOUNT(scan_lookup);

		set<BeesResolver> resolved_addrs;
		set<BeesAddress> found_addrs;

		// We know that there is at least one copy of the data and where it is,
		// but we don't want to do expensive LOGICAL_INO operations unless there
		// are at least two distinct addresses to look at.
		found_addrs.insert(addr);

		for (auto i : found) {
			BEESTRACE("found (hash, address): " << i);
			BEESCOUNT(scan_found);

			// Hash has to match
			THROW_CHECK2(runtime_error, i.e_hash, hash, i.e_hash == hash);

			BeesAddress found_addr(i.e_addr);

#if 0
			// If address already in hash table, move on to next extent.
			// We've already seen this block and may have made additional references to it.
			// The current extent is effectively "pinned" and can't be modified any more.
			if (found_addr.get_physical_or_zero() == addr.get_physical_or_zero()) {
				BEESCOUNT(scan_already);
				return bfr;
			}
#endif

			// Block must have matching EOF alignment
			if (found_addr.is_unaligned_eof() != addr.is_unaligned_eof()) {
				BEESCOUNT(scan_malign);
				continue;
			}

			// Address is a duplicate
			if (!found_addrs.insert(found_addr).second) {
				BEESCOUNT(scan_twice);
				continue;
			}

			// Hash is toxic
			if (found_addr.is_toxic()) {
				BEESLOGWARN("WORKAROUND: abandoned toxic match for hash " << hash << " addr " << found_addr << " matching bbd " << bbd);
				// Don't push these back in because we'll never delete them.
				// Extents may become non-toxic so give them a chance to expire.
				// hash_table->push_front_hash_addr(hash, found_addr);
				BEESCOUNT(scan_toxic_hash);
				return bfr;
			}

			// Distinct address, go resolve it
			bool abandon_extent = false;
			catch_all([&]() {
				BEESNOTE("resolving " << found_addr << " matched " << bbd);
				BEESTRACE("resolving " << found_addr << " matched " << bbd);
				BEESTRACE("BeesContext::scan_one_extent calling BeesResolver " << found_addr);
				BeesResolver resolved(m_ctx, found_addr);
				// Toxic extents are really toxic
				if (resolved.is_toxic()) {
					BEESLOGWARN("WORKAROUND: discovered toxic match at found_addr " << found_addr << " matching bbd " << bbd);
					BEESCOUNT(scan_toxic_match);
					// Make sure we never see this hash again.
					// It has become toxic since it was inserted into the hash table.
					found_addr.set_toxic();
					hash_table->push_front_hash_addr(hash, found_addr);
					abandon_extent = true;
				} else if (!resolved.count()) {
					BEESCOUNT(scan_resolve_zero);
					// Didn't find anything, address is dead
					BEESTRACE("matched hash " << hash << " addr " << addr << " count zero");
					hash_table->erase_hash_addr(hash, found_addr);
				} else {
					resolved_addrs.insert(resolved);
					BEESCOUNT(scan_resolve_hit);
				}
			});

			if (abandon_extent) {
				return bfr;
			}
		}

		// This shouldn't happen (often), so let's count it separately
		if (resolved_addrs.size() > 2) {
			BEESCOUNT(matched_3_or_more);
		}
		if (resolved_addrs.size() > 1) {
			BEESCOUNT(matched_2_or_more);
		}

		// No need to do all this unless there are two or more distinct matches
		if (!resolved_addrs.empty()) {
			bar.at(bar_p) = 'M';
			BEESCOUNT(matched_1_or_more);
			BEESTRACE("resolved_addrs.size() = " << resolved_addrs.size());
			BEESNOTE("resolving " << resolved_addrs.size() << " matches for hash " << hash);

			BeesFileRange replaced_bfr;

			BeesAddress last_replaced_addr;
			for (auto it = resolved_addrs.begin(); it != resolved_addrs.end(); ++it) {
				// FIXME:  Need to terminate this loop on replace_dst exception condition
				// catch_all([&]() {
					auto it_copy = *it;
					BEESNOTE("finding one match (out of " << it_copy.count() << ") at " << it_copy.addr() << " for " << bbd);
					BEESTRACE("finding one match (out of " << it_copy.count() << ") at " << it_copy.addr() << " for " << bbd);
					replaced_bfr = it_copy.replace_dst(bbd);
					BEESTRACE("next_p " << to_hex(next_p) << " -> replaced_bfr " << replaced_bfr);

					// If we didn't find this hash where the hash table said it would be,
					// correct the hash table.
					if (it_copy.found_hash()) {
						BEESCOUNT(scan_hash_hit);
					} else {
						// BEESLOGDEBUG("erase src hash " << hash << " addr " << it_copy.addr());
						BEESCOUNT(scan_hash_miss);
						hash_table->erase_hash_addr(hash, it_copy.addr());
					}

					if (it_copy.found_dup()) {
						BEESCOUNT(scan_dup_hit);

						// FIXME:  we will thrash if we let multiple references to identical blocks
						// exist in the hash table.  Erase all but the last one.
						if (last_replaced_addr) {
							BEESLOGINFO("Erasing redundant hash " << hash << " addr " << last_replaced_addr);
							hash_table->erase_hash_addr(hash, last_replaced_addr);
							BEESCOUNT(scan_erase_redundant);
						}
						last_replaced_addr = it_copy.addr();

						// Invalidate resolve cache so we can count refs correctly
						m_ctx->invalidate_addr(it_copy.addr());
						m_ctx->invalidate_addr(bbd.addr());

						// Remove deduped blocks from insert map
						THROW_CHECK0(runtime_error, replaced_bfr);
						for (off_t ip = replaced_bfr.begin(); ip < replaced_bfr.end(); ip += BLOCK_SIZE_SUMS) {
							BEESCOUNT(scan_dup_block);
							noinsert_set.insert(ip);
							if (ip >= e.begin() && ip < e.end()) {
								off_t bar_p = (ip - e.begin()) / BLOCK_SIZE_SUMS;
								bar.at(bar_p) = 'd';
							}
						}

						// next_p may be past EOF so check p only
						THROW_CHECK2(runtime_error, p, replaced_bfr, p < replaced_bfr.end());

						BEESCOUNT(scan_bump);
						next_p = replaced_bfr.end();
					} else {
						BEESCOUNT(scan_dup_miss);
					}
				// });
			}
			if (last_replaced_addr) {
				// If we replaced extents containing the incoming addr,
				// push the addr we kept to the front of the hash LRU.
				hash_table->push_front_hash_addr(hash, last_replaced_addr);
				BEESCOUNT(scan_push_front);
			}
		} else {
			BEESCOUNT(matched_0);
		}
	}

	// If the extent was compressed and all zeros, nuke entire thing
	if (!rewrite_extent && (extent_contains_zero && !extent_contains_nonzero)) {
		rewrite_extent = true;
		BEESCOUNT(scan_zero_compressed);
	}

	// Turning this off because it's a waste of time on small extents
	// and it's incorrect for large extents.
#if 0
	// If the extent contains obscured blocks, and we can find no
	// other refs to the extent that reveal those blocks, nuke the incoming extent.
	// Don't rewrite extents that are bigger than the maximum FILE_EXTENT_SAME size
	// because we can't make extents that large with dedup.
	// Don't rewrite small extents because it is a waste of time without being
	// able to combine them into bigger extents.
	if (!rewrite_extent && (e.flags() & Extent::OBSCURED) && (e.physical_len() > BLOCK_SIZE_MAX_COMPRESSED_EXTENT) && (e.physical_len() < BLOCK_SIZE_MAX_EXTENT_SAME)) {
		BEESCOUNT(scan_obscured);
		BEESNOTE("obscured extent " << e);
		// We have to map all the source blocks to see if any of them
		// (or all of them aggregated) provide a path through the FS to the blocks
		BeesResolver br(m_ctx, BeesAddress(e, e.begin()));
		BeesBlockData ref_bbd(bfr.fd(), bfr.begin(), min(BLOCK_SIZE_SUMS, bfr.size()));
		// BEESLOG("ref_bbd " << ref_bbd);
		auto bfr_set = br.find_all_matches(ref_bbd);
		bool non_obscured_extent_found = false;
		set<off_t> blocks_to_find;
		for (off_t j = 0; j < e.physical_len(); j += BLOCK_SIZE_CLONE) {
			blocks_to_find.insert(j);
		}
		// Don't bother if saving less than 1%
		auto maximum_hidden_count = blocks_to_find.size() / 100;
		for (auto i : bfr_set) {
			BtrfsExtentWalker ref_ew(bfr.fd(), bfr.begin(), m_ctx->root_fd());
			Extent ref_e = ref_ew.current();
			// BEESLOG("\tref_e " << ref_e);
			THROW_CHECK2(out_of_range, ref_e, e, ref_e.offset() + ref_e.logical_len() <= e.physical_len());
			for (off_t j = ref_e.offset(); j < ref_e.offset() + ref_e.logical_len(); j += BLOCK_SIZE_CLONE) {
				blocks_to_find.erase(j);
			}
			if (blocks_to_find.size() <= maximum_hidden_count) {
				BEESCOUNT(scan_obscured_miss);
				BEESLOG("Found references to all but " << blocks_to_find.size() << " blocks");
				non_obscured_extent_found = true;
				break;
			} else {
				BEESCOUNT(scan_obscured_hit);
				// BEESLOG("blocks_to_find: " << blocks_to_find.size() << " from " << *blocks_to_find.begin() << ".." << *blocks_to_find.rbegin());
			}
		}
		if (!non_obscured_extent_found) {
			// BEESLOG("No non-obscured extents found");
			rewrite_extent = true;
			BEESCOUNT(scan_obscured_rewrite);
		}
	}
#endif

	// If we deduped any blocks then we must rewrite the remainder of the extent
	if (!noinsert_set.empty()) {
		rewrite_extent = true;
	}

	// If we need to replace part of the extent, rewrite all instances of it
	if (rewrite_extent) {
		bool blocks_rewritten = false;
		BEESTRACE("Rewriting extent " << e);
		off_t last_p = e.begin();
		off_t p = last_p;
		off_t next_p;
		BEESTRACE("next_p " << to_hex(next_p) << " p " << to_hex(p) << " last_p " << to_hex(last_p));
		for (next_p = e.begin(); next_p < e.end(); ) {
			p = next_p;
			next_p += BLOCK_SIZE_SUMS;

			// BEESLOG("noinsert_set.count(" << to_hex(p) << ") " << noinsert_set.count(p));
			if (noinsert_set.count(p)) {
				if (p - last_p > 0) {
					rewrite_file_range(BeesFileRange(bfr.fd(), last_p, p));
					blocks_rewritten = true;
				}
				last_p = next_p;
			} else {
				off_t bar_p = (p - e.begin()) / BLOCK_SIZE_SUMS;
				bar.at(bar_p) = '+';
			}
		}
		BEESTRACE("last");
		if (next_p - last_p > 0) {
			rewrite_file_range(BeesFileRange(bfr.fd(), last_p, next_p));
			blocks_rewritten = true;
		}
		if (blocks_rewritten) {
			// Nothing left to insert, all blocks clobbered
			insert_map.clear();
		} else {
			// BEESLOG("No blocks rewritten");
			BEESCOUNT(scan_no_rewrite);
		}
	}

	// We did not rewrite the extent and it contained data, so insert it.
	for (auto i : insert_map) {
		off_t bar_p = (i.first - e.begin()) / BLOCK_SIZE_SUMS;
		BEESTRACE("e " << e << "bar_p = " << bar_p << " i.first-e.begin() " << i.first - e.begin() << " i.second " << i.second.first << ", " << i.second.second);
		if (noinsert_set.count(i.first)) {
			// FIXME:  we removed one reference to this copy.  Avoid thrashing?
			hash_table->erase_hash_addr(i.second.first, i.second.second);
			// Block was clobbered, do not insert
			// Will look like 'Ddddd' because we skip deduped blocks
			bar.at(bar_p) = 'D';
			BEESCOUNT(inserted_clobbered);
		} else {
			hash_table->push_random_hash_addr(i.second.first, i.second.second);
			bar.at(bar_p) = '.';
			BEESCOUNT(inserted_block);
		}
	}

	// Visualize
	if (bar != string(block_count, '.')) {
		BEESLOGINFO("scan: " << pretty(e.size()) << " " << to_hex(e.begin()) << " [" << bar << "] " << to_hex(e.end()) << ' ' << name_fd(bfr.fd()));
	}

	return bfr;
}

BeesFileRange
BeesContext::scan_forward(const BeesFileRange &bfr)
{
	// What are we doing here?
	BEESTRACE("scan_forward " << bfr);
	BEESCOUNT(scan_forward);

	Timer scan_timer;

	// Silently filter out blacklisted files
	if (is_blacklisted(bfr.fid())) {
		BEESCOUNT(scan_blacklisted);
		return bfr;
	}

	BEESNOTE("scan open " << bfr);

	// Reconstitute FD
	bfr.fd(shared_from_this());

	BEESNOTE("scan extent " << bfr);

	// No FD?  Well, that was quick.
	if (!bfr.fd()) {
		// BEESLOGINFO("No FD in " << root_path() << " for " << bfr);
		BEESCOUNT(scan_no_fd);
		return bfr;
	}

	// Sanity check
	if (bfr.begin() >= bfr.file_size()) {
		BEESLOGWARN("past EOF: " << bfr);
		BEESCOUNT(scan_eof);
		return bfr;
	}

	BtrfsExtentWalker ew(bfr.fd(), bfr.begin(), root_fd());

	BeesFileRange return_bfr(bfr);

	Extent e;
	catch_all([&]() {
		while (true) {
			e = ew.current();

			catch_all([&]() {
				uint64_t extent_bytenr = e.bytenr();
				BEESNOTE("waiting for extent bytenr " << to_hex(extent_bytenr));
				auto extent_lock = m_extent_lock_set.make_lock(extent_bytenr);
				Timer one_extent_timer;
				return_bfr = scan_one_extent(bfr, e);
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

	return return_bfr;
}

BeesResolveAddrResult::BeesResolveAddrResult()
{
}

BeesResolveAddrResult
BeesContext::resolve_addr_uncached(BeesAddress addr)
{
	THROW_CHECK1(invalid_argument, addr, !addr.is_magic());
	THROW_CHECK0(invalid_argument, !!root_fd());
	Timer resolve_timer;

	// There is no performance benefit if we restrict the buffer size.
        BtrfsIoctlLogicalInoArgs log_ino(addr.get_physical_or_zero());

	{
		BEESTOOLONG("Resolving addr " << addr << " in " << root_path() << " refs " << log_ino.m_iors.size());
		if (log_ino.do_ioctl_nothrow(root_fd())) {
			BEESCOUNT(resolve_ok);
		} else {
			BEESCOUNT(resolve_fail);
		}
		BEESCOUNTADD(resolve_ms, resolve_timer.age() * 1000);
	}

	// Prevent unavoidable performance bug from crippling the rest of the system
	auto rt_age = resolve_timer.age();

	// Avoid performance bug
	BeesResolveAddrResult rv;
	rv.m_biors = log_ino.m_iors;
	if (rt_age < BEES_TOXIC_DURATION && log_ino.m_iors.size() < BEES_MAX_EXTENT_REF_COUNT) {
		rv.m_is_toxic = false;
	} else {
		BEESLOGWARN("WORKAROUND: toxic address " << addr << " in " << root_path() << " with " << log_ino.m_iors.size() << " refs took " << rt_age << "s in LOGICAL_INO");
		BEESCOUNT(resolve_toxic);
		rv.m_is_toxic = true;
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
BeesContext::set_root_fd(Fd fd)
{
	uint64_t root_fd_treeid = btrfs_get_root_id(fd);
	BEESLOGINFO("set_root_fd " << name_fd(fd));
	BEESTRACE("set_root_fd " << name_fd(fd));
	THROW_CHECK1(invalid_argument, root_fd_treeid, root_fd_treeid == BTRFS_FS_TREE_OBJECTID);
	Stat st(fd);
	THROW_CHECK1(invalid_argument, st.st_ino, st.st_ino == BTRFS_FIRST_FREE_OBJECTID);
	m_root_fd = fd;
	BtrfsIoctlFsInfoArgs fsinfo;
	fsinfo.do_ioctl(fd);
	m_root_uuid = fsinfo.uuid();
	BEESLOGINFO("Filesystem UUID is " << m_root_uuid);

	// 65536 is big enough for two max-sized extents.
	// Need enough total space in the cache for the maximum number of active threads.
	m_resolve_cache.max_size(65536);
	m_resolve_cache.func([&](BeesAddress addr) -> BeesResolveAddrResult {
		return resolve_addr_uncached(addr);
	});

	// Start queue producers
	roots();

	BEESLOGINFO("returning from set_root_fd in " << name_fd(fd));
}

void
BeesContext::blacklist_add(const BeesFileId &fid)
{
	BEESLOGDEBUG("Adding " << fid << " to blacklist");
	unique_lock<mutex> lock(m_blacklist_mutex);
	m_blacklist.insert(fid);
}

bool
BeesContext::is_blacklisted(const BeesFileId &fid) const
{
	// Everything on root 1 is blacklisted, no locks necessary.
	if (fid.root() == 1) {
		return true;
	}
	unique_lock<mutex> lock(m_blacklist_mutex);
	return m_blacklist.count(fid);
}

shared_ptr<BeesTempFile>
BeesContext::tmpfile()
{
	// There need be only one, this is not a high-contention path
	static mutex s_mutex;
	unique_lock<mutex> lock(s_mutex);

	if (!m_tmpfiles[this_thread::get_id()]) {
		m_tmpfiles[this_thread::get_id()] = make_shared<BeesTempFile>(shared_from_this());
	}
	auto rv = m_tmpfiles[this_thread::get_id()];
	return rv;
}

shared_ptr<BeesFdCache>
BeesContext::fd_cache()
{
	static mutex s_mutex;
	unique_lock<mutex> lock(s_mutex);
	if (!m_fd_cache) {
		m_fd_cache = make_shared<BeesFdCache>();
	}
	auto rv = m_fd_cache;
	return rv;
}

shared_ptr<BeesRoots>
BeesContext::roots()
{
	static mutex s_mutex;
	unique_lock<mutex> lock(s_mutex);
	if (!m_roots) {
		m_roots = make_shared<BeesRoots>(shared_from_this());
	}
	auto rv = m_roots;
	return rv;
}

shared_ptr<BeesHashTable>
BeesContext::hash_table()
{
	static mutex s_mutex;
	unique_lock<mutex> lock(s_mutex);
	if (!m_hash_table) {
		m_hash_table = make_shared<BeesHashTable>(shared_from_this(), "beeshash.dat");
	}
	auto rv = m_hash_table;
	return rv;
}

void
BeesContext::set_root_path(string path)
{
	BEESLOGINFO("set_root_path " << path);
	m_root_path = path;
	set_root_fd(open_or_die(m_root_path, FLAGS_OPEN_DIR));
}

void
BeesContext::insert_root_ino(Fd fd)
{
	fd_cache()->insert_root_ino(shared_from_this(), fd);
}
