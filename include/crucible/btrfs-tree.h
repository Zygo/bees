#ifndef CRUCIBLE_BTRFS_TREE_H
#define CRUCIBLE_BTRFS_TREE_H

#include "crucible/fd.h"
#include "crucible/fs.h"
#include "crucible/bytevector.h"

namespace crucible {
	using namespace std;

	/// A single item returned by a btrfs kernel tree search.
	/// Carries the key triple (objectid, type, offset), the transaction ID,
	/// and the raw payload bytes.  Typed accessor methods decode the payload
	/// into the appropriate kernel structure; calling the wrong accessor for
	/// the item's type throws an exception.
	class BtrfsTreeItem {
		uint64_t m_objectid = 0;
		uint64_t m_offset = 0;
		uint64_t m_transid = 0;
		ByteVector m_data;
		uint8_t m_type = 0;
	public:
		/// Key component: object identifier.
		uint64_t objectid() const { return m_objectid; }
		/// Key component: item offset (meaning depends on @p type).
		uint64_t offset() const { return m_offset; }
		/// Transaction ID at which this item was last modified.
		uint64_t transid() const { return m_transid; }
		/// Key component: item type (one of the BTRFS_*_KEY constants).
		uint8_t type() const { return m_type; }
		/// Raw payload bytes for this item.
		const ByteVector data() const { return m_data; }
		BtrfsTreeItem() = default;
		/// Construct from a raw search result header (including its payload).
		BtrfsTreeItem(const BtrfsIoctlSearchHeader &bish);
		/// Assign from a raw search result header (including its payload).
		BtrfsTreeItem& operator=(const BtrfsIoctlSearchHeader &bish);
		/// True if this item is empty (default-constructed or null result).
		bool operator!() const;

		/// Member access methods.  Invoking a method on the
		/// wrong type of item will throw an exception.

		/// @{ Block group items
		uint64_t block_group_flags() const;
		uint64_t block_group_used() const;
		/// @}

		/// @{ Chunk items
		uint64_t chunk_length() const;
		uint64_t chunk_type() const;
		/// @}

		/// @{ Dev extent items (physical byte ranges)
		uint64_t dev_extent_chunk_offset() const;
		uint64_t dev_extent_length() const;
		/// @}

		/// @{ Dev items (devices)
		uint64_t dev_item_total_bytes() const;
		uint64_t dev_item_bytes_used() const;
		/// @}

		/// @{ Inode items
		uint64_t inode_flags() const;
		uint64_t inode_size() const;
		/// @}

		/// @{ Extent refs (EXTENT_DATA)
		uint64_t file_extent_logical_bytes() const;
		uint64_t file_extent_generation() const;
		uint64_t file_extent_offset() const;
		uint64_t file_extent_bytenr() const;
		uint8_t file_extent_type() const;
		btrfs_compression_type file_extent_compression() const;
		/// @}

		/// @{ Extent items (EXTENT_ITEM)
		uint64_t extent_begin() const;
		uint64_t extent_end() const;
		uint64_t extent_flags() const;
		uint64_t extent_generation() const;
		/// @}

		/// @{ Root items
		uint64_t root_flags() const;
		uint64_t root_refs() const;
		/// @}

		/// @{ Root backref items.
		uint64_t root_ref_dirid() const;
		string root_ref_name() const;
		uint64_t root_ref_parent_rootid() const;
		/// @}
	};

	ostream &operator<<(ostream &os, const BtrfsTreeItem &bti);

	/// Abstract base for btrfs kernel tree search helpers.
	/// Subclasses implement the mapping between a logical position (objectid or
	/// offset) and the search key, enabling prev/next/at/lower_bound lookups
	/// over any btrfs tree.
	class BtrfsTreeFetcher {
	protected:
		Fd m_fd;
		BtrfsIoctlSearchKey m_sk;
		uint64_t m_tree = 0;
		uint64_t m_min_transid = 0;
		uint64_t m_max_transid = numeric_limits<uint64_t>::max();
		uint64_t m_block_size = 0;
		uint64_t m_lookbehind_size = 0;
		uint64_t m_scale_size = 0;
		uint8_t m_type = 0;

		uint64_t scale_logical(uint64_t logical) const;
		uint64_t unscale_logical(uint64_t logical) const;
		const static uint64_t s_max_logical = numeric_limits<uint64_t>::max();
		uint64_t scaled_max_logical() const;

		virtual void fill_sk(BtrfsIoctlSearchKey &key, uint64_t object);
		virtual void next_sk(BtrfsIoctlSearchKey &key, const BtrfsIoctlSearchHeader &hdr);
		virtual uint64_t hdr_logical(const BtrfsIoctlSearchHeader &hdr) = 0;
		virtual bool hdr_match(const BtrfsIoctlSearchHeader &hdr) = 0;
		virtual bool hdr_stop(const BtrfsIoctlSearchHeader &hdr) = 0;
		Fd fd() const;
		void fd(Fd fd);
	public:
		virtual ~BtrfsTreeFetcher() = default;
		BtrfsTreeFetcher(Fd new_fd);
		/// Set the item type filter (BTRFS_*_KEY constant).
		void type(uint8_t type);
		/// Return the current item type filter.
		uint8_t type();
		/// Set the tree ID to search (e.g. BTRFS_EXTENT_TREE_OBJECTID).
		void tree(uint64_t tree);
		/// Return the current tree ID.
		uint64_t tree();
		/// Restrict results to items whose transaction ID is in [@p min_transid, @p max_transid].
		void transid(uint64_t min_transid, uint64_t max_transid = numeric_limits<uint64_t>::max());
		/// Block size (sectorsize) of filesystem
		uint64_t block_size() const;
		/// Fetch last object < logical, null if not found
		BtrfsTreeItem prev(uint64_t logical);
		/// Fetch first object > logical, null if not found
		BtrfsTreeItem next(uint64_t logical);
		/// Fetch object at exactly logical, null if not found
		BtrfsTreeItem at(uint64_t);
		/// Fetch first object >= logical
		BtrfsTreeItem lower_bound(uint64_t logical);
		/// Fetch last object <= logical
		BtrfsTreeItem rlower_bound(uint64_t logical);

