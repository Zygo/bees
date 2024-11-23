#include "bees.h"

#include "crucible/limits.h"
#include "crucible/string.h"

using namespace crucible;
using namespace std;

BeesAddress
BeesResolver::addr(BeesAddress new_addr)
{
	THROW_CHECK1(invalid_argument, new_addr, !new_addr.is_magic());

	m_found_data = false;
	m_found_dup = false;
	m_found_hash = false;
	m_wrong_data = false;
	m_biors.clear();
	m_ranges.clear();
	m_addr = new_addr;
	m_bior_count = 0;

	auto rv = m_ctx->resolve_addr(m_addr);
	m_biors = rv.m_biors;
	m_is_toxic = rv.m_is_toxic;
	m_bior_count = m_biors.size();

	return m_addr;
}

BeesResolver::BeesResolver(shared_ptr<BeesContext> ctx, BeesAddress new_addr) :
	m_ctx(ctx),
	m_bior_count(0)
{
	addr(new_addr);
}

BeesBlockData
BeesResolver::adjust_offset(const BeesFileRange &haystack, const BeesBlockData &needle)
{
	BEESTRACE("Searching for needle " << needle << "\n\tin haystack " << haystack);

	BEESCOUNT(adjust_try);

	// Constraint checks
	THROW_CHECK1(invalid_argument, needle.begin(), (needle.begin() & BLOCK_MASK_CLONE) == 0);
	THROW_CHECK1(invalid_argument, haystack.begin(), (haystack.begin() & BLOCK_MASK_CLONE) == 0);

	// Need to know the precise dimensions of the haystack and needle
	off_t haystack_size = haystack.file_size();

	// If the needle is not a full block then it can only match at EOF
	off_t needle_len = needle.size();
	bool is_unaligned_eof = needle_len & BLOCK_MASK_CLONE;
	BEESTRACE("is_unaligned_eof = " << is_unaligned_eof << ", needle_len = " << to_hex(needle_len) << ", haystack_size = " << to_hex(haystack_size));

	// Unaligned EOF can only match at EOF, so only check there
	if (is_unaligned_eof) {
		BEESTRACE("Construct needle_bfr from " << needle);
		BeesFileRange needle_bfr(needle);

		// Census
		if (haystack_size & BLOCK_MASK_CLONE) {
			BEESCOUNT(adjust_eof_haystack);
		}
		if (needle_bfr.end() & BLOCK_MASK_CLONE) {
			BEESCOUNT(adjust_eof_needle);
		}

		// Non-aligned part of the lengths must be the same
		if ( (haystack_size & BLOCK_MASK_CLONE) != (needle_bfr.end() & BLOCK_MASK_CLONE) ) {
			BEESCOUNT(adjust_eof_fail);
			return BeesBlockData();
		}

		// Read the haystack block
		BEESTRACE("Reading haystack (haystack_size = " << to_hex(haystack_size) << ")");
		BeesBlockData straw(haystack.fd(), haystack_size & ~BLOCK_MASK_CLONE, haystack_size & BLOCK_MASK_CLONE);

		// It either matches or it doesn't
		BEESTRACE("Verifying haystack " << straw);
		if (straw.is_data_equal(needle)) {
			BEESCOUNT(adjust_eof_hit);
			m_found_data = true;
			m_found_hash = true;
			return straw;
		}

		// Check for matching hash
		BEESTRACE("Verifying haystack hash");
		if (straw.hash() == needle.hash()) {
			// OK at least the hash is still valid
			m_found_hash = true;
		}

		BEESCOUNT(adjust_eof_miss);
		// BEESLOG("adjust_eof_miss " << straw);
		return BeesBlockData();
	}

	off_t haystack_offset = haystack.begin();
	bool is_compressed_offset = false;
	bool is_exact = false;
	if (m_addr.is_compressed()) {
		BtrfsExtentWalker ew(haystack.fd(), haystack.begin(), m_ctx->root_fd());
		BEESTRACE("haystack extent data " << ew);
		Extent e = ew.current();
		THROW_CHECK1(runtime_error, m_addr, m_addr.has_compressed_offset());
		off_t coff = m_addr.get_compressed_offset();
		if (e.offset() > coff) {
			// this extent begins after the target block
			BEESCOUNT(adjust_offset_low);
			return BeesBlockData();
		}
		coff -= e.offset();
		if (e.size() <= coff) {
			// this extent ends before the target block
			BEESCOUNT(adjust_offset_high);
			return BeesBlockData();
		}
		haystack_offset = e.begin() + coff;
		BEESCOUNT(adjust_offset_hit);
		is_compressed_offset = true;
	} else {
		BEESCOUNT(adjust_exact);
		is_exact = true;
	}

	BEESTRACE("Checking haystack " << haystack << " offset " << to_hex(haystack_offset));

	// Check all the blocks in the list
	THROW_CHECK1(out_of_range, haystack_offset, (haystack_offset & BLOCK_MASK_CLONE) == 0);

	// Straw cannot extend beyond end of haystack
	if (haystack_offset + needle.size() > haystack_size) {
		BEESCOUNT(adjust_needle_too_long);
		return BeesBlockData();
	}

	// Read the haystack
	BEESTRACE("straw " << name_fd(haystack.fd()) << ", offset " << to_hex(haystack_offset) << ", length " << needle.size());
	BeesBlockData straw(haystack.fd(), haystack_offset, needle.size());

	BEESTRACE("straw = " << straw);

	// Stop if we find a match
	if (straw.is_data_equal(needle)) {
		BEESCOUNT(adjust_hit);
		m_found_data = true;
		m_found_hash = true;
		if (is_compressed_offset) BEESCOUNT(adjust_compressed_offset_correct);
		if (is_exact) BEESCOUNT(adjust_exact_correct);
		return straw;
	}

	if (straw.hash() != needle.hash()) {
		// Not the same hash or data, try next block
		BEESCOUNT(adjust_miss);
		return BeesBlockData();
	}

	// Found the hash but not the data.  Yay!
	m_found_hash = true;
#if 0
	BEESLOGINFO("HASH COLLISION\n"
		<< "\tneedle " << needle << "\n"
		<< "\tstraw " << straw);
#endif
	BEESCOUNT(hash_collision);

	// Ran out of offsets to try
	BEESCOUNT(adjust_no_match);
	if (is_compressed_offset) BEESCOUNT(adjust_compressed_offset_wrong);
	if (is_exact) BEESCOUNT(adjust_exact_wrong);
	m_wrong_data = true;
	return BeesBlockData();
}

