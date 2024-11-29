#include "bees.h"

#include "crucible/limits.h"
#include "crucible/ntoa.h"
#include "crucible/string.h"

#include <fstream>
#include <inttypes.h>

using namespace crucible;
using namespace std;

ostream &
operator<<(ostream &os, const BeesFileId &bfi)
{
	return os << bfi.root() << ":" << bfi.ino();
}

bool
BeesFileId::operator<(const BeesFileId &that) const
{
	// Order by inode first so we get good locality when scanning across snapshots
	return tie(m_ino, m_root) < tie(that.m_ino, that.m_root);
}

bool
BeesFileId::operator==(const BeesFileId &that) const
{
	return m_root == that.m_root && m_ino == that.m_ino;
}

bool
BeesFileId::operator!=(const BeesFileId &that) const
{
	return m_root != that.m_root || m_ino != that.m_ino;
}

BeesFileId::operator bool() const
{
	return m_root && m_ino;
}

BeesFileId::BeesFileId(const BtrfsInodeOffsetRoot &bior) :
	m_root(bior.m_root),
	m_ino(bior.m_inum)
{
}

BeesFileId::BeesFileId(uint64_t root, uint64_t ino) :
	m_root(root),
	m_ino(ino)
{
}

BeesFileId::BeesFileId(int fd) :
	m_root(btrfs_get_root_id(fd)),
	m_ino(Stat(fd).st_ino)
{
}

BeesFileId::BeesFileId() :
	m_root(0),
	m_ino(0)
{
}

ostream &
operator<<(ostream &os, const BeesFileRange &bfr)
{
	if (bfr.end() == numeric_limits<off_t>::max()) {
		os << "- [" << to_hex(bfr.begin()) << "..eof]";
	} else {
		os << pretty(bfr.size()) << " ";
		if (bfr.begin() != 0) {
			os << "[" << to_hex(bfr.begin());
		} else {
			os << "(";
		}
		os << ".." << to_hex(bfr.end());
		if (!!bfr.m_fd && bfr.end() >= bfr.file_size()) {
			os << ")";
		} else {
			os << "]";
		}
	}
	if (bfr.m_fid) {
		os << " fid = " << bfr.m_fid;
	}
	if (!!bfr.m_fd) {
		os << " fd = " << bfr.m_fd << " '" << name_fd(bfr.m_fd) << "'";
	}
	return os;
}

ostream &
operator<<(ostream &os, const BeesRangePair &brp)
{
	return os << "BeesRangePair: " << pretty(brp.first.size())
		<< " src[" << to_hex(brp.first.begin()) << ".." << to_hex(brp.first.end()) << "]"
		<< " dst[" << to_hex(brp.second.begin()) << ".." << to_hex(brp.second.end()) << "]"
		<< "\nsrc = " << brp.first.fd() << " " << name_fd(brp.first.fd())
		<< "\ndst = " << brp.second.fd() << " " << name_fd(brp.second.fd());
}

bool
BeesFileRange::operator<(const BeesFileRange &that) const
{
	// Read file blocks in order
	return make_tuple(fid(), m_begin, m_end) < make_tuple(that.fid(), that.m_begin, that.m_end);
	// Faster to read big chunks first?  Probably confuses the hell
	// out of crawl state, so let's only keep this if there's a clear
	// performance win.
	// return make_tuple(that.size(), fid(), m_begin, m_end) < make_tuple(size(), that.fid(), that.m_begin, that.m_end);
}

bool
BeesFileRange::operator==(const BeesFileRange &that) const
{
	// These fields are cheap to compare and have the most variety
	if (m_begin != that.m_begin || m_end != that.m_end) {
		return false;
	}
	// If they both have the same fd they're equal,
	// but different fds are not necessarily distinct
	if (!!m_fd && !!that.m_fd && m_fd == that.m_fd) {
		return true;
	}
	// OK now we have to go check their FileIds
	return fid() == that.fid();
}

bool
BeesFileRange::operator!=(const BeesFileRange &that) const
{
	return !((*this) == that);
}

bool
BeesFileRange::empty() const
{
	THROW_CHECK2(invalid_argument, m_begin, m_end, m_begin <= m_end);
	return m_begin >= m_end;
}

