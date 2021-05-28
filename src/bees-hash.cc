#include "bees.h"

#include "crucible/city.h"
#include "crucible/crc64.h"
#include "crucible/string.h"
#include "crucible/uname.h"

#include <algorithm>

#include <sys/mman.h>

using namespace crucible;
using namespace std;

BeesHash::BeesHash(const uint8_t *ptr, size_t len) :
	// m_hash(CityHash64(reinterpret_cast<const char *>(ptr), len))
	m_hash(Digest::CRC::crc64(ptr, len))
{
}

ostream &
operator<<(ostream &os, const BeesHash &bh)
{
	return os << to_hex(BeesHash::Type(bh));
}

ostream &
operator<<(ostream &os, const BeesHashTable::Cell &bhte)
{
	return os << "BeesHashTable::Cell { hash = " << BeesHash(bhte.e_hash) << ", addr = "
		  << BeesAddress(bhte.e_addr) << " }";
}

#if 0
static
void
dump_bucket_locked(BeesHashTable::Cell *p, BeesHashTable::Cell *q)
{
	for (auto i = p; i < q; ++i) {
		BEESLOG("Entry " << i - p << " " << *i);
	}
}
#endif

static const bool VERIFY_CLEARS_BUGS = false;

bool
verify_cell_range(BeesHashTable::Cell *p, BeesHashTable::Cell *q, bool clear_bugs = VERIFY_CLEARS_BUGS)
{
	// Must be called while holding m_bucket_mutex
	bool bugs_found = false;
	set<BeesHashTable::Cell> seen_it;
	for (BeesHashTable::Cell *cell = p; cell < q; ++cell) {
		if (cell->e_addr && cell->e_addr < 0x1000) {
			BEESCOUNT(bug_hash_magic_addr);
			BEESLOGDEBUG("Bad hash table address hash " << to_hex(cell->e_hash) << " addr " << to_hex(cell->e_addr));
			if (clear_bugs) {
				cell->e_addr = 0;
				cell->e_hash = 0;
			}
			bugs_found = true;
		}
		if (cell->e_addr && !seen_it.insert(*cell).second) {
			BEESCOUNT(bug_hash_duplicate_cell);
			// BEESLOGDEBUG("Duplicate hash table entry:\nthis = " << *cell << "\nold = " << *seen_it.find(*cell));
			BEESLOGDEBUG("Duplicate hash table entry: " << *cell);
			if (clear_bugs) {
				cell->e_addr = 0;
				cell->e_hash = 0;
			}
			bugs_found = true;
		}
	}
	return bugs_found;
}

pair<BeesHashTable::Cell *, BeesHashTable::Cell *>
BeesHashTable::get_cell_range(HashType hash)
{
	THROW_CHECK1(runtime_error, m_buckets, m_buckets > 0);
	THROW_CHECK1(runtime_error, m_bucket_ptr, m_bucket_ptr != nullptr);
	Bucket *pp = &m_bucket_ptr[hash % m_buckets];
	Cell *bp = pp[0].p_cells;
	Cell *ep = pp[1].p_cells;
	THROW_CHECK2(out_of_range, m_cell_ptr,     bp, bp >= m_cell_ptr);
	THROW_CHECK2(out_of_range, m_cell_ptr_end, ep, ep <= m_cell_ptr_end);
	return make_pair(bp, ep);
}

pair<uint8_t *, uint8_t *>
BeesHashTable::get_extent_range(HashType hash)
{
	THROW_CHECK1(runtime_error, m_buckets, m_buckets > 0);
	THROW_CHECK1(runtime_error, m_bucket_ptr, m_bucket_ptr != nullptr);
	Extent *iop = &m_extent_ptr[ (hash % m_buckets) / c_buckets_per_extent ];
	uint8_t *bp = iop[0].p_byte;
	uint8_t *ep = iop[1].p_byte;
	THROW_CHECK2(out_of_range, m_byte_ptr,     bp, bp >= m_byte_ptr);
	THROW_CHECK2(out_of_range, m_byte_ptr_end, ep, ep <= m_byte_ptr_end);
	return make_pair(bp, ep);
}