BeesFileRange
BeesResolver::chase_extent_ref(const BtrfsInodeOffsetRoot &bior, BeesBlockData &needle_bbd)
{
	BEESTRACE("chase_extent_ref bior " << bior << " needle_bbd " << needle_bbd);
	BEESNOTE("chase_extent_ref bior " << bior << " needle_bbd " << needle_bbd);
	BEESCOUNT(chase_try);

	Fd file_fd = m_ctx->roots()->open_root_ino(bior.m_root, bior.m_inum);
	if (!file_fd) {
		// Deleted snapshots generate craptons of these
		// BEESLOGDEBUG("No FD in chase_extent_ref " << bior);
		BEESCOUNT(chase_no_fd);
		return BeesFileRange();
	}

	BEESNOTE("searching at offset " << to_hex(bior.m_offset) << " in file " << name_fd(file_fd) << "\n\tfor " << needle_bbd);

	BEESTRACE("bior file " << name_fd(file_fd));
	BEESTRACE("get file_addr " << bior);
	BeesAddress file_addr(file_fd, bior.m_offset, m_ctx);
	BEESTRACE("file_addr " << file_addr);

	// ...or are we?
	if (file_addr.is_magic()) {
		BEESLOGDEBUG("file_addr is magic: file_addr = " << file_addr << " bior = " << bior << " needle_bbd = " << needle_bbd);
		BEESCOUNT(chase_wrong_magic);
		return BeesFileRange();
	}
	THROW_CHECK1(invalid_argument, m_addr, !m_addr.is_magic());

	// Did we get the physical block we asked for?  The magic bits have to match too,
	// but the compressed offset bits do not.
	if (file_addr.get_physical_or_zero() != m_addr.get_physical_or_zero()) {
		// BEESLOGDEBUG("found addr " << file_addr << " at " << name_fd(file_fd) << " offset " << to_hex(bior.m_offset) << " but looking for " << m_addr);
		// FIEMAP/resolve are working, but the data is old.
		BEESCOUNT(chase_wrong_addr);
		return BeesFileRange();
	}

	// Calculate end of range, which is a sum block or less
	// It's a sum block because we have to compare content now
	off_t file_size = Stat(file_fd).st_size;
	off_t bior_offset = ranged_cast<off_t>(bior.m_offset);
	off_t end_offset = min(file_size, bior_offset + needle_bbd.size());
	BeesBlockData haystack_bbd(file_fd, bior_offset, end_offset - bior_offset);

	BEESTRACE("matched haystack_bbd " << haystack_bbd << " file_addr " << file_addr);

	// If the data was compressed and no offset was captured then
	// we won't get an exact address from resolve.
	// Search near the resolved address for a matching data block.
	// ...even if it's not compressed, we should do this sanity
	// check before considering the block as a duplicate candidate.
	// FIXME:  this is mostly obsolete now and we shouldn't do it here.
	// Don't bother fixing it because it will all go away with (extent, offset) reads.
	auto new_bbd = adjust_offset(haystack_bbd, needle_bbd);
	if (new_bbd.empty()) {
		// matching offset search failed
		BEESCOUNT(chase_no_data);
		return BeesFileRange();
	}
	if (new_bbd.begin() == haystack_bbd.begin()) {
		BEESCOUNT(chase_uncorrected);
	} else {
		// corrected the bfr
		BEESCOUNT(chase_corrected);
		haystack_bbd = new_bbd;
	}

	// We have found at least one duplicate block, so resolve was a success
	BEESCOUNT(chase_hit);

	// Matching block
	BEESTRACE("Constructing dst_bfr { " << BeesFileId(haystack_bbd.fd()) << ", " << to_hex(haystack_bbd.begin()) << ".." << to_hex(haystack_bbd.end()) << " }");
	BeesFileRange dst_bfr(BeesFileId(haystack_bbd.fd()), haystack_bbd.begin(), haystack_bbd.end());

	return dst_bfr;
}