off_t
BeesFileRange::size() const
{
	THROW_CHECK2(invalid_argument, m_begin, m_end, m_begin <= m_end);
	return m_end - m_begin;
}

off_t
BeesFileRange::file_size() const
{
	if (m_file_size <= 0) {
		Stat st(fd());
		m_file_size = st.st_size;
		// These checks could trigger on valid input, but that would mean we have
		// lost a race (e.g. a file was truncated while we were building a
		// matching range pair with it).  In such cases we should probably stop
		// whatever we were doing and backtrack to some higher level anyway.
		// Well, OK, but we call this function from exception handlers...
		THROW_CHECK1(invalid_argument, m_file_size, m_file_size >= 0);
		// THROW_CHECK2(invalid_argument, m_file_size, m_end, m_end <= m_file_size || m_end == numeric_limits<off_t>::max());
	}
	return m_file_size;
}

off_t
BeesFileRange::grow_end(off_t delta)
{
	THROW_CHECK1(invalid_argument, delta, delta > 0);
	m_end = min(m_end + delta, file_size());
	THROW_CHECK2(runtime_error, m_file_size, m_end, m_end <= m_file_size);
	return m_end;
}

off_t
BeesFileRange::grow_begin(off_t delta)
{
	THROW_CHECK1(invalid_argument, delta, delta > 0);
	m_begin -= min(delta, m_begin);
	return m_begin;
}

off_t
BeesFileRange::shrink_begin(off_t delta)
{
	THROW_CHECK1(invalid_argument, delta, delta > 0);
	THROW_CHECK3(invalid_argument, delta, m_begin, m_end, delta + m_begin < m_end);
	m_begin += delta;
	return m_begin;
}

off_t
BeesFileRange::shrink_end(off_t delta)
{
	THROW_CHECK1(invalid_argument, delta, delta > 0);
	THROW_CHECK2(invalid_argument, delta, m_end, m_end >= delta);
	m_end -= delta;
	return m_end;
}

BeesFileRange::BeesFileRange(const BeesBlockData &bbd) :
	m_fd(bbd.fd()),
	m_begin(bbd.begin()),
	m_end(bbd.end())
{
}

BeesFileRange::BeesFileRange(Fd fd, off_t begin, off_t end) :
	m_fd(fd),
	m_begin(begin),
	m_end(end)
{
}

BeesFileRange::BeesFileRange(const BeesFileId &fid, off_t begin, off_t end) :
	m_fid(fid),
	m_begin(begin),
	m_end(end)
{
}

bool
BeesFileRange::is_same_file(const BeesFileRange &that) const
{
	// If we have two FDs, start by comparing those
	if (!!m_fd && !!that.m_fd && m_fd == that.m_fd) {
		return true;
	}
	// OK have to go fetch the fid from both files and compare them
	return fid() == that.fid();
}

bool
BeesFileRange::overlaps(const BeesFileRange &that) const
{
	// Determine whether the byte ranges overlap before doing syscalls on file descriptors

	pair<uint64_t, uint64_t> a(m_begin, m_end);
	pair<uint64_t, uint64_t> b(that.m_begin, that.m_end);

	// range a starts lower than or equal b
	if (b.first < a.first) {
		swap(a, b);
	}

	// if b starts within a, they overlap
	// (and the intersecting region is b.first..min(a.second, b.second))
	// (and the union region is a.first..max(a.second, b.second))
	if (b.first >= a.first && b.first < a.second) {
		return is_same_file(that);
	}

	return false;
}

BeesFileRange::operator BeesBlockData() const
{
	BEESTRACE("operator BeesBlockData " << *this);
	return BeesBlockData(m_fd, m_begin, m_end - m_begin);
}

Fd
BeesFileRange::fd() const
{
	return m_fd;
}

Fd
BeesFileRange::fd(const shared_ptr<BeesContext> &ctx)
{
	// If we don't have a fid we can't do much here
	if (m_fid) {
		if (!m_fd) {
			// If we don't have a fd, open by fid
			if (m_fid && ctx) {
				Fd new_fd = ctx->roots()->open_root_ino(m_fid);
				m_fd = new_fd;
			}
		} else {
			// If we have both fid and fd, make sure they match
			BeesFileId fd_fid(m_fd);
			THROW_CHECK2(invalid_argument, fd_fid, m_fid, fd_fid == m_fid);
		}
	}
	// We either had a fid and opened it, or we didn't and we're just stuck with our fd
	return m_fd;
}