bool
BeesHashTable::flush_dirty_extent(uint64_t extent_index)
{
	BEESNOTE("flushing extent #" << extent_index << " of " << m_extents << " extents");

	auto lock = lock_extent_by_index(extent_index);
	bool wrote_extent = false;

	catch_all([&]() {
		uint8_t *const dirty_extent      = m_extent_ptr[extent_index].p_byte;
		uint8_t *const dirty_extent_end  = m_extent_ptr[extent_index + 1].p_byte;
		const size_t dirty_extent_offset = dirty_extent - m_byte_ptr;
		THROW_CHECK1(out_of_range, dirty_extent,     dirty_extent     >= m_byte_ptr);
		THROW_CHECK1(out_of_range, dirty_extent_end, dirty_extent_end <= m_byte_ptr_end);
		THROW_CHECK2(out_of_range, dirty_extent_end, dirty_extent, dirty_extent_end - dirty_extent == BLOCK_SIZE_HASHTAB_EXTENT);
		BEESTOOLONG("pwrite(fd " << m_fd << " '" << name_fd(m_fd)<< "', length " << to_hex(dirty_extent_end - dirty_extent) << ", offset " << to_hex(dirty_extent - m_byte_ptr) << ")");
		// Copy the extent because we might be stuck writing for a while
		ByteVector extent_copy(dirty_extent, dirty_extent_end);

		// Release the lock
		lock.unlock();

		// Write the extent (or not)
		pwrite_or_die(m_fd, extent_copy, dirty_extent_offset);
		BEESCOUNT(hash_extent_out);

		// Nope, this causes a _dramatic_ loss of performance.
		// const size_t dirty_extent_size   = dirty_extent_end - dirty_extent;
		// bees_unreadahead(m_fd, dirty_extent_offset, dirty_extent_size);

		// Mark extent clean if write was successful
		lock.lock();
		m_extent_metadata.at(extent_index).m_dirty = false;

		wrote_extent = true;
	});

	return wrote_extent;
}

size_t
BeesHashTable::flush_dirty_extents(bool slowly)
{
	THROW_CHECK1(runtime_error, m_buckets, m_buckets > 0);

	uint64_t wrote_extents = 0;
	for (size_t extent_index = 0; extent_index < m_extents; ++extent_index) {
		// Skip the clean ones
		auto lock = lock_extent_by_index(extent_index);
		if (!m_extent_metadata.at(extent_index).m_dirty) {
			continue;
		}
		lock.unlock();

		if (flush_dirty_extent(extent_index)) {
			++wrote_extents;
			if (slowly) {
				if (m_stop_requested) {
					slowly = false;
					continue;
				}
				BEESNOTE("flush rate limited after extent #" << extent_index << " of " << m_extents << " extents");
				chrono::duration<double> sleep_time(m_flush_rate_limit.sleep_time(BLOCK_SIZE_HASHTAB_EXTENT));
				unique_lock<mutex> lock(m_stop_mutex);
				m_stop_condvar.wait_for(lock, sleep_time);
			}
		}
	}
	BEESLOGINFO("Flushed " << wrote_extents << " of " << m_extents << " hash table extents");
	return wrote_extents;
}

void
BeesHashTable::set_extent_dirty_locked(uint64_t extent_index)
{
	// Must already be locked
	m_extent_metadata.at(extent_index).m_dirty = true;

	// Signal writeback thread
	unique_lock<mutex> dirty_lock(m_dirty_mutex);
	m_dirty = true;
	m_dirty_condvar.notify_one();
}

void
BeesHashTable::writeback_loop()
{
	while (!m_stop_requested) {
		auto wrote_extents = flush_dirty_extents(true);

		BEESNOTE("idle after writing " << wrote_extents << " of " << m_extents << " extents");

		unique_lock<mutex> lock(m_dirty_mutex);
		if (m_stop_requested) {
			break;
		}
		if (m_dirty) {
			m_dirty = false;
		} else {
			m_dirty_condvar.wait(lock);
		}
	}

	// The normal loop exits at the end of one iteration when stop requested,
	// but stop request will be in the middle of the loop, and some extents
	// will still be dirty.  Run the flush loop again to get those.
	BEESNOTE("flushing hash table, round 2");
	BEESLOGDEBUG("Flushing hash table");
	flush_dirty_extents(false);

	// If there were any Tasks still running, they may have updated
	// some hash table pages during the second flush.  These updates
	// will be lost.  The Tasks will be repeated on the next run because
	// they were not completed prior to the stop request, and the
	// Crawl progress was already flushed out before the Hash table
	// started writing, so nothing is really lost here.

	catch_all([&]() {
		// trigger writeback on our way out
#if 0
		// seems to trigger huge latency spikes
		BEESTOOLONG("unreadahead hash table size " <<
		pretty(m_size)); bees_unreadahead(m_fd, 0, m_size);
#endif
	});
	BEESLOGDEBUG("Exited hash table writeback_loop");
}

