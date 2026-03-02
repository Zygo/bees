#ifndef CRUCIBLE_EXTENTWALKER_H
#define CRUCIBLE_EXTENTWALKER_H

#include "crucible/fd.h"

namespace crucible {
	using namespace std;

	// FIXME:  ExtentCursor is probably a better name
	/// Description of a single file extent as reported by FIEMAP or the btrfs tree.
	/// Logical range is [m_begin, m_end).  The HOLE, PREALLOC, and OBSCURED flags
	/// extend the standard FIEMAP flag set using bits 32 and above.
	struct Extent {
		off_t		m_begin = 0;
		off_t		m_end = 0;
		uint64_t	m_physical = 0;
		uint64_t	m_flags = 0;

		// Btrfs extent reference details
		off_t		m_physical_len = 0;
		off_t		m_logical_len = 0;
		off_t		m_offset = 0;

		// fiemap flags are uint32_t, so bits 32..63 are OK for us

		// no extent here
		static const uint64_t HOLE      = (1ULL << 32);

		// extent is physical space full of zeros
		static const uint64_t PREALLOC  = (1ULL << 33);

		// extent's physical (RAM) size does not match logical (can we know this?)
		static const uint64_t OBSCURED  = (1ULL << 34);

		/// True if this extent is non-empty (has a valid range).
		operator bool() const;
		/// Logical byte length of this extent.
		off_t size() const;
		off_t begin() const { return m_begin; }
		off_t end() const { return m_end; }
		uint64_t flags() const { return m_flags; }
		uint64_t physical() const { return m_physical; }
		off_t physical_len() const { return m_physical_len; }
		off_t logical_len() const { return m_logical_len; }
		/// Offset of the logical range within the physical extent (non-zero for shared/compressed extents).
		off_t offset() const { return m_offset; }
		/// True if the extent uses btrfs compression.
		bool compressed() const;
		/// Physical block address of the extent data on disk.
		uint64_t bytenr() const;
		bool operator==(const Extent &that) const;
		bool operator!=(const Extent &that) const { return !(*this == that); }
	};

	/// @deprecated Use the btrfs tree classes in btrfs-tree.h directly instead.
	/// ExtentWalker is kept for compatibility but carries FIEMAP legacy
	/// that complicates the implementation.  New code should not use it.
	///
	/// Cursor for iterating over the extents of an open file.
	/// Wraps FIEMAP queries with a cache and provides forward/backward
	/// navigation and random-access seeking by file offset.
	class ExtentWalker {
	public:
		using Vec = vector<Extent>;
		using Itr = Vec::iterator;

	protected:
		Fd	m_fd;
		Stat	m_stat;

		virtual Vec get_extent_map(off_t pos);

	private:
		Vec	m_extents;
		Itr	m_current;

		Itr find_in_cache(off_t pos);
		void run_fiemap(off_t pos);

#ifdef EXTENTWALKER_DEBUG
		ostringstream m_log;
#endif

	public:
		/// Construct an ExtentWalker positioned at offset 0.
		ExtentWalker(Fd fd = Fd());
		/// Construct an ExtentWalker positioned at @p initial_pos.
		ExtentWalker(Fd fd, off_t initial_pos);
		virtual ~ExtentWalker();

		/// Reset the cursor to offset 0.
		void reset();
		/// Return the extent at the current cursor position.
		Extent current();
		/// Advance the cursor to the next extent.  Returns false at end of file.
		bool next();
		/// Move the cursor to the previous extent.  Returns false at start of file.
		bool prev();
		/// Move the cursor to the extent covering @p new_pos.
		void seek(off_t new_pos);

	friend ostream & operator<<(ostream &os, const ExtentWalker &ew);
	};

	/// ExtentWalker that uses the btrfs EXTENT_DATA tree instead of FIEMAP,
	/// providing more accurate extent information for btrfs files.
	class BtrfsExtentWalker : public ExtentWalker {
		uint64_t m_tree_id;
		Fd m_root_fd;

	protected:
		Vec get_extent_map(off_t pos) override;

	public:
		BtrfsExtentWalker(Fd fd);
		BtrfsExtentWalker(Fd fd, off_t initial_pos);
		/// Construct with an explicit @p root_fd for the filesystem root (used for tree lookups).
		BtrfsExtentWalker(Fd fd, off_t initial_pos, Fd root_fd);
		/// Override the filesystem root fd used for tree lookups.
		void set_root_fd(Fd fd);
	};

	ostream &operator<<(ostream &os, const Extent &e);
};

#endif // CRUCIBLE_EXTENTWALKER_H