BeesFileRange
BeesFileRange::copy_closed() const
{
	return BeesFileRange(fid(), m_begin, m_end);
}

BeesFileId
BeesFileRange::fid() const
{
	if (!m_fid) {
		if (!!m_fd) {
			m_fid = BeesFileId(m_fd);
		}
	}
	return m_fid;
}

BeesRangePair::BeesRangePair(const BeesFileRange &src, const BeesFileRange &dst) :
	pair<BeesFileRange, BeesFileRange>(src, dst)
{
	BEESTRACE("checking constraints on " << *this);

	// Must not initially overlap
	THROW_CHECK2(invalid_argument, first, second, !first.overlaps(second));

	// Must initially be equal
	THROW_CHECK2(invalid_argument, first, second, first.size() == second.size());

	// Can't check content unless open
	if (!first.fd() || !second.fd()) {
		return;
	}

	// Must check every block individually
	off_t first_begin = first.begin();
	off_t second_begin = second.begin();
	off_t size = first.size();
	while (size) {
		off_t len = min(BLOCK_SIZE_SUMS, size);
		BeesBlockData first_bbd(first.fd(), first_begin, len);
		BeesBlockData second_bbd(second.fd(), second_begin, len);
		THROW_CHECK2(invalid_argument, first_bbd, second_bbd, first_bbd.is_data_equal(second_bbd));
		first_begin += len;
		second_begin += len;
		size -= len;
	}
}

bool
BeesRangePair::operator<(const BeesRangePair &that) const
{
	// Order by destination then source
	return tie(second, first) < tie(that.second, that.first);
}