		/// Estimated distance between objects
		virtual uint64_t lookbehind_size() const;
		virtual void lookbehind_size(uint64_t);

		/// Scale size (normally block size but must be set to 1 for fs trees)
		uint64_t scale_size() const;
		void scale_size(uint64_t);
	};

	/// BtrfsTreeFetcher variant that uses the objectid field as the logical position.
	/// Suitable for trees indexed primarily by objectid (e.g. extent tree, inode tree).
	class BtrfsTreeObjectFetcher : public BtrfsTreeFetcher {
	protected:
		virtual void fill_sk(BtrfsIoctlSearchKey &key, uint64_t logical) override;
		virtual uint64_t hdr_logical(const BtrfsIoctlSearchHeader &hdr) override;
		virtual bool hdr_match(const BtrfsIoctlSearchHeader &hdr) override;
		virtual bool hdr_stop(const BtrfsIoctlSearchHeader &hdr) override;
	public:
		using BtrfsTreeFetcher::BtrfsTreeFetcher;
	};

	/// BtrfsTreeFetcher variant that uses the offset field as the logical position.
	/// The objectid is fixed and set via objectid(); suitable for per-inode trees
	/// such as EXTENT_DATA items within a subvolume tree.
	class BtrfsTreeOffsetFetcher : public BtrfsTreeFetcher {
	protected:
		uint64_t m_objectid = 0;
		virtual void fill_sk(BtrfsIoctlSearchKey &key, uint64_t offset) override;
		virtual uint64_t hdr_logical(const BtrfsIoctlSearchHeader &hdr) override;
		virtual bool hdr_match(const BtrfsIoctlSearchHeader &hdr) override;
		virtual bool hdr_stop(const BtrfsIoctlSearchHeader &hdr) override;
	public:
		using BtrfsTreeFetcher::BtrfsTreeFetcher;
		/// Set the fixed objectid used for all searches.
		void objectid(uint64_t objectid);
		/// Return the fixed objectid.
		uint64_t objectid() const;
	};

	/// Fetcher for checksum tree items.  Automatically queries the filesystem
	/// to determine the checksum algorithm and per-checksum byte size.
	class BtrfsCsumTreeFetcher : public BtrfsTreeOffsetFetcher {
	public:
		const uint32_t BTRFS_CSUM_TYPE_UNKNOWN = uint32_t(1) << 16;
	private:
		size_t		m_sum_size = 0;
		uint32_t	m_sum_type = BTRFS_CSUM_TYPE_UNKNOWN;
	public:
		BtrfsCsumTreeFetcher(const Fd &fd);

		/// Return the filesystem's checksum algorithm type code.
		uint32_t sum_type() const;
		/// Return the size in bytes of a single checksum.
		size_t sum_size() const;
		/// Retrieve @p count checksums starting at @p logical and deliver them via @p output.
		void get_sums(uint64_t logical, size_t count, function<void(uint64_t logical, const uint8_t *buf, size_t count)> output);
	};

	/// Fetch extent items from extent tree.
	/// Does not filter out metadata!  See BtrfsDataExtentTreeFetcher for that.
	class BtrfsExtentItemFetcher : public BtrfsTreeObjectFetcher {
	public:
		BtrfsExtentItemFetcher(const Fd &fd);
	};

	/// Fetch extent refs from an inode.  Caller must set the tree and objectid.
	class BtrfsExtentDataFetcher : public BtrfsTreeOffsetFetcher {
	public:
		BtrfsExtentDataFetcher(const Fd &fd);
	};

	/// Fetch raw inode items
	class BtrfsInodeFetcher : public BtrfsTreeObjectFetcher {
	public:
		BtrfsInodeFetcher(const Fd &fd);
		BtrfsTreeItem stat(uint64_t subvol, uint64_t inode);
	};

	/// Fetch a root (subvol) item
	class BtrfsRootFetcher : public BtrfsTreeObjectFetcher {
	public:
		BtrfsRootFetcher(const Fd &fd);
		BtrfsTreeItem root(uint64_t subvol);
		BtrfsTreeItem root_backref(uint64_t subvol);
	};

	/// Fetch data extent items from extent tree, skipping metadata-only block groups
	class BtrfsDataExtentTreeFetcher : public BtrfsExtentItemFetcher {
		BtrfsTreeItem		m_current_bg;
		BtrfsTreeOffsetFetcher	m_chunk_tree;
	protected:
		virtual void next_sk(BtrfsIoctlSearchKey &key, const BtrfsIoctlSearchHeader &hdr) override;
	public:
		BtrfsDataExtentTreeFetcher(const Fd &fd);
	};

}

#endif