static
string
percent(size_t num, size_t den)
{
	if (den) {
		return astringprintf("%u%%", num * 100 / den);
	} else {
		return "--%";
	}
}

void
BeesHashTable::prefetch_loop()
{
	Uname uname;
	bool not_locked = true;
	while (!m_stop_requested) {
		size_t width = 64;
		vector<size_t> occupancy(width, 0);
		size_t occupied_count = 0;
		size_t total_count = 0;
		size_t compressed_count = 0;
		size_t compressed_offset_count = 0;
		size_t toxic_count = 0;
		size_t unaligned_eof_count = 0;

		m_prefetch_running = true;
		for (uint64_t ext = 0; ext < m_extents && !m_stop_requested; ++ext) {
			BEESNOTE("prefetching hash table extent #" << ext << " of " << m_extents);
			catch_all([&]() {
				fetch_missing_extent_by_index(ext);

				BEESNOTE("analyzing hash table extent #" << ext << " of " << m_extents);
				bool duplicate_bugs_found = false;
				auto lock = lock_extent_by_index(ext);
				for (Bucket *bucket = m_extent_ptr[ext].p_buckets; bucket < m_extent_ptr[ext + 1].p_buckets; ++bucket) {
					if (verify_cell_range(bucket[0].p_cells, bucket[1].p_cells)) {
						duplicate_bugs_found = true;
					}
					size_t this_bucket_occupied_count = 0;
					for (Cell *cell = bucket[0].p_cells; cell < bucket[1].p_cells; ++cell) {
						if (cell->e_addr) {
							++this_bucket_occupied_count;
							BeesAddress a(cell->e_addr);
							if (a.is_compressed()) {
								++compressed_count;
								if (a.has_compressed_offset()) {
									++compressed_offset_count;
								}
							}
							if (a.is_toxic()) {
								++toxic_count;
							}
							if (a.is_unaligned_eof()) {
								++unaligned_eof_count;
							}
						}
						++total_count;
					}
					++occupancy.at(this_bucket_occupied_count * width / (1 + c_cells_per_bucket) );
					// Count these instead of calculating the number so we get better stats in case of exceptions
					occupied_count += this_bucket_occupied_count;
				}
				if (duplicate_bugs_found) {
					set_extent_dirty_locked(ext);
				}
			});
		}
		m_prefetch_running = false;

		BEESNOTE("calculating hash table statistics");

		vector<string> histogram;
		vector<size_t> thresholds;
		size_t threshold = 1;
		bool threshold_exceeded = false;
		do {
			threshold_exceeded = false;
			histogram.push_back(string(width, ' '));
			thresholds.push_back(threshold);
			for (size_t x = 0; x < width; ++x) {
				if (occupancy.at(x) >= threshold) {
					histogram.back().at(x) = '#';
					threshold_exceeded = true;
				}
			}
			threshold *= 2;
		} while (threshold_exceeded);

		ostringstream out;
		size_t count = histogram.size();
		bool first_line = true;
		for (auto it = histogram.rbegin(); it != histogram.rend(); ++it) {
			out << *it << " " << thresholds.at(--count);
			if (first_line) {
				first_line = false;
				out << " pages";
			}
			out << "\n";
		}

		size_t uncompressed_count = occupied_count - compressed_offset_count;

		ostringstream graph_blob;

		graph_blob << "Now:     " << format_time(time(NULL)) << "\n";
		graph_blob << "Uptime:  " << m_ctx->total_timer().age() << " seconds\n";
		graph_blob << "Version: " << BEES_VERSION << "\n";
		graph_blob << "Kernel:  " << uname.sysname << " " << uname.release << " " << uname.machine << " " << uname.version << "\n";

		graph_blob
			<< "\nHash table page occupancy histogram (" << occupied_count << "/" << total_count << " cells occupied, " << (occupied_count * 100 / total_count) << "%)\n"
			<< out.str() << "0%      |      25%      |      50%      |      75%      |   100% page fill\n"
			<< "compressed " << compressed_count << " (" << percent(compressed_count, occupied_count) << ")\n"
			<< "uncompressed " << uncompressed_count << " (" << percent(uncompressed_count, occupied_count) << ")"
			<< " unaligned_eof " << unaligned_eof_count << " (" << percent(unaligned_eof_count, occupied_count) << ")"
			<< " toxic " << toxic_count << " (" << percent(toxic_count, occupied_count) << ")";

		graph_blob << "\n\n";

		graph_blob << "TOTAL:\n";
		auto thisStats = BeesStats::s_global;
		graph_blob << "\t" << thisStats << "\n";

		graph_blob << "\nRATES:\n";
		auto avg_rates = thisStats / m_ctx->total_timer().age();
		graph_blob << "\t" << avg_rates << "\n";

		graph_blob << m_ctx->get_progress();

		BEESLOGINFO(graph_blob.str());
		catch_all([&]() {
			m_stats_file.write(graph_blob.str());
		});

		if (not_locked && !m_stop_requested) {
			// Always do the mlock, whether shared or not
			THROW_CHECK1(runtime_error, m_size, m_size > 0);
			BEESLOGINFO("mlock(" << pretty(m_size) << ")...");
			Timer lock_time;
			catch_all([&]() {
				BEESNOTE("mlock " << pretty(m_size));
				DIE_IF_NON_ZERO(mlock(m_byte_ptr, m_size));
			});
			BEESLOGINFO("mlock(" << pretty(m_size) << ") done in " << lock_time << " sec");
			not_locked = false;
		}

		BEESNOTE("idle " << BEES_HASH_TABLE_ANALYZE_INTERVAL << "s");
		unique_lock<mutex> lock(m_stop_mutex);
		if (m_stop_requested) {
			BEESLOGDEBUG("Stop requested in hash table prefetch");
			return;
		}
		m_stop_condvar.wait_for(lock, chrono::duration<double>(BEES_HASH_TABLE_ANALYZE_INTERVAL));
	}
}