bool
BeesRangePair::grow(shared_ptr<BeesContext> ctx, bool constrained)
{
	BEESTOOLONG("grow constrained = " << constrained << " *this = " << *this);
	BEESTRACE("grow constrained = " << constrained << " *this = " << *this);
	bool rv = false;
	Timer grow_backward_timer;

	THROW_CHECK1(invalid_argument, first.begin(), (first.begin() & BLOCK_MASK_CLONE) == 0);
	THROW_CHECK1(invalid_argument, second.begin(), (second.begin() & BLOCK_MASK_CLONE) == 0);

	// We should not be overlapping already
	THROW_CHECK2(invalid_argument, first, second, !first.overlaps(second));

	BtrfsExtentWalker ew_second(second.fd());

	// Stop on aligned extent boundary
	ew_second.seek(second.begin());

	Extent e_second = ew_second.current();
	BEESTRACE("e_second " << e_second);

	// Preread entire extent
	bees_readahead_pair(second.fd(), e_second.begin(), e_second.size(),
			    first.fd(), e_second.begin() + first.begin() - second.begin(), e_second.size());

	auto hash_table = ctx->hash_table();

	// Look backward
	BEESTRACE("grow_backward " << *this);
	while (first.size() < BLOCK_SIZE_MAX_EXTENT) {
		if (second.begin() <= e_second.begin()) {
#if 0
			if (constrained) {
				break;
			}
			BEESCOUNT(pairbackward_extent);
			ew_second.seek(second.begin() - min(BLOCK_SIZE_CLONE, second.begin()));
			e_second = ew_second.current();
			if (e_second.flags() & Extent::HOLE) {
				BEESCOUNT(pairbackward_hole);
				break;
			}
			bees_readahead(second.fd(), e_second.begin(), e_second.size());
#else
			// This tends to repeatedly process extents that were recently processed.
			// We tend to catch duplicate blocks early since we scan them forwards.
			// Also, reading backwards is slow so we probably don't want to do it much.
			break;
#endif
		}
		BEESCOUNT(pairbackward_try);

		// Extend first range.  If we hit BOF we can go no further.
		BeesFileRange new_first = first;
		BEESTRACE("new_first = " << new_first);
		new_first.grow_begin(BLOCK_SIZE_CLONE);
		if (new_first.begin() == first.begin()) {
			BEESCOUNT(pairbackward_bof_first);
			break;
		}

		// Extend second range.  If we hit BOF we can go no further.
		BeesFileRange new_second = second;
		BEESTRACE("new_second = " << new_second);
		new_second.grow_begin(BLOCK_SIZE_CLONE);
		if (new_second.begin() == second.begin()) {
			BEESCOUNT(pairbackward_bof_second);
			break;
		}

		// If the ranges now overlap we went too far
		if (new_first.overlaps(new_second)) {
			BEESCOUNT(pairbackward_overlap);
			break;
		}

		BEESTRACE("first " << first << " new_first " << new_first);
		BeesBlockData first_bbd(first.fd(), new_first.begin(), first.begin() - new_first.begin());
		BEESTRACE("first_bbd " << first_bbd);
		BEESTRACE("second " << second << " new_second " << new_second);
		BeesBlockData second_bbd(second.fd(), new_second.begin(), second.begin() - new_second.begin());
		BEESTRACE("second_bbd " << second_bbd);

		// Both blocks must have identical content
		if (!first_bbd.is_data_equal(second_bbd)) {
			BEESCOUNT(pairbackward_miss);
			break;
		}

		// Physical blocks must be distinct
		if (first_bbd.addr().get_physical_or_zero() == second_bbd.addr().get_physical_or_zero()) {
			BEESCOUNT(pairbackward_same);
			break;
		}

		// Source block cannot be zero in a non-compressed non-magic extent
		BeesAddress first_addr(first.fd(), new_first.begin());
		if (first_bbd.is_data_zero() && !first_addr.is_magic() && !first_addr.is_compressed()) {
			BEESCOUNT(pairbackward_zero);
			break;
		}

		// Source block cannot have a toxic hash
		auto found_hashes = hash_table->find_cell(first_bbd.hash());
		bool found_toxic = false;
		for (auto i : found_hashes) {
			if (BeesAddress(i.e_addr).is_toxic()) {
				found_toxic = true;
				break;
			}
		}
		if (found_toxic) {
			BEESLOGWARN("WORKAROUND: found toxic hash in " << first_bbd << " while extending backward:\n" << *this);
			BEESCOUNT(pairbackward_toxic_hash);
			break;
		}

		THROW_CHECK2(invalid_argument, new_first.size(), new_second.size(), new_first.size() == new_second.size());
		first = new_first;
		second = new_second;
		rv = true;
		BEESCOUNT(pairbackward_hit);
	}
	BEESCOUNT(pairbackward_stop);
	BEESCOUNTADD(pairbackward_ms, grow_backward_timer.age() * 1000);

	// Look forward
	BEESTRACE("grow_forward " << *this);
	Timer grow_forward_timer;
	while (first.size() < BLOCK_SIZE_MAX_EXTENT) {
		if (second.end() >= e_second.end()) {
			if (constrained) {
				break;
			}
			BEESCOUNT(pairforward_extent);
			ew_second.seek(second.end());
			e_second = ew_second.current();
			if (e_second.flags() & Extent::HOLE) {
				BEESCOUNT(pairforward_hole);
				break;
			}
			bees_readahead(second.fd(), e_second.begin(), e_second.size());
		}
		BEESCOUNT(pairforward_try);

		// Extend first range.  If we hit EOF we can go no further.
		BeesFileRange new_first = first;
		BEESTRACE("new_first = " << new_first);
		new_first.grow_end(BLOCK_SIZE_CLONE);
		if (new_first.end() == first.end()) {
			BEESCOUNT(pairforward_eof_first);
			break;
		}

		// Extend second range.  If we hit EOF we can go no further.
		BeesFileRange new_second = second;
		BEESTRACE("new_second = " << new_second);
		new_second.grow_end(BLOCK_SIZE_CLONE);
		if (new_second.end() == second.end()) {
			BEESCOUNT(pairforward_eof_second);
			break;
		}

		// If we have hit an unaligned EOF then it has to be the same unaligned EOF.
		// If we haven't hit EOF then the ends of the ranges are still aligned,
		// so the misalignment (zero) will be equal.
		if ((new_second.end() & BLOCK_MASK_CLONE) != (new_first.end() & BLOCK_MASK_CLONE)) {
			BEESCOUNT(pairforward_eof_malign);
			break;
		}

		// If the ranges now overlap we went too far
		if (new_first.overlaps(new_second)) {
			BEESCOUNT(pairforward_overlap);
			break;
		}

		BEESTRACE("first " << first << " new_first " << new_first);
		BeesBlockData first_bbd(first.fd(), first.end(), new_first.end() - first.end());
		BEESTRACE("first_bbd " << first_bbd);
		BEESTRACE("second " << second << " new_second " << new_second);
		BeesBlockData second_bbd(second.fd(), second.end(), new_second.end() - second.end());
		BEESTRACE("second_bbd " << second_bbd);

		// Both blocks must have identical content
		if (!first_bbd.is_data_equal(second_bbd)) {
			BEESCOUNT(pairforward_miss);
			break;
		}

		// Physical blocks must be distinct
		if (first_bbd.addr().get_physical_or_zero() == second_bbd.addr().get_physical_or_zero()) {
			BEESCOUNT(pairforward_same);
			break;
		}

		// Source block cannot be zero in a non-compressed non-magic extent
		BeesAddress first_addr(first.fd(), new_first.begin());
		if (first_bbd.is_data_zero() && !first_addr.is_magic() && !first_addr.is_compressed()) {
			BEESCOUNT(pairforward_zero);
			break;
		}

		// Source block cannot have a toxic hash
		auto found_hashes = hash_table->find_cell(first_bbd.hash());
		bool found_toxic = false;
		for (auto i : found_hashes) {
			if (BeesAddress(i.e_addr).is_toxic()) {
				found_toxic = true;
				break;
			}
		}
		if (found_toxic) {
			BEESLOGWARN("WORKAROUND: found toxic hash in " << first_bbd << " while extending forward:\n" << *this);
			BEESCOUNT(pairforward_toxic_hash);
			break;
		}

		// OK, next block
		THROW_CHECK2(invalid_argument, new_first.size(), new_second.size(), new_first.size() == new_second.size());
		first = new_first;
		second = new_second;
		rv = true;
		BEESCOUNT(pairforward_hit);
	}

	if (first.overlaps(second)) {
		BEESLOGTRACE("after grow, first " << first << "\n\toverlaps " << second);
		BEESCOUNT(bug_grow_pair_overlaps);
	}

	BEESCOUNT(pairforward_stop);
	BEESCOUNTADD(pairforward_ms, grow_forward_timer.age() * 1000);
	return rv;
}

