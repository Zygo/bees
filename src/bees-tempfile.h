#pragma once

/// @file bees-tempfile.h
/// Temporary-file management for partial-extent deduplication staging.

#include "bees-fwd.h"

#include "crucible/fd.h"

#include <memory>

#include <cstdint>

using namespace crucible;
using namespace std;

/// Configuration for a BeesTempFile: controls COW and compression attributes
/// applied to the temporary file at creation time.
struct BeesTempFileConfig {
	/// If false, disable copy-on-write (NOCOW inode flag) on the temp file.
	bool m_datacow = true;
	/// Compression setting to apply; "yes" lets btrfs choose the algorithm.
	string m_compress = "yes";
};

/**
 * Temporary file used to stage data during partial-extent deduplication.
 *
 * When a dedup operation cannot cover a full extent (e.g. the source and
 * destination ranges are not aligned to extent boundaries), bees copies the
 * relevant data into a temporary file first, then deduplicates the temporary
 * file against the destination.  BeesTempFile manages the lifetime of that
 * staging file and is recycled via a Pool in BeesContext.
 */
class BeesTempFile {
	shared_ptr<BeesContext> m_ctx;
	shared_ptr<BeesRoots>   m_roots;
	Fd			m_fd;
	/// Current logical end of used data within the temp file.
	off_t			m_end_offset;
	/// Saved inode flags (FS_IOC_GETFLAGS) used to restore state after reset.
	uint32_t		m_iflags;

	/// Align m_end_offset up to the clone-alignment boundary.
	void realign();
	/// Extend or truncate the file to @p new_end_offset bytes.
	void resize(off_t new_end_offset);

public:
	~BeesTempFile();
	/// Construct a new temp file under @p ctx with the given configuration.
	BeesTempFile(shared_ptr<BeesContext> ctx, const BeesTempFileConfig& conf = BeesTempFileConfig());

	/// Append a hole of @p count bytes and return the range covering it.
	BeesFileRange make_hole(off_t count);
	/// Copy data from @p src into the temp file and return the corresponding range.
	BeesFileRange make_copy(const BeesFileRange &src);
	/// Reset the temp file to an empty state so it can be reused from the pool.
	void reset();
};