size_t
BeesHashTable::hash_to_extent_index(HashType hash)
{
	auto pr = get_extent_range(hash);
	uint64_t extent_index = reinterpret_cast<const Extent *>(pr.first) - m_extent_ptr;
	THROW_CHECK2(runtime_error, extent_index, m_extents, extent_index < m_extents);
	return extent_index;
}

BeesHashTable::ExtentMetaData::ExtentMetaData() :
	m_mutex_ptr(make_shared<mutex>())
{
}

unique_lock<mutex>
BeesHashTable::lock_extent_by_index(uint64_t extent_index)
{
	THROW_CHECK2(out_of_range, extent_index, m_extents, extent_index < m_extents);
	return unique_lock<mutex>(*m_extent_metadata.at(extent_index).m_mutex_ptr);
}

unique_lock<mutex>
BeesHashTable::lock_extent_by_hash(HashType hash)
{
	BEESTOOLONG("fetch_missing_extent for hash " << to_hex(hash));
	return lock_extent_by_index(hash_to_extent_index(hash));
}

void
BeesHashTable::fetch_missing_extent_by_index(uint64_t extent_index)
{
	BEESNOTE("checking hash extent #" << extent_index << " of " << m_extents << " extents");
	auto lock = lock_extent_by_index(extent_index);
	if (!m_extent_metadata.at(extent_index).m_missing) {
		return;
	}

	// OK we have to read this extent
	BEESNOTE("fetching hash extent #" << extent_index << " of " << m_extents << " extents");
	BEESTRACE("Fetching hash extent #" << extent_index << " of " << m_extents << " extents");
	BEESTOOLONG("Fetching hash extent #" << extent_index << " of " << m_extents << " extents");

	uint8_t *const dirty_extent      = m_extent_ptr[extent_index].p_byte;
	uint8_t *const dirty_extent_end  = m_extent_ptr[extent_index + 1].p_byte;
	const size_t dirty_extent_size   = dirty_extent_end - dirty_extent;
	const size_t dirty_extent_offset = dirty_extent - m_byte_ptr;

	// If the read fails don't retry, just go with whatever data we have
	m_extent_metadata.at(extent_index).m_missing = false;

	catch_all([&]() {
		BEESTOOLONG("pread(fd " << m_fd << " '" << name_fd(m_fd)<< "', length " << to_hex(dirty_extent_end - dirty_extent) << ", offset " << to_hex(dirty_extent - m_byte_ptr) << ")");
		pread_or_die(m_fd, dirty_extent, dirty_extent_size, dirty_extent_offset);

		// Only count extents successfully read
		BEESCOUNT(hash_extent_in);

		// Won't need that again
		bees_unreadahead(m_fd, dirty_extent_offset, dirty_extent_size);

		// If we are in prefetch, give the kernel a hint about the next extent
		if (m_prefetch_running) {
			// Use the kernel readahead here, because it might work for this use case
			readahead(m_fd, dirty_extent_offset + dirty_extent_size, dirty_extent_size);
		}
	});

	Cell *cell     = m_extent_ptr[extent_index    ].p_buckets[0].p_cells;
	Cell *cell_end = m_extent_ptr[extent_index + 1].p_buckets[0].p_cells;
	size_t toxic_cleared_count = 0;
	set<BeesHashTable::Cell> seen_it(cell, cell_end);
	while (cell < cell_end) {
		if (cell->e_addr & BeesAddress::c_toxic_mask) {
			++toxic_cleared_count;
			cell->e_addr &= ~BeesAddress::c_toxic_mask;
			// Clearing the toxic bit might mean we now have a duplicate.
			// This could be due to a race between two
			// inserts, one finds the extent toxic while the
			// other does not.  That's arguably a bug elsewhere,
			// but we should rewrite the whole extent lookup/insert
			// loop, not spend time fixing code that will be
			// thrown out later anyway.
			// If there is a cell that is identical to this one
			// except for the toxic bit, then we don't need this one.
			if (seen_it.count(*cell)) {
				cell->e_addr = 0;
				cell->e_hash = 0;
			}
		}
		++cell;
	}
	if (toxic_cleared_count) {
		BEESLOGDEBUG("Cleared " << toxic_cleared_count << " hashes while fetching hash table extent " << extent_index);
	}
}