BeesRangePair
BeesRangePair::copy_closed() const
{
	return BeesRangePair(first.copy_closed(), second.copy_closed());
}

void
BeesRangePair::shrink_begin(off_t const delta)
{
	first.shrink_begin(delta);
	second.shrink_begin(delta);
	THROW_CHECK2(runtime_error, first.size(), second.size(), first.size() == second.size());
}

void
BeesRangePair::shrink_end(off_t const delta)
{
	first.shrink_end(delta);
	second.shrink_end(delta);
	THROW_CHECK2(runtime_error, first.size(), second.size(), first.size() == second.size());
}

ostream &
operator<<(ostream &os, const BeesAddress &ba)
{
	if (ba.is_magic()) {
		enum {
			ZERO     = BeesAddress::MagicValue::ZERO,
			DELALLOC = BeesAddress::MagicValue::DELALLOC,
			HOLE     = BeesAddress::MagicValue::HOLE,
			UNUSABLE = BeesAddress::MagicValue::UNUSABLE,
		};
                static const bits_ntoa_table table[] = {
                        NTOA_TABLE_ENTRY_ENUM(ZERO),
                        NTOA_TABLE_ENTRY_ENUM(DELALLOC),
                        NTOA_TABLE_ENTRY_ENUM(HOLE),
                        NTOA_TABLE_ENTRY_ENUM(UNUSABLE),
                        NTOA_TABLE_ENTRY_END()
                };
		return os << bits_ntoa(static_cast<BeesAddress::Type>(ba), table);
	}

	auto gpz = ba.get_physical_or_zero();
	if (gpz == 0x1000) {
		os << "NIL";
	} else {
		os << to_hex(gpz);
	}

	if (ba.is_toxic()) {
		os << "t";
	}

	if (ba.is_unaligned_eof()) {
		os << "u";
	}

	if (ba.is_compressed()) {
		os << "z";
		if (ba.has_compressed_offset()) {
			os << astringprintf("%" PRIx64, ba.get_compressed_offset());
		}
	}

	return os;
}

