#include "crucible/extentwalker.h"

#include "crucible/chatter.h"
#include "crucible/error.h"
#include "crucible/fs.h"
#include "crucible/limits.h"
#include "crucible/string.h"


namespace crucible {
	using namespace std;

	const off_t ExtentWalker::sc_step_size;

	// fm_start, fm_length, fm_flags, m_extents
	// fe_logical, fe_physical, fe_length, fe_flags

	static const off_t MAX_OFFSET = numeric_limits<off_t>::max();
	static const off_t FIEMAP_BLOCK_SIZE = 4096;

	static bool __ew_do_log = getenv("EXTENTWALKER_DEBUG");

#define EWLOG(x) do { \
	if (__ew_do_log) { \
		CHATTER(x); \
	} \
} while (0)

	ostream &
	operator<<(ostream &os, const Extent &e)
	{
		os << "Extent {"
			<< " begin = " << to_hex(e.m_begin)
			<< ", end = " << to_hex(e.m_end)
			<< ", physical = " << to_hex(e.m_physical)
			<< ", flags = ";
		if (e.m_flags & Extent::HOLE) {
			os << "Extent::HOLE|";
		}
		if (e.m_flags & Extent::PREALLOC) {
			os << "Extent::PREALLOC|";
		}
		if (e.m_flags & Extent::OBSCURED) {
			os << "Extent::OBSCURED|";
		}
		if (e.m_flags & ~(Extent::HOLE | Extent::PREALLOC | Extent::OBSCURED)) {
			os << fiemap_extent_flags_ntoa(e.m_flags & ~(Extent::HOLE | Extent::PREALLOC | Extent::OBSCURED));
		}
		if (e.m_physical_len) {
			os << ", physical_len = " << to_hex(e.m_physical_len);
		}
		if (e.m_logical_len) {
			os << ", logical_len = " << to_hex(e.m_logical_len);
		}
		if (e.m_offset) {
			os << ", offset = " << to_hex(e.m_offset);
		}
		return os << " }";
	}

	ostream &
	operator<<(ostream &os, const ExtentWalker::Vec &v)
	{
		os << "ExtentWalker::Vec {";
		for (auto e : v) {
			os << "\n\t" << e;
		}
		return os << "}";
	}

	ostream &
	operator<<(ostream &os, const ExtentWalker &ew)
	{
		return os << "ExtentWalker {"
			<< " fd = " << name_fd(ew.m_fd)
			<< ", stat.st_size = " << to_hex(ew.m_stat.st_size)
			<< ", extents = " << ew.m_extents
			<< ", current = [" << ew.m_current - ew.m_extents.begin()
			<< "] }";
	}

	Extent::operator bool() const
	{
		THROW_CHECK2(invalid_argument, m_begin, m_end, m_end >= m_begin);
		return m_end > m_begin;
	}

	off_t
	Extent::size() const
	{
		THROW_CHECK2(invalid_argument, m_begin, m_end, m_end >= m_begin);
		return m_end - m_begin;
	}

	bool
	Extent::operator==(const Extent &that) const
	{
		return m_begin == that.m_begin && m_end == that.m_end && m_physical == that.m_physical && m_flags == that.m_flags;
	}

	bool
	Extent::compressed() const
	{
		return m_flags & FIEMAP_EXTENT_ENCODED;
	}

	uint64_t
	Extent::bytenr() const
	{
		return compressed() ? m_physical : m_physical - m_offset;
	}

	ExtentWalker::ExtentWalker(Fd fd) :
		m_fd(fd),
		m_current(m_extents.begin())
	{
	}

	ExtentWalker::ExtentWalker(Fd fd, off_t initial_pos) :
		m_fd(fd),
		m_current(m_extents.begin())
	{
		seek(initial_pos);
	}

	ExtentWalker::Itr
	ExtentWalker::find_in_cache(off_t pos)
	{
		EWLOG("find_in_cache " << to_hex(pos));
		// EOF is an annoying special case
		if (pos >= m_stat.st_size) {
			if (!m_extents.empty() && m_extents.rbegin()->m_end == m_stat.st_size) {
				auto i = m_extents.end();
				return --i;
			}
		}
		for (auto vi = m_extents.begin(); vi != m_extents.end(); ++vi) {
			if (pos >= vi->m_begin && pos < vi->m_end) {
				EWLOG("pos " << to_hex(pos) << " in " << *vi);
				if (vi == m_extents.begin() && !(m_extents.begin()->m_begin == 0)) {
					// Must have an extent before pos, unless
					// there can be no extent before pos because pos == 0
					EWLOG("can't match first unless begin is BOF");
					break;
				}
				auto ni = vi;
				++ni;
				if (ni == m_extents.end() && !(vi->m_end >= m_stat.st_size)) {
					// Must have an extent after pos, unless
					// there can be no extent after pos because pos >= EOF
					EWLOG("can't match last unless end past EOF " << to_hex(m_stat.st_size));
					break;
				}
				// Extent surrounded on either side by other known extents
				return vi;
			}
		}
		EWLOG("find_in_cache failed: " << *this);
		return m_extents.end();
	}

	void
	ExtentWalker::run_fiemap(off_t pos)
	{
		ostringstream log;
		CHATTER_UNWIND("Log of run_fiemap: " << log.str());

		EWLOG("pos = " << to_hex(pos));

		THROW_CHECK1(invalid_argument, pos, (pos & (FIEMAP_BLOCK_SIZE - 1)) == 0);

		Vec fm;

		off_t step_size = pos;
		off_t begin = pos - min(pos, sc_step_size);

		// This loop should not run forever
		int loop_count = 0;
		int loop_limit = 99;
		while (true) {
			if (loop_count == 90) {
				EWLOG(log.str());
			}

			THROW_CHECK1(runtime_error, loop_count, loop_count < loop_limit);
			++loop_count;

			// Get file size every time in case it changes under us
			m_stat.fstat(m_fd);

			// Get fiemap begin..EOF
			fm = get_extent_map(begin);
			EWLOG("fiemap result loop count #" << loop_count << ":" << fm);

			// This algorithm seeks at least three extents: one before,
			// one after, and one containing pos.  Files which contain
			// two or fewer extents will cause an obvious problem with that,
			// so handle those cases separately.

			// FIEMAP lies, and we catch it in a lie about the size of the
			// second extent.  To work around this, try getting more than 3.

			// 0..2(ish) extents
			if (fm.size() < sc_extent_fetch_min) {
				// If we are not at beginning of file, move backward
				if (begin > 0) {
					step_size /= 2;
					auto next_begin = (begin - min(step_size, begin)) & ~(FIEMAP_BLOCK_SIZE - 1);
					EWLOG("step backward " << to_hex(begin) << " -> " << to_hex(next_begin) << " extents size " << fm.size());
					if (begin == next_begin) {
						EWLOG("step backward stopped");
						break;
					}
					begin = next_begin;
					continue;
				}

				// We are at beginning of file and have too few extents.

				// Zero extents?  Entire file is a hole.
				if (fm.empty()) {
					EWLOG("zero extents");
					break;
				}

				// We know we have the beginning of the file and at least
				// one extent.  If the last extent is EOF then we have the
				// whole file in the buffer.  If the last extent is NOT
				// EOF then fiemap did something we didn't expect.
				THROW_CHECK1(runtime_error, fm.rbegin()->flags(), fm.rbegin()->flags() & FIEMAP_EXTENT_LAST);
				break;
			}

			// We have at least three extents, so there is now a first and last.
			// We want pos to be between first and last.  There doesn't have
			// to be an extent between these (it could be a hole).
			auto &first_extent = fm.at(sc_extent_fetch_min - 2);
			auto &last_extent = *fm.rbegin();
			EWLOG("first_extent = " << first_extent);
			EWLOG("last_extent = " << last_extent);

			// First extent must end on or before pos
			if (first_extent.end() > pos) {
				// Can we move backward?
				if (begin > 0) {
					step_size /= 2;
					auto next_begin = (begin - min(step_size, begin)) & ~(FIEMAP_BLOCK_SIZE - 1);
					EWLOG("step backward " << to_hex(begin) << " -> " << to_hex(next_begin) << " extents size " << fm.size());
					if (begin == next_begin) {
						EWLOG("step backward stopped");
						break;
					}
					begin = next_begin;
					continue;
				}

				// We are as far back as we can go, so there must be no
				// extent before pos (i.e. file starts with a hole).
				EWLOG("no extent before pos");
				break;
			}

			// First extent ends on or before pos.

			// If last extent is EOF then we have the entire file in the buffer.
			// pos could be in last extent, so skip the later checks that
			// insist pos be located prior to the last extent.
			if (last_extent.flags() & FIEMAP_EXTENT_LAST) {
				break;
			}

			// Don't have EOF, must have an extent after pos.
			if (last_extent.begin() <= pos) {
				step_size /= 2;
				auto new_begin = (begin + step_size) & ~(FIEMAP_BLOCK_SIZE - 1);
				EWLOG("step forward " << to_hex(begin) << " -> " << to_hex(new_begin));
				if (begin == new_begin) {
					EWLOG("step forward stopped");
					break;
				}
				begin = new_begin;
				continue;
			}

			// Last extent begins after pos, first extent ends on or before pos.
			// All other cases should have been handled before here.
			THROW_CHECK2(runtime_error, pos, first_extent, first_extent.end() <= pos);
			THROW_CHECK2(runtime_error, pos, last_extent, last_extent.begin() > pos);

			// We should probably stop now
			break;
		}

		// Fill in holes so there are Extent records over entire range
		auto fmi = fm.begin();
		off_t ipos = begin;
		Vec new_vec;
		// If we mapped the entire file and there are no extents,
		// the entire file is a hole.
		bool last_extent_is_last = (begin == 0 && fm.empty());
		while (fmi != fm.end()) {
			Extent new_extent(*fmi);
			THROW_CHECK2(runtime_error, ipos, new_extent.m_begin, ipos <= new_extent.m_begin);
			if (new_extent.m_begin > ipos) {
				Extent hole_extent;
				hole_extent.m_begin = ipos;
				hole_extent.m_end = fmi->begin();
				hole_extent.m_physical = 0;
				hole_extent.m_flags = Extent::HOLE;
				new_vec.push_back(hole_extent);
				ipos += hole_extent.size();
			}
			THROW_CHECK2(runtime_error, ipos, new_extent.m_begin, ipos == new_extent.m_begin);
			new_vec.push_back(new_extent);
			ipos += new_extent.size();
			last_extent_is_last = fmi->flags() & FIEMAP_EXTENT_LAST;
			++fmi;
		}
		// If we have run out of extents before EOF, insert a hole at the end
		if (last_extent_is_last && ipos < m_stat.st_size) {
			Extent hole_extent;
			hole_extent.m_begin = ipos;
			hole_extent.m_end = m_stat.st_size;
			hole_extent.m_physical = 0;
			hole_extent.m_flags = Extent::HOLE;
			if (!new_vec.empty() && new_vec.rbegin()->m_flags & FIEMAP_EXTENT_LAST) {
				new_vec.rbegin()->m_flags &= ~(FIEMAP_EXTENT_LAST);
				hole_extent.m_flags |= FIEMAP_EXTENT_LAST;
			}
			new_vec.push_back(hole_extent);
			ipos += new_vec.size();
		}
		THROW_CHECK1(runtime_error, new_vec.size(), !new_vec.empty());

		// Allow last extent to extend beyond desired range (e.g. at EOF)
		THROW_CHECK2(runtime_error, ipos, new_vec.rbegin()->m_end, ipos <= new_vec.rbegin()->m_end);
		// If we have the last extent in the file, truncate it to the file size.
		if (ipos >= m_stat.st_size) {
			THROW_CHECK2(runtime_error, new_vec.rbegin()->m_begin, m_stat.st_size, m_stat.st_size > new_vec.rbegin()->m_begin);
			THROW_CHECK2(runtime_error, new_vec.rbegin()->m_end, m_stat.st_size, m_stat.st_size <= new_vec.rbegin()->m_end);
			new_vec.rbegin()->m_end = m_stat.st_size;
		}

		// Verify contiguous, ascending order, at least one Extent
		THROW_CHECK1(runtime_error, new_vec, !new_vec.empty());

		ipos = new_vec.begin()->m_begin;
		bool last_flag_last = false;
		for (auto e : new_vec) {
			THROW_CHECK1(runtime_error, new_vec, e.m_begin == ipos);
			THROW_CHECK1(runtime_error, e, e.size() > 0);
			THROW_CHECK1(runtime_error, new_vec, !last_flag_last);
			ipos += e.size();
			last_flag_last = e.m_flags & FIEMAP_EXTENT_LAST;
		}
		THROW_CHECK1(runtime_error, new_vec, !last_extent_is_last || new_vec.rbegin()->m_end == ipos);

		m_extents = new_vec;
		m_current = m_extents.begin();
	}

	void
	ExtentWalker::reset()
	{
		m_extents.clear();
		m_current = m_extents.begin();
	}

	void
	ExtentWalker::seek(off_t pos)
	{
		CHATTER_UNWIND("seek " << to_hex(pos));
		THROW_CHECK1(out_of_range, pos, pos >= 0);
		Itr rv = find_in_cache(pos);
		if (rv != m_extents.end()) {
			m_current = rv;
			return;
		}
		run_fiemap(pos);
		m_current = find_in_cache(pos);
	}

	Extent
	ExtentWalker::current()
	{
		THROW_CHECK2(invalid_argument, *this, m_extents.size(), m_current != m_extents.end());
		CHATTER_UNWIND("current " << *m_current);
		return *m_current;
	}


	bool
	ExtentWalker::next()
	{
		CHATTER_UNWIND("next");
		THROW_CHECK1(invalid_argument, (m_current != m_extents.end()), m_current != m_extents.end());
		if (current().m_end >= m_stat.st_size) {
			CHATTER_UNWIND("next EOF");
			return false;
		}
		auto next_pos = current().m_end;
		if (next_pos >= m_stat.st_size) {
			CHATTER_UNWIND("next next_pos = " << next_pos << " m_stat.st_size = " << m_stat.st_size);
			return false;
		}
		seek(next_pos);
		THROW_CHECK1(runtime_error, (m_current != m_extents.end()), m_current != m_extents.end());

		// FIEMAP is full of lies, so this check keeps failing
		// THROW_CHECK2(runtime_error, current().m_begin, next_pos, current().m_begin == next_pos);
		// Just ensure that pos is in the next extent somewhere.
		THROW_CHECK2(runtime_error, current(), next_pos, current().m_begin <= next_pos);
		THROW_CHECK2(runtime_error, current(), next_pos, current().m_end > next_pos);

		return true;
	}

	bool
	ExtentWalker::prev()
	{
		CHATTER_UNWIND("prev");
		THROW_CHECK1(invalid_argument, (m_current != m_extents.end()), m_current != m_extents.end());
		auto prev_iter = m_current;
		if (prev_iter->m_begin == 0) {
			CHATTER_UNWIND("prev BOF");
			return false;
		}
		THROW_CHECK1(invalid_argument, (prev_iter != m_extents.begin()), prev_iter != m_extents.begin());
		--prev_iter;
		CHATTER_UNWIND("prev seeking to " << *prev_iter << "->m_begin");
		auto prev_end = current().m_begin;
		seek(prev_iter->m_begin);
		THROW_CHECK1(runtime_error, (m_current != m_extents.end()), m_current != m_extents.end());
		THROW_CHECK2(runtime_error, current().m_end, prev_end, current().m_end == prev_end);
		return true;
	}

	ExtentWalker::~ExtentWalker()
	{
	}

	BtrfsExtentWalker::BtrfsExtentWalker(Fd fd) :
		ExtentWalker(fd),
		m_tree_id(0)
	{
	}

	BtrfsExtentWalker::BtrfsExtentWalker(Fd fd, off_t initial_pos) :
		ExtentWalker(fd),
		m_tree_id(0)
	{
		seek(initial_pos);
	}

	void
	BtrfsExtentWalker::set_root_fd(Fd root_fd)
	{
		m_root_fd = root_fd;
	}

	BtrfsExtentWalker::BtrfsExtentWalker(Fd fd, off_t initial_pos, Fd root_fd) :
		ExtentWalker(fd),
		m_tree_id(0)
	{
		set_root_fd(root_fd);
		seek(initial_pos);
	}

	BtrfsExtentWalker::Vec
	BtrfsExtentWalker::get_extent_map(off_t pos)
	{
		BtrfsIoctlSearchKey sk(sc_extent_fetch_max * (sizeof(btrfs_file_extent_item) + sizeof(btrfs_ioctl_search_header)));
		if (!m_root_fd) {
			m_root_fd = m_fd;
		}
		if (!m_tree_id) {
			m_tree_id = btrfs_get_root_id(m_fd);
		}
		sk.tree_id = m_tree_id;
		sk.min_objectid = m_stat.st_ino;
		sk.max_objectid = numeric_limits<uint64_t>::max();
		sk.min_offset = ranged_cast<uint64_t>(pos);
		sk.max_offset = numeric_limits<uint64_t>::max();
		sk.min_transid = 0;
		sk.max_transid = numeric_limits<uint64_t>::max();
		sk.min_type = sk.max_type = BTRFS_EXTENT_DATA_KEY;
		sk.nr_items = sc_extent_fetch_max;

		CHATTER_UNWIND("sk " << sk << " root_fd " << name_fd(m_root_fd));
		sk.do_ioctl(m_root_fd);

		Vec rv;

		bool past_eof = false;
		for (auto i : sk.m_result) {
			// If we're seeing extents from the next file then we're past EOF on this file
			if (i.objectid > m_stat.st_ino) {
				past_eof = true;
				break;
			}

			// Ignore things that aren't EXTENT_DATA_KEY
			if (i.type != BTRFS_EXTENT_DATA_KEY) {
				continue;
			}

			// Hmmmkay we shouldn't be seeing these
			if (i.objectid < m_stat.st_ino) {
				THROW_ERROR(out_of_range, "objectid " << i.objectid << " < m_stat.st_ino " << m_stat.st_ino);
				continue;
			}

			Extent e;
			e.m_begin = i.offset;
			auto compressed = call_btrfs_get(btrfs_stack_file_extent_compression, i.m_data);
			// FIEMAP told us about compressed extents and we can too
			if (compressed) {
				e.m_flags |= FIEMAP_EXTENT_ENCODED;
			}

			auto type = call_btrfs_get(btrfs_stack_file_extent_type, i.m_data);
			off_t len = -1;
			switch (type) {
				default:
					cerr << "Unhandled file extent type " << type << " in root " << m_tree_id << " ino " << m_stat.st_ino << endl;
					break;
				case BTRFS_FILE_EXTENT_INLINE:
					len = ranged_cast<off_t>(call_btrfs_get(btrfs_stack_file_extent_ram_bytes, i.m_data));
					e.m_flags |= FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_NOT_ALIGNED;
					// Inline extents are never obscured, so don't bother filling in m_physical_len, etc.
					break;
				case BTRFS_FILE_EXTENT_PREALLOC:
					e.m_flags |= Extent::PREALLOC;
					// fallthrough
				case BTRFS_FILE_EXTENT_REG: {
					e.m_physical = call_btrfs_get(btrfs_stack_file_extent_disk_bytenr, i.m_data);

					// This is the length of the full extent (decompressed)
					off_t ram = ranged_cast<off_t>(call_btrfs_get(btrfs_stack_file_extent_ram_bytes, i.m_data));

					// This is the length of the part of the extent appearing in the file (decompressed)
					len = ranged_cast<off_t>(call_btrfs_get(btrfs_stack_file_extent_num_bytes, i.m_data));

					// This is the offset from start of on-disk extent to the part we see in the file (decompressed)
					// May be negative due to the kind of bug we're stuck with forever, so no cast range check
					off_t offset = call_btrfs_get(btrfs_stack_file_extent_offset, i.m_data);

					// If there is a physical address there must be size too
					if (e.m_physical) {
						THROW_CHECK1(runtime_error, ram, ram > 0);
						THROW_CHECK1(runtime_error, len, len > 0);
						THROW_CHECK2(runtime_error, offset, ram, offset < ram);
					} else {
						// There are two kinds of hole in btrfs.  This is the other one.
						e.m_flags |= Extent::HOLE;
					}

					// Partially obscured extent
					// FIXME:  sometimes this happens:
					// i.type == BTRFS_EXTENT_DATA_KEY
					// type = 0x1
					// compressed = 0x0
					// REG start 0x0 offset 0x0 num 0x20000 ram 0x21000 gen 1101121
					// btrfs_file_extent_item {
						// generation = 1101121
						// ram_bytes = 135168
						// compression = 0x0
						// encryption = 0x0
						// other_encoding = 0x0
						// type = 0x1
						// disk_bytenr = 0x0
						// disk_num_bytes = 0x0
						// offset = 0x0
						// num_bytes = 0x20000
					// }
					if (ram != len || offset != 0) {
						e.m_flags |= Extent::OBSCURED;
						// cerr << e << "\nram = " << ram << ", len = " << len << ", offset = " << offset << endl;
					}
					e.m_physical_len = ram;
					e.m_logical_len = len;
					e.m_offset = offset;

					// To maintain compatibility with FIEMAP we ignore the offset for compressed extents.
					// At some point we'll grow out of this.
					if (!compressed) {
						e.m_physical += offset;
					}

                                        break;
				}
			}
			if (len > 0) {
				e.m_end = e.m_begin + len;
				if (e.m_end >= m_stat.st_size) {
					e.m_flags |= FIEMAP_EXTENT_LAST;
				}
				// FIXME:  no FIEMAP_EXTENT_SHARED
				// WONTFIX:  non-trivial to replicate LOGIAL_INO
				rv.push_back(e);
			}
		}

		// Plug a hole at EOF
		if (past_eof && !rv.empty()) {
			rv.rbegin()->m_flags |= FIEMAP_EXTENT_LAST;
		}

		return rv;
	}

	ExtentWalker::Vec
	ExtentWalker::get_extent_map(off_t pos)
	{
		Fiemap fm;
		fm.fm_start = ranged_cast<uint64_t>(pos);
		fm.fm_length = ranged_cast<uint64_t>(numeric_limits<off_t>::max() - pos);
		fm.m_max_count = fm.m_min_count = sc_extent_fetch_max;
		fm.do_ioctl(m_fd);
		Vec rv;
		for (auto i : fm.m_extents) {
			Extent e;
			e.m_begin = ranged_cast<off_t>(i.fe_logical);
			e.m_end = ranged_cast<off_t>(i.fe_logical + i.fe_length);
			e.m_physical = i.fe_physical;
			e.m_flags = i.fe_flags;
			rv.push_back(e);
		}
		return rv;
	}

};