void
BeesHashTable::fetch_missing_extent_by_hash(HashType hash)
{
	uint64_t extent_index = hash_to_extent_index(hash);
	BEESNOTE("waiting to fetch hash extent #" << extent_index << " of " << m_extents << " extents");

	fetch_missing_extent_by_index(extent_index);
}

vector<BeesHashTable::Cell>
BeesHashTable::find_cell(HashType hash)
{
	fetch_missing_extent_by_hash(hash);
	BEESTOOLONG("find_cell hash " << BeesHash(hash));
	vector<Cell> rv;
	auto lock = lock_extent_by_hash(hash);
	auto er = get_cell_range(hash);
	// FIXME:  Weed out zero addresses in the table due to earlier bugs
	copy_if(er.first, er.second, back_inserter(rv), [=](const Cell &ip) { return ip.e_hash == hash && ip.e_addr >= 0x1000; });
	BEESCOUNT(hash_lookup);
	return rv;
}

/// Remove a hash from the table, leaving an empty space on the list
/// where the hash used to be.  Used when an invalid address is found
/// because lookups on invalid addresses really hurt.
void
BeesHashTable::erase_hash_addr(HashType hash, AddrType addr)
{
	fetch_missing_extent_by_hash(hash);
	BEESTOOLONG("erase hash " << to_hex(hash) << " addr " << addr);
	auto lock = lock_extent_by_hash(hash);
	auto er = get_cell_range(hash);
	Cell mv(hash, addr);
	Cell *ip = find(er.first, er.second, mv);
	bool found = (ip < er.second);
	if (found) {
		*ip = Cell(0, 0);
		set_extent_dirty_locked(hash_to_extent_index(hash));
		BEESCOUNT(hash_erase);
#if 0
		if (verify_cell_range(er.first, er.second)) {
			BEESLOGDEBUG("while erasing hash " << hash << " addr " << addr);
		}
#endif
	} else {
		BEESCOUNT(hash_erase_miss);
	}
}

