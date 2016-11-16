#ifndef CRUCIBLE_EXTENTWALKER_H
#define CRUCIBLE_EXTENTWALKER_H

#include "crucible/fd.h"

namespace crucible {
	using namespace std;

	// FIXME:  ExtentCursor is probably a better name
	struct Extent {
		off_t		m_begin;
		off_t		m_end;
		uint64_t	m_physical;
		uint64_t	m_flags;

		// Btrfs extent reference details
		off_t		m_physical_len;
		off_t		m_logical_len;
		off_t		m_offset;

		// fiemap flags are uint32_t, so bits 32..63 are OK for us

		// no extent here
		static const uint64_t HOLE      = (1ULL << 32);

		// extent is physical space full of zeros
		static const uint64_t PREALLOC  = (1ULL << 33);

		// extent's physical (RAM) size does not match logical (can we know this?)
		static const uint64_t OBSCURED  = (1ULL << 34);

		operator bool() const;
		off_t size() const;
		off_t begin() const { return m_begin; }
		off_t end() const { return m_end; }
		uint64_t flags() const { return m_flags; }
		uint64_t physical() const { return m_physical; }
		off_t physical_len() const { return m_physical_len; }
		off_t logical_len() const { return m_logical_len; }
		off_t offset() const { return m_offset; }
		bool operator==(const Extent &that) const;
		bool operator!=(const Extent &that) const { return !(*this == that); }

		Extent();
		Extent(const Extent &e) = default;
	};

	class ExtentWalker {
	public:
		using Vec = vector<Extent>;
		using Itr = Vec::iterator;

	protected:
		Fd	m_fd;
		Stat	m_stat;

		virtual Vec get_extent_map(off_t pos);

		static const unsigned sc_extent_fetch_max = 64;
		static const unsigned sc_extent_fetch_min = 4;
		static const off_t sc_step_size = 0x1000 * (sc_extent_fetch_max / 2);

	private:
		Vec	m_extents;
		Itr	m_current;

		Itr find_in_cache(off_t pos);
		void run_fiemap(off_t pos);

	public:
		ExtentWalker(Fd fd = Fd());
		ExtentWalker(Fd fd, off_t initial_pos);
		virtual ~ExtentWalker();

		void reset();
		Extent current();
		bool next();
		bool prev();
		void seek(off_t new_pos);

	friend ostream & operator<<(ostream &os, const ExtentWalker &ew);
	};

	class BtrfsExtentWalker : public ExtentWalker {
		uint64_t m_tree_id;
		Fd m_root_fd;

	protected:
		Vec get_extent_map(off_t pos) override;

	public:
		BtrfsExtentWalker(Fd fd);
		BtrfsExtentWalker(Fd fd, off_t initial_pos);
		BtrfsExtentWalker(Fd fd, off_t initial_pos, Fd root_fd);
		void set_root_fd(Fd fd);
	};

	ostream &operator<<(ostream &os, const Extent &e);
};

#endif // CRUCIBLE_EXTENTWALKER_H