bool
BeesAddress::magic_check(uint64_t flags)
{
	// This one isn't FIEMAP
	if (flags & Extent::HOLE) {
		m_addr = HOLE;
		BEESCOUNT(addr_hole);
		return true;
	}

	// These trigger extra processing steps for compressed extents
	static const unsigned compressed_flags = FIEMAP_EXTENT_ENCODED;

	// These indicate the extent is not yet on disk (try again with sync)
	static const unsigned delalloc_flags = FIEMAP_EXTENT_UNKNOWN | FIEMAP_EXTENT_DELALLOC;

	// These flags are irrelevant to extent-same
	static const unsigned ignore_flags = FIEMAP_EXTENT_LAST | FIEMAP_EXTENT_SHARED;

	// These flags mean we can't use extent-same
	static const unsigned unusable_flags = FIEMAP_EXTENT_NOT_ALIGNED | FIEMAP_EXTENT_DATA_INLINE;

	// All of the above (any other flag is a new feature we maybe can't cope with)
	static const unsigned recognized_flags = compressed_flags | delalloc_flags | ignore_flags | unusable_flags;

	if (flags & ~recognized_flags) {
		BEESLOGTRACE("Unrecognized flags in " << fiemap_extent_flags_ntoa(flags));
		m_addr = UNUSABLE;
		// maybe we throw here?
		BEESCOUNT(addr_unrecognized);
		return true;
	}

	if (flags & unusable_flags) {
		// we know these, but can't touch them
		BEESCOUNT(addr_unusable);
		m_addr = UNUSABLE;
		return true;
	}

	if (flags & delalloc_flags) {
		// delayed allocation, try again with force
		BEESCOUNT(addr_delalloc);
		m_addr = DELALLOC;
		return true;
	}

	return false;
}

BeesAddress::BeesAddress(const Extent &e, off_t offset) :
	m_addr(ZERO)
{
	BEESTRACE("BeesAddress " << e << " offset " << to_hex(offset));
	Type new_addr = 0;

	THROW_CHECK1(invalid_argument, e, (e.physical() & BLOCK_MASK_CLONE) == 0);
	THROW_CHECK1(invalid_argument, e, (e.begin() & BLOCK_MASK_CLONE) == 0);
	THROW_CHECK1(invalid_argument, e, (offset & BLOCK_MASK_CLONE) == 0);
	THROW_CHECK1(invalid_argument, e, e.end() > e.begin());

	if (magic_check(e.flags())) {
		BEESCOUNT(addr_magic);
		return;
	}

	// All addresses from here on are physical
	THROW_CHECK1(invalid_argument, e, e.physical() > 0);

	if (e.flags() & FIEMAP_EXTENT_ENCODED) {
		THROW_CHECK1(invalid_argument, e, (e.offset() & BLOCK_MASK_CLONE) == 0);
		THROW_CHECK1(invalid_argument, e, e.offset() >= 0 && e.offset() < BLOCK_SIZE_MAX_COMPRESSED_EXTENT);
		int extent_offset = offset - e.begin() + e.offset();
		BEESTRACE("extent_offset = " << to_hex(extent_offset));
		THROW_CHECK1(invalid_argument, extent_offset, extent_offset >= 0 && extent_offset < BLOCK_SIZE_MAX_COMPRESSED_EXTENT);
		THROW_CHECK1(invalid_argument, extent_offset, (extent_offset & BLOCK_MASK_CLONE) == 0);
		unsigned offset_bits = (extent_offset / BLOCK_SIZE_CLONE) + 1;
		BEESTRACE("offset_bits = " << offset_bits);
		THROW_CHECK1(invalid_argument, offset_bits, offset_bits >= c_offset_min && offset_bits <= c_offset_max);
		THROW_CHECK1(invalid_argument, offset_bits, (offset_bits & ~c_offset_mask) == 0);
#if 1
		new_addr = e.physical() | c_compressed_mask | offset_bits;
		BEESCOUNT(addr_compressed_offset);
#else
		new_addr = e.physical() | c_compressed_mask;
		BEESCOUNT(addr_compressed);
#endif
	} else {
		new_addr = e.physical() + (offset - e.begin());
		BEESCOUNT(addr_uncompressed);
	}

	if ((e.flags() & FIEMAP_EXTENT_LAST) && (e.end() & BLOCK_MASK_CLONE) != 0 && (offset & ~BLOCK_MASK_CLONE) == (e.end() & ~BLOCK_MASK_CLONE)) {
		new_addr |= c_eof_mask;
		BEESCOUNT(addr_eof_e);
	}

	m_addr = new_addr;
	BEESCOUNT(addr_block);
}