/// Insert a hash entry at the head of the list.  If entry is already
/// present in list, move it to the front of the list without dropping
/// any entries, and return true.  If entry is not present in list,
/// insert it at the front of the list, possibly dropping the last entry
/// in the list, and return false.  Used to move duplicate hash blocks
/// to the front of the list.
bool
BeesHashTable::push_front_hash_addr(HashType hash, AddrType addr)
{
	fetch_missing_extent_by_hash(hash);
	BEESTOOLONG("push_front_hash_addr hash " << BeesHash(hash) <<" addr " << BeesAddress(addr));
	auto lock = lock_extent_by_hash(hash);
	auto er = get_cell_range(hash);
	Cell mv(hash, addr);
	Cell *ip = find(er.first, er.second, mv);
	bool found = (ip < er.second);
	if (!found) {
		// If no match found, get rid of an empty space instead
		// If no empty spaces, ip will point to end
		ip = find(er.first, er.second, Cell(0, 0));
	}
	if (ip > er.first) {
		// Delete matching entry, first empty entry,
		// or last entry whether empty or not
		// move_backward(er.first, ip - 1, ip);
		auto sp = ip;
		auto dp = ip;
		--sp;
		// If we are deleting the last entry then don't copy it
		if (dp == er.second) {
			--sp;
			--dp;
			BEESCOUNT(hash_evict);
		}
		while (dp > er.first) {
			*dp-- = *sp--;
		}
	}
	// There is now a space at the front, insert there if different
	if (er.first[0] != mv) {
		er.first[0] = mv;
		set_extent_dirty_locked(hash_to_extent_index(hash));
		BEESCOUNT(hash_front);
	} else {
		BEESCOUNT(hash_front_already);
	}
#if 0
	if (verify_cell_range(er.first, er.second)) {
		BEESLOGDEBUG("while push_fronting hash " << hash << " addr " << addr);
	}
#endif
	return found;
}

thread_local uniform_int_distribution<size_t> BeesHashTable::tl_distribution(0, c_cells_per_bucket - 1);

/// Insert a hash entry at some unspecified point in the list.
/// If entry is already present in list, returns true and does not
/// modify list.  If entry is not present in list, returns false and
/// inserts at a random position in the list, possibly evicting the entry
/// at the end of the list.  Used to insert new unique (not-yet-duplicate)
/// blocks in random order.
bool
BeesHashTable::push_random_hash_addr(HashType hash, AddrType addr)
{
	fetch_missing_extent_by_hash(hash);
	BEESTOOLONG("push_random_hash_addr hash " << BeesHash(hash) << " addr " << BeesAddress(addr));
	auto lock = lock_extent_by_hash(hash);
	auto er = get_cell_range(hash);
	Cell mv(hash, addr);
	Cell *ip = find(er.first, er.second, mv);
	bool found = (ip < er.second);

	const auto pos = tl_distribution(bees_generator);

	int case_cond = 0;
#if 0
	vector<Cell> saved(er.first, er.second);
#endif

	if (found) {
		// If hash already exists after pos, swap with pos
		if (ip > er.first + pos) {

			// move_backward(er.first + pos, ip - 1, ip);
			auto sp = ip;
			auto dp = ip;
			--sp;
			while (dp > er.first + pos) {
				*dp-- = *sp--;
			}
			*dp = mv;
			BEESCOUNT(hash_bump);
			case_cond = 1;
			goto ret_dirty;
		}
		// Hash already exists before (or at) pos, leave it there
		BEESCOUNT(hash_already);
		case_cond = 2;
		goto ret;
	}

	// Find an empty space to back of pos
	for (ip = er.first + pos; ip < er.second; ++ip) {
		if (*ip == Cell(0, 0)) {
			*ip = mv;
			case_cond = 3;
			goto ret_dirty;
		}
	}

	// Find an empty space to front of pos
	// if there is anything to front of pos
	if (pos > 0) {
		for (ip = er.first + pos - 1; ip >= er.first; --ip) {
			if (*ip == Cell(0, 0)) {
				*ip = mv;
				case_cond = 4;
				goto ret_dirty;
			}
		}
	}

	// Evict something and insert at pos
	// move_backward(er.first + pos, er.second - 1, er.second);
	ip = er.second - 1;
	while (ip > er.first + pos) {
		auto dp = ip;
		*dp = *--ip;
	}
	er.first[pos] = mv;
	BEESCOUNT(hash_evict);
	case_cond = 5;
ret_dirty:
	BEESCOUNT(hash_insert);
	set_extent_dirty_locked(hash_to_extent_index(hash));
ret:
#if 0
	if (verify_cell_range(er.first, er.second, false)) {
		BEESLOG("while push_randoming (case " << case_cond << ") pos " << pos
			<< " ip " << (ip - er.first) << " " << mv);
		// dump_bucket_locked(saved.data(), saved.data() + saved.size());
		// dump_bucket_locked(er.first, er.second);
	}
#else
	(void)case_cond;
#endif
	return found;
}