void
BeesResolver::replace_src(const BeesFileRange &src_bfr)
{
	BEESTRACE("replace_src src_bfr " << src_bfr);
	THROW_CHECK0(runtime_error, !m_is_toxic);
	BEESCOUNT(replacesrc_try);

	// Open src, reuse it for all dst
	auto i_bfr = src_bfr;
	BEESNOTE("Opening src bfr " << i_bfr);
	BEESTRACE("Opening src bfr " << i_bfr);
	i_bfr.fd(m_ctx);

	BeesBlockData bbd(i_bfr);

	for_each_extent_ref(bbd, [&](const BeesFileRange &j) -> bool {
		// Open dst
		auto j_bfr = j;
		BEESNOTE("Opening dst bfr " << j_bfr);
		BEESTRACE("Opening dst bfr " << j_bfr);
		j_bfr.fd(m_ctx);

		if (i_bfr.overlaps(j_bfr)) {
			BEESCOUNT(replacesrc_overlaps);
			return false; // i.e. continue
		}

		// Make pair(src, dst)
		BEESTRACE("creating brp (" << i_bfr << ", " << j_bfr << ")");
		BeesRangePair brp(i_bfr, j_bfr);
		BEESTRACE("Found matching range: " << brp);

		// Extend range at beginning
		BEESNOTE("Extending matching range: " << brp);
		// No particular reason to be constrained?
		if (brp.grow(m_ctx, true)) {
			BEESCOUNT(replacesrc_grown);
		}

		// Dedup
		BEESNOTE("dedup " << brp);
		if (m_ctx->dedup(brp)) {
			BEESCOUNT(replacesrc_dedup_hit);
			m_found_dup = true;
		} else {
			BEESCOUNT(replacesrc_dedup_miss);
		}
		return false; // i.e. continue
	});
}

void
BeesResolver::find_matches(bool just_one, BeesBlockData &bbd)
{
	// Walk through the (ino, offset, root) tuples until we find a match.
	BEESTRACE("finding all matches for " << bbd << " at " << m_addr << ": " << m_biors.size() << " found");
	THROW_CHECK0(runtime_error, !m_is_toxic);
	bool stop_now = false;
	for (auto ino_off_root : m_biors) {
		if (m_wrong_data) {
			return;
		}

		BEESTRACE("ino_off_root " << ino_off_root);
		BeesFileId this_fid(ino_off_root.m_root, ino_off_root.m_inum);

		// Silently ignore blacklisted files, e.g. BeesTempFile files
		if (m_ctx->is_blacklisted(this_fid)) {
			continue;
		}

		// Look at the old data
		catch_all([&]() {
			BEESTRACE("chase_extent_ref ino " << ino_off_root << " bbd " << bbd);
			auto new_range = chase_extent_ref(ino_off_root, bbd);
			if (new_range) {
				m_ranges.insert(new_range.copy_closed());
				stop_now = true;
			}
		});

		if (just_one && stop_now) {
			break;
		}
	}
}