BeesAddress::BeesAddress(int fd, off_t offset) :
	m_addr(ZERO)
{
	BEESTOOLONG("BeesAddress(fd " << fd << " " << name_fd(fd) << " offset " << to_hex(offset) << ")");
	BEESTRACE("BeesAddress(fd " << fd << " " << name_fd(fd) << " offset " << to_hex(offset) << ")");

	Type uoffset = ranged_cast<Type>(offset);

	THROW_CHECK1(invalid_argument, uoffset, (uoffset & c_all_mask) == 0);
	THROW_CHECK1(invalid_argument, uoffset, (uoffset & BLOCK_MASK_CLONE) == 0);

	Timer extentwalker_timer;
	BtrfsExtentWalker ew(fd, uoffset);
	Extent e = ew.current();
	BEESCOUNT(addr_from_fd);
	BEESCOUNTADD(addr_ms, extentwalker_timer.age() * 1000);

	*this = BeesAddress(e, offset);
}

BeesAddress::BeesAddress(int fd, off_t offset, shared_ptr<BeesContext> ctx) :
	m_addr(ZERO)
{
	BEESTOOLONG("BeesAddress(fd " << fd << " " << name_fd(fd) << " offset " << to_hex(offset) << " ctx " << ctx->root_path() << ")");
	BEESTRACE("BeesAddress(fd " << fd << " " << name_fd(fd) << " offset " << to_hex(offset) << " ctx " << ctx->root_path() << ")");

	Type uoffset = ranged_cast<Type>(offset);

	THROW_CHECK1(invalid_argument, uoffset, (uoffset & c_all_mask) == 0);
	THROW_CHECK1(invalid_argument, uoffset, (uoffset & BLOCK_MASK_CLONE) == 0);

	Timer extentwalker_timer;
	BtrfsExtentWalker ew(fd, uoffset, ctx->root_fd());
	Extent e = ew.current();
	BEESCOUNT(addr_from_root_fd);
	BEESCOUNTADD(addr_ms, extentwalker_timer.age() * 1000);

	*this = BeesAddress(e, offset);
}

// Get just the physical address with no extra bits or compressed block offset (magic values become zero)

BeesAddress::Type
BeesAddress::get_physical_or_zero() const
{
	if (is_magic()) {
		return 0;
	} else {
		return m_addr & ~c_all_mask;
	}
}

// A compressed block address is divided into two fields:
// the beginning of the physical extent,
// and the distance (in CLONE blocks) from the start of the extent to the current block.
// Throws an exception if has_compressed_offset is not true.

BeesAddress::Type
BeesAddress::get_compressed_offset() const
{
	THROW_CHECK1(invalid_argument, *this, has_compressed_offset());
	return ((m_addr & c_offset_mask) - 1) * BLOCK_SIZE_CLONE;
}

void
BeesAddress::set_toxic()
{
	THROW_CHECK1(invalid_argument, *this, !is_magic());
	m_addr |= c_toxic_mask;
}

bool
BeesAddress::operator==(const BeesAddress &that) const
{
	// If one side has an offset and the other doesn't, compare without checking offset bits
	// This returns the right result for comparisons between magic and non-magic values,
	// even though the math is all wrong.
	if (has_compressed_offset() != that.has_compressed_offset()) {
		return (m_addr & ~c_offset_mask) == (that.m_addr & ~c_offset_mask);
	} else {
		return m_addr == that.m_addr;
	}
}

bool
BeesAddress::operator<(const BeesAddress &that) const
{
	if (has_compressed_offset() != that.has_compressed_offset()) {
		return (m_addr & ~c_offset_mask) < (that.m_addr & ~c_offset_mask);
	} else {
		return m_addr < that.m_addr;
	}
}