void
BeesHashTable::try_mmap_flags(int flags)
{
	if (!m_cell_ptr) {
		THROW_CHECK1(out_of_range, m_size, m_size > 0);
		Timer map_time;
		catch_all([&]() {
			BEESLOGINFO("mapping hash table size " << m_size << " with flags " << mmap_flags_ntoa(flags));
			void *ptr = mmap_or_die(nullptr, m_size, PROT_READ | PROT_WRITE, flags, flags & MAP_ANONYMOUS ? -1 : int(m_fd), 0);
			BEESLOGINFO("mmap done in " << map_time << " sec");
			m_cell_ptr = static_cast<Cell *>(ptr);
			void *ptr_end = static_cast<uint8_t *>(ptr) + m_size;
			m_cell_ptr_end = static_cast<Cell *>(ptr_end);
		});
	}
}

void
BeesHashTable::open_file()
{
	// OK open hash table
	BEESNOTE("opening hash table '" << m_filename << "' target size " << m_size << " (" << pretty(m_size) << ")");

	// Try to open existing hash table
	Fd new_fd = openat(m_ctx->home_fd(), m_filename.c_str(), FLAGS_OPEN_FILE_RW, 0700);

	// If that doesn't work, try to make a new one
	if (!new_fd) {
		string tmp_filename = m_filename + ".tmp";
		BEESNOTE("creating new hash table '" << tmp_filename << "'");
		BEESLOGINFO("Creating new hash table '" << tmp_filename << "'");
		unlinkat(m_ctx->home_fd(), tmp_filename.c_str(), 0);
		new_fd = openat_or_die(m_ctx->home_fd(), tmp_filename, FLAGS_CREATE_FILE, 0700);
		BEESNOTE("truncating new hash table '" << tmp_filename << "' size " << m_size << " (" << pretty(m_size) << ")");
		BEESLOGINFO("Truncating new hash table '" << tmp_filename << "' size " << m_size << " (" << pretty(m_size) << ")");
		ftruncate_or_die(new_fd, m_size);
		BEESNOTE("truncating new hash table '" << tmp_filename << "' -> '" << m_filename << "'");
		BEESLOGINFO("Truncating new hash table '" << tmp_filename << "' -> '" << m_filename << "'");
		renameat_or_die(m_ctx->home_fd(), tmp_filename, m_ctx->home_fd(), m_filename);
	}

	Stat st(new_fd);
	off_t new_size = st.st_size;

	THROW_CHECK1(invalid_argument, new_size, new_size > 0);
	THROW_CHECK1(invalid_argument, new_size, (new_size % BLOCK_SIZE_HASHTAB_EXTENT) == 0);
	m_size = new_size;
	m_fd = new_fd;
}