bool
BeesResolver::for_each_extent_ref(BeesBlockData bbd, function<bool(const BeesFileRange &bfr)> visitor)
{
	// Walk through the (ino, offset, root) tuples until we are told to stop
	BEESTRACE("for_each_extent_ref " << bbd << " at " << m_addr << ": " << m_biors.size() << " found");
	THROW_CHECK0(runtime_error, !m_is_toxic);
	bool stop_now = false;
	for (auto ino_off_root : m_biors) {
		BEESTRACE("ino_off_root " << ino_off_root);
		BeesFileId this_fid(ino_off_root.m_root, ino_off_root.m_inum);

		// Silently ignore blacklisted files, e.g. BeesTempFile files
		if (m_ctx->is_blacklisted(this_fid)) {
			continue;
		}

		// Look at the old data
		// FIXME:  propagate exceptions for now.  Proper fix requires a rewrite.
		// catch_all([&]() {
			BEESTRACE("chase_extent_ref ino " << ino_off_root << " bbd " << bbd);
			auto new_range = chase_extent_ref(ino_off_root, bbd);
			// XXX: should we catch visitor's exceptions here?
			if (new_range) {
				stop_now = visitor(new_range);
			} else {
				// We have reliable block addresses now, so we guarantee we can hit the desired block.
				// Failure in chase_extent_ref means we are done, and don't need to look up all the
				// other references.
				// Or...not?  If we have a compressed extent, some refs will not match
				// if there is are two references to the same extent with a reference
				// to a different extent between them.
				// stop_now = true;
			}
		// });

		if (stop_now) {
			break;
		}
	}
	return stop_now;
}

BeesRangePair
BeesResolver::replace_dst(const BeesFileRange &dst_bfr_in)
{
	BEESTRACE("replace_dst dst_bfr " << dst_bfr_in);
	BEESCOUNT(replacedst_try);

	// Open dst, reuse it for all src
	BEESNOTE("Opening dst bfr " << dst_bfr_in);
	BEESTRACE("Opening dst bfr " << dst_bfr_in);
	auto dst_bfr = dst_bfr_in;
	dst_bfr.fd(m_ctx);

	BeesFileRange overlap_bfr;
	BEESTRACE("overlap_bfr " << overlap_bfr);

	BeesBlockData bbd(dst_bfr);
	BeesRangePair rv = { BeesFileRange(), BeesFileRange() };

	for_each_extent_ref(bbd, [&](const BeesFileRange &src_bfr_in) -> bool {
		// Open src
		BEESNOTE("Opening src bfr " << src_bfr_in);
		BEESTRACE("Opening src bfr " << src_bfr_in);
		auto src_bfr = src_bfr_in;
		src_bfr.fd(m_ctx);

		if (dst_bfr.overlaps(src_bfr)) {
			BEESCOUNT(replacedst_overlaps);
			return false; // i.e. continue
		}

		// If dst is already occupying src, skip.
		// FIXME: BeesContext::scan_one_extent should be weeding these out, but does not.
		BeesBlockData src_bbd(src_bfr.fd(), src_bfr.begin(), min(BLOCK_SIZE_SUMS, src_bfr.size()));
		if (bbd.addr().get_physical_or_zero() == src_bbd.addr().get_physical_or_zero()) {
			BEESCOUNT(replacedst_same);
			// stop looping here, all the other srcs will probably fail this test too
			BeesTracer::set_silent();
			throw runtime_error("FIXME: too many duplicate candidates, bailing out here");
		}

		// Make pair(src, dst)
		BEESTRACE("creating brp (" << src_bfr << ", " << dst_bfr << ")");
		BeesRangePair brp(src_bfr, dst_bfr);
		BEESTRACE("Found matching range: " << brp);

		// Extend range at beginning
		BEESNOTE("Extending matching range: " << brp);
		// 'false' Has nasty loops, and may not be faster.
		// 'true' At best, keeps fragmentation constant...but can also make it worse
		if (brp.grow(m_ctx, true)) {
			BEESCOUNT(replacedst_grown);
		}

		rv = brp;
		m_found_dup = true;
		return true;
	});
	// BEESLOG("overlap_bfr after " << overlap_bfr);
	return rv;
}

BeesFileRange
BeesResolver::find_one_match(BeesBlockData &bbd)
{
	THROW_CHECK0(runtime_error, !m_is_toxic);
	find_matches(true, bbd);
	if (m_ranges.empty()) {
		return BeesFileRange();
	} else {
		return *m_ranges.begin();
	}
}

set<BeesFileRange>
BeesResolver::find_all_matches(BeesBlockData &bbd)
{
	THROW_CHECK0(runtime_error, !m_is_toxic);
	find_matches(false, bbd);
	return m_ranges;
}

bool
BeesResolver::operator<(const BeesResolver &that) const
{
	// Lowest count, highest address
	return tie(that.m_bior_count, m_addr) < tie(m_bior_count, that.m_addr);
}