ostream &
operator<<(ostream &os, const BeesBlockData &bbd)
{
	os << "BeesBlockData { " << pretty(bbd.m_length) << " " << to_hex(bbd.m_offset) << " fd = " << bbd.m_fd << " '" << name_fd(bbd.m_fd) << "'";
	if (bbd.m_addr != BeesAddress::ZERO) {
		os << ", address = " << bbd.m_addr;
	}
	if (bbd.m_hash_done) {
		os << ", hash = " << bbd.m_hash;
	}
	if (!bbd.m_data.empty()) {
// Turn this on to debug BeesBlockData, but leave it off otherwise.
// It's a massive data leak that is only interesting to developers.
#if 0
		os << ", data[" << bbd.m_data.size() << "] = '";

		size_t max_print = 12;
		size_t to_print = min(bbd.m_data.size(), max_print);
		for (size_t i = 0; i < to_print; ++i) {
			uint8_t c = bbd.m_data[i];
			// We are ASCII heathens here
			if (c >= 32 && c < 127 && c != '\\') {
				os << c;
			} else {
				char buf[8];
				sprintf(buf, "\\x%02x", c);
				os << buf;
			}
		}
		os << "...'";
#else
		os << ", data[" << bbd.m_data.size() << "]";
#endif
	}
	return os << " }";
}

BeesBlockData::BeesBlockData(Fd fd, off_t offset, size_t read_length) :
	m_fd(fd),
	m_offset(offset),
	m_length(read_length)
{
	BEESTRACE("Constructing " << *this);
	THROW_CHECK1(invalid_argument, m_length, m_length > 0);
	THROW_CHECK1(invalid_argument, m_length, m_length <= BLOCK_SIZE_SUMS);
	THROW_CHECK1(invalid_argument, m_offset, (m_offset % BLOCK_SIZE_SUMS) == 0);
}

BeesBlockData::BeesBlockData() :
	m_offset(0),
	m_length(0)
{
}

BeesAddress
BeesBlockData::addr() const
{
	if (m_addr == BeesAddress::ZERO) {
		m_addr = BeesAddress(fd(), m_offset);
	}
	return m_addr;
}

BeesBlockData &
BeesBlockData::addr(const BeesAddress &a)
{
	m_addr = a;
	return *this;
}

const BeesBlockData::Blob &
BeesBlockData::data() const
{
	if (m_data.empty()) {
		THROW_CHECK1(invalid_argument, size(), size() > 0);
		BEESNOTE("Reading BeesBlockData " << *this);
		BEESTOOLONG("Reading BeesBlockData " << *this);
		Timer read_timer;

		Blob rv(size());
		pread_or_die(m_fd, rv, m_offset);
		THROW_CHECK2(runtime_error, rv.size(), size(), ranged_cast<off_t>(rv.size()) == size());
		m_data = rv;
		BEESCOUNT(block_read);
		BEESCOUNTADD(block_bytes, rv.size());
		BEESCOUNTADD(block_ms, read_timer.age() * 1000);
	}

	return m_data;
}

BeesHash
BeesBlockData::hash() const
{
	if (!m_hash_done) {
		// We can only dedupe unaligned EOF blocks against other unaligned EOF blocks,
		// so we do NOT round up to a full sum block size.
		const Blob &blob = data();
		m_hash = BeesHash(blob.data(), blob.size());
		m_hash_done = true;
		BEESCOUNT(block_hash);
	}

	return m_hash;
}

bool
BeesBlockData::is_data_zero() const
{
	// The CRC64 of zero is zero, so skip some work if we already know the CRC
	// ...but that doesn't work for any other hash function, and it
	// saves us next to nothing.

	// OK read block (maybe) and check every byte
	for (auto c : data()) {
		if (c != '\0') {
			return false;
		}
	}

	BEESCOUNT(block_zero);
	return true;
}

bool
BeesBlockData::is_data_equal(const BeesBlockData &that) const
{
	BEESTRACE("is_data_equal this = " << *this << ", that = " << that);
	THROW_CHECK1(invalid_argument, size(), size() > 0);
	THROW_CHECK2(invalid_argument, size(), that.size(), size() == that.size());

	// skip some work if we already know the CRCs don't match
	if (m_hash_done && that.m_hash_done && m_hash != that.m_hash) {
		return false;
	}

	return data() == that.data();
}