BeesHashTable::BeesHashTable(shared_ptr<BeesContext> ctx, string filename, off_t size) :
	m_ctx(ctx),
	m_size(0),
	m_void_ptr(nullptr),
	m_void_ptr_end(nullptr),
	m_buckets(0),
	m_cells(0),
	m_writeback_thread("hash_writeback"),
	m_prefetch_thread("hash_prefetch"),
	m_flush_rate_limit(BEES_FLUSH_RATE),
	m_stats_file(m_ctx->home_fd(), "beesstats.txt")
{
	// Sanity checks to protect the implementation from its weaknesses
	THROW_CHECK2(invalid_argument, BLOCK_SIZE_HASHTAB_BUCKET, BLOCK_SIZE_HASHTAB_EXTENT, (BLOCK_SIZE_HASHTAB_EXTENT % BLOCK_SIZE_HASHTAB_BUCKET) == 0);

	// There's more than one union
	THROW_CHECK2(runtime_error, sizeof(Bucket), BLOCK_SIZE_HASHTAB_BUCKET, BLOCK_SIZE_HASHTAB_BUCKET == sizeof(Bucket));
	THROW_CHECK2(runtime_error, sizeof(Bucket::p_byte), BLOCK_SIZE_HASHTAB_BUCKET, BLOCK_SIZE_HASHTAB_BUCKET == sizeof(Bucket::p_byte));
	THROW_CHECK2(runtime_error, sizeof(Extent), BLOCK_SIZE_HASHTAB_EXTENT, BLOCK_SIZE_HASHTAB_EXTENT == sizeof(Extent));
	THROW_CHECK2(runtime_error, sizeof(Extent::p_byte), BLOCK_SIZE_HASHTAB_EXTENT, BLOCK_SIZE_HASHTAB_EXTENT == sizeof(Extent::p_byte));

	m_filename = filename;
	m_size = size;
	open_file();

	// Now we know size we can compute stuff

	BEESTRACE("hash table size " << m_size);
	BEESTRACE("hash table bucket size " << BLOCK_SIZE_HASHTAB_BUCKET);
	BEESTRACE("hash table extent size " << BLOCK_SIZE_HASHTAB_EXTENT);

	BEESLOGINFO("opened hash table filename '" << filename << "' length " << m_size);
	m_buckets = m_size / BLOCK_SIZE_HASHTAB_BUCKET;
	m_cells = m_buckets * c_cells_per_bucket;
	m_extents = (m_size + BLOCK_SIZE_HASHTAB_EXTENT - 1) / BLOCK_SIZE_HASHTAB_EXTENT;
	BEESLOGINFO("\tcells " << m_cells << ", buckets " << m_buckets << ", extents " << m_extents);

	BEESLOGINFO("\tflush rate limit " << BEES_FLUSH_RATE);

	// Try to mmap that much memory
	try_mmap_flags(MAP_PRIVATE | MAP_ANONYMOUS);

	if (!m_cell_ptr) {
		THROW_ERRNO("unable to mmap " << filename);
	}

	// Do unions work the way we think (and rely on)?
	THROW_CHECK2(runtime_error, m_void_ptr, m_cell_ptr, m_void_ptr == m_cell_ptr);
	THROW_CHECK2(runtime_error, m_void_ptr, m_byte_ptr, m_void_ptr == m_byte_ptr);
	THROW_CHECK2(runtime_error, m_void_ptr, m_bucket_ptr, m_void_ptr == m_bucket_ptr);
	THROW_CHECK2(runtime_error, m_void_ptr, m_extent_ptr, m_void_ptr == m_extent_ptr);

	// Give all the madvise hints that the kernel understands
	const struct madv_flag {
		const char *name;
		int value;
	} madv_flags[] = {
		{ .name = "MADV_HUGEPAGE", .value = MADV_HUGEPAGE },
		{ .name = "MADV_DONTFORK", .value = MADV_DONTFORK },
		{ .name = "MADV_DONTDUMP", .value = MADV_DONTDUMP },
		{ .name = "", .value = 0 },
	};
	for (auto fp = madv_flags; fp->value; ++fp) {
		BEESTOOLONG("madvise(" << fp->name << ")");
		if (madvise(m_byte_ptr, m_size, fp->value)) {
			BEESLOGWARN("madvise(..., " << fp->name << "): " << strerror(errno) << " (ignored)");
		}
	}

	m_extent_metadata.resize(m_extents);

	m_writeback_thread.exec([&]() {
		writeback_loop();
        });

	m_prefetch_thread.exec([&]() {
		prefetch_loop();
        });

	// Blacklist might fail if the hash table is not stored on a btrfs
	catch_all([&]() {
		m_ctx->blacklist_insert(BeesFileId(m_fd));
	});
}

BeesHashTable::~BeesHashTable()
{
	BEESLOGDEBUG("Destroy BeesHashTable");
	if (m_cell_ptr && m_size) {
		// Dirty extents should have been flushed before now,
		// e.g. in stop().  If that didn't happen, don't fall
		// into the same trap (and maybe throw an exception) here.
		// flush_dirty_extents(false);
		catch_all([&]() {
			// drop the memory mapping
			BEESTOOLONG("unmap handle table size " << pretty(m_size));
			DIE_IF_NON_ZERO(munmap(m_cell_ptr, m_size));
		});
		m_cell_ptr = nullptr;
		m_size = 0;
	}
	BEESLOGDEBUG("BeesHashTable destroyed");
}

void
BeesHashTable::stop_request()
{
	BEESNOTE("stopping BeesHashTable threads");
	BEESLOGDEBUG("Stopping BeesHashTable threads");

	unique_lock<mutex> lock(m_stop_mutex);
	m_stop_requested = true;
	m_stop_condvar.notify_all();
	lock.unlock();

	// Wake up hash writeback too
	unique_lock<mutex> dirty_lock(m_dirty_mutex);
	m_dirty_condvar.notify_all();
	dirty_lock.unlock();
}

void
BeesHashTable::stop_wait()
{
	BEESNOTE("waiting for hash_prefetch thread");
	BEESLOGDEBUG("Waiting for hash_prefetch thread");
	m_prefetch_thread.join();

	BEESNOTE("waiting for hash_writeback thread");
	BEESLOGDEBUG("Waiting for hash_writeback thread");
	m_writeback_thread.join();

	BEESLOGDEBUG("BeesHashTable stopped");
}
