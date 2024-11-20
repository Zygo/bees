#include "crucible/btrfs-tree.h"
#include "crucible/btrfs.h"
#include "crucible/error.h"
#include "crucible/fs.h"
#include "crucible/hexdump.h"
#include "crucible/seeker.h"

namespace crucible {
	using namespace std;

	uint64_t
	BtrfsTreeItem::extent_begin() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_ITEM_KEY);
		return m_objectid;
	}

	uint64_t
	BtrfsTreeItem::extent_end() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_ITEM_KEY);
		return m_objectid + m_offset;
	}

	uint64_t
	BtrfsTreeItem::extent_flags() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_ITEM_KEY);
		return btrfs_get_member(&btrfs_extent_item::flags, m_data);
	}

	uint64_t
	BtrfsTreeItem::extent_generation() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_ITEM_KEY);
		return btrfs_get_member(&btrfs_extent_item::generation, m_data);
	}

	uint64_t
	BtrfsTreeItem::root_ref_dirid() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_ROOT_BACKREF_KEY);
		return btrfs_get_member(&btrfs_root_ref::dirid, m_data);
	}

	string
	BtrfsTreeItem::root_ref_name() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_ROOT_BACKREF_KEY);
		const auto name_len = btrfs_get_member(&btrfs_root_ref::name_len, m_data);
		const auto name_start = sizeof(struct btrfs_root_ref);
		const auto name_end = name_len + name_start;
		THROW_CHECK2(runtime_error, m_data.size(), name_end, m_data.size() >= name_end);
		return string(m_data.data() + name_start, m_data.data() + name_end);
	}

	uint64_t
	BtrfsTreeItem::root_ref_parent_rootid() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_ROOT_BACKREF_KEY);
		return offset();
	}

	uint64_t
	BtrfsTreeItem::root_flags() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_ROOT_ITEM_KEY);
		return btrfs_get_member(&btrfs_root_item::flags, m_data);
	}

	uint64_t
	BtrfsTreeItem::root_refs() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_ROOT_ITEM_KEY);
		return btrfs_get_member(&btrfs_root_item::refs, m_data);
	}

	ostream &
	operator<<(ostream &os, const BtrfsTreeItem &bti)
	{
		os << "BtrfsTreeItem {"
			<< " objectid = " << to_hex(bti.objectid())
			<< ", type = " << btrfs_search_type_ntoa(bti.type())
			<< ", offset = " << to_hex(bti.offset())
			<< ", transid = " << bti.transid()
			<< ", data = ";
		hexdump(os, bti.data());
		return os;
	}

	uint64_t
	BtrfsTreeItem::block_group_flags() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_BLOCK_GROUP_ITEM_KEY);
		return btrfs_get_member(&btrfs_block_group_item::flags, m_data);
	}

	uint64_t
	BtrfsTreeItem::block_group_used() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_BLOCK_GROUP_ITEM_KEY);
		return btrfs_get_member(&btrfs_block_group_item::used, m_data);
	}

	uint64_t
	BtrfsTreeItem::chunk_length() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_CHUNK_ITEM_KEY);
		return btrfs_get_member(&btrfs_chunk::length, m_data);
	}

	uint64_t
	BtrfsTreeItem::chunk_type() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_CHUNK_ITEM_KEY);
		return btrfs_get_member(&btrfs_chunk::type, m_data);
	}

	uint64_t
	BtrfsTreeItem::dev_extent_chunk_offset() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_DEV_EXTENT_KEY);
		return btrfs_get_member(&btrfs_dev_extent::chunk_offset, m_data);
	}

	uint64_t
	BtrfsTreeItem::dev_extent_length() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_DEV_EXTENT_KEY);
		return btrfs_get_member(&btrfs_dev_extent::length, m_data);
	}

	uint64_t
	BtrfsTreeItem::dev_item_total_bytes() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_DEV_ITEM_KEY);
		return btrfs_get_member(&btrfs_dev_item::total_bytes, m_data);
	}

	uint64_t
	BtrfsTreeItem::dev_item_bytes_used() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_DEV_ITEM_KEY);
		return btrfs_get_member(&btrfs_dev_item::bytes_used, m_data);
	}

	uint64_t
	BtrfsTreeItem::inode_size() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_INODE_ITEM_KEY);
		return btrfs_get_member(&btrfs_inode_item::size, m_data);
	}

	uint64_t
	BtrfsTreeItem::file_extent_logical_bytes() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_DATA_KEY);
		const auto file_extent_item_type = btrfs_get_member(&btrfs_file_extent_item::type, m_data);
		switch (file_extent_item_type) {
			case BTRFS_FILE_EXTENT_INLINE:
				return btrfs_get_member(&btrfs_file_extent_item::ram_bytes, m_data);
			case BTRFS_FILE_EXTENT_PREALLOC:
			case BTRFS_FILE_EXTENT_REG:
				return btrfs_get_member(&btrfs_file_extent_item::num_bytes, m_data);
			default:
				THROW_ERROR(runtime_error, "unknown btrfs_file_extent_item type " << file_extent_item_type);
		}
	}

	uint64_t
	BtrfsTreeItem::file_extent_offset() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_DATA_KEY);
		const auto file_extent_item_type = btrfs_get_member(&btrfs_file_extent_item::type, m_data);
		switch (file_extent_item_type) {
			case BTRFS_FILE_EXTENT_INLINE:
				THROW_ERROR(invalid_argument, "extent is inline " << *this);
			case BTRFS_FILE_EXTENT_PREALLOC:
			case BTRFS_FILE_EXTENT_REG:
				return btrfs_get_member(&btrfs_file_extent_item::offset, m_data);
			default:
				THROW_ERROR(runtime_error, "unknown btrfs_file_extent_item type " << file_extent_item_type << " in " << *this);
		}
	}

	uint64_t
	BtrfsTreeItem::file_extent_generation() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_DATA_KEY);
		return btrfs_get_member(&btrfs_file_extent_item::generation, m_data);
	}

	uint64_t
	BtrfsTreeItem::file_extent_bytenr() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_DATA_KEY);
		auto file_extent_item_type = btrfs_get_member(&btrfs_file_extent_item::type, m_data);
		switch (file_extent_item_type) {
			case BTRFS_FILE_EXTENT_INLINE:
				THROW_ERROR(invalid_argument, "extent is inline " << *this);
			case BTRFS_FILE_EXTENT_PREALLOC:
			case BTRFS_FILE_EXTENT_REG:
				return btrfs_get_member(&btrfs_file_extent_item::disk_bytenr, m_data);
			default:
				THROW_ERROR(runtime_error, "unknown btrfs_file_extent_item type " << file_extent_item_type << " in " << *this);
		}
	}

	uint8_t
	BtrfsTreeItem::file_extent_type() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_DATA_KEY);
		return btrfs_get_member(&btrfs_file_extent_item::type, m_data);
	}

	btrfs_compression_type
	BtrfsTreeItem::file_extent_compression() const
	{
		THROW_CHECK1(invalid_argument, btrfs_search_type_ntoa(m_type), m_type == BTRFS_EXTENT_DATA_KEY);
		return static_cast<btrfs_compression_type>(btrfs_get_member(&btrfs_file_extent_item::compression, m_data));
	}

	BtrfsTreeItem::BtrfsTreeItem(const BtrfsIoctlSearchHeader &bish) :
		m_objectid(bish.objectid),
		m_offset(bish.offset),
		m_transid(bish.transid),
		m_data(bish.m_data),
		m_type(bish.type)
	{
	}

	BtrfsTreeItem &
	BtrfsTreeItem::operator=(const BtrfsIoctlSearchHeader &bish)
	{
		m_objectid = bish.objectid;
		m_offset = bish.offset;
		m_transid = bish.transid;
		m_data = bish.m_data;
		m_type = bish.type;
		return *this;
	}

	bool
	BtrfsTreeItem::operator!() const
	{
		return m_transid == 0 && m_objectid == 0 && m_offset == 0 && m_type == 0;
	}

	uint64_t
	BtrfsTreeFetcher::block_size() const
	{
		return m_block_size;
	}

	BtrfsTreeFetcher::BtrfsTreeFetcher(Fd new_fd) :
		m_fd(new_fd)
	{
		BtrfsIoctlFsInfoArgs bifia;
		bifia.do_ioctl(fd());
		m_block_size = bifia.sectorsize;
		THROW_CHECK1(runtime_error, m_block_size, m_block_size > 0);
		// We don't believe sector sizes that aren't multiples of 4K
		THROW_CHECK1(runtime_error, m_block_size, (m_block_size % 4096) == 0);
		m_lookbehind_size = 128 * 1024;
		m_scale_size = m_block_size;
	}

	Fd
	BtrfsTreeFetcher::fd() const
	{
		return m_fd;
	}

	void
	BtrfsTreeFetcher::fd(Fd fd)
	{
		m_fd = fd;
	}

	void
	BtrfsTreeFetcher::type(uint8_t type)
	{
		m_type = type;
	}

	uint8_t
	BtrfsTreeFetcher::type()
	{
		return m_type;
	}

	void
	BtrfsTreeFetcher::tree(uint64_t tree)
	{
		m_tree = tree;
	}

	uint64_t
	BtrfsTreeFetcher::tree()
	{
		return m_tree;
	}

	void
	BtrfsTreeFetcher::transid(uint64_t min_transid, uint64_t max_transid)
	{
		m_min_transid = min_transid;
		m_max_transid = max_transid;
	}

	uint64_t
	BtrfsTreeFetcher::lookbehind_size() const
	{
		return m_lookbehind_size;
	}

	void
	BtrfsTreeFetcher::lookbehind_size(uint64_t lookbehind_size)
	{
		m_lookbehind_size = lookbehind_size;
	}

	uint64_t
	BtrfsTreeFetcher::scale_size() const
	{
		return m_scale_size;
	}

	void
	BtrfsTreeFetcher::scale_size(uint64_t scale_size)
	{
		m_scale_size = scale_size;
	}

	void
	BtrfsTreeFetcher::fill_sk(BtrfsIoctlSearchKey &sk, uint64_t object)
	{
		(void)object;
		// btrfs allows tree ID 0 meaning the current tree, but we do not.
		THROW_CHECK0(invalid_argument, m_tree != 0);
		sk.tree_id = m_tree;
		sk.min_type = m_type;
		sk.max_type = m_type;
		sk.min_transid = m_min_transid;
		sk.max_transid = m_max_transid;
		sk.nr_items = 1;
	}

	void
	BtrfsTreeFetcher::next_sk(BtrfsIoctlSearchKey &key, const BtrfsIoctlSearchHeader &hdr)
	{
		key.next_min(hdr, m_type);
	}

	BtrfsTreeItem
	BtrfsTreeFetcher::at(uint64_t logical)
	{
		BtrfsIoctlSearchKey &sk = m_sk;
		fill_sk(sk, logical);
		// Exact match, should return 0 or 1 items
		sk.max_type = sk.min_type;
		sk.nr_items = 1;
		sk.do_ioctl(fd());
		THROW_CHECK1(runtime_error, sk.m_result.size(), sk.m_result.size() < 2);
		for (const auto &i : sk.m_result) {
			if (hdr_logical(i) == logical && hdr_match(i)) {
				return i;
			}
		}
		return BtrfsTreeItem();
	}

	uint64_t
	BtrfsTreeFetcher::scale_logical(const uint64_t logical) const
	{
		THROW_CHECK1(invalid_argument, logical, (logical % m_scale_size) == 0 || logical == s_max_logical);
		return logical / m_scale_size;
	}

	uint64_t
	BtrfsTreeFetcher::scaled_max_logical() const
	{
		return scale_logical(s_max_logical);
	}

	uint64_t
	BtrfsTreeFetcher::unscale_logical(const uint64_t logical) const
	{
		THROW_CHECK1(invalid_argument, logical, logical <= scaled_max_logical());
		if (logical == scaled_max_logical()) {
			return s_max_logical;
		}
		return logical * scale_size();
	}

	BtrfsTreeItem
	BtrfsTreeFetcher::rlower_bound(uint64_t logical)
	{
	#if 0
	#define BTFRLB_DEBUG(x) do { cerr << x; } while (false)
	#else
	#define BTFRLB_DEBUG(x) do { } while (false)
	#endif
		BtrfsTreeItem closest_item;
		uint64_t closest_logical = 0;
		BtrfsIoctlSearchKey &sk = m_sk;
		size_t loops = 0;
		BTFRLB_DEBUG("rlower_bound: " << to_hex(logical) << endl);
		seek_backward(scale_logical(logical), [&](uint64_t lower_bound, uint64_t upper_bound) {
			++loops;
			fill_sk(sk, unscale_logical(min(scaled_max_logical(), lower_bound)));
			set<uint64_t> rv;
			do {
				sk.nr_items = 4;
				sk.do_ioctl(fd());
				BTFRLB_DEBUG("fetch: loop " << loops << " lower_bound..upper_bound " << to_hex(lower_bound) << ".." << to_hex(upper_bound));
				for (auto &i : sk.m_result) {
					next_sk(sk, i);
					const auto this_logical = hdr_logical(i);
					const auto scaled_hdr_logical = scale_logical(this_logical);
					BTFRLB_DEBUG(" " << to_hex(scaled_hdr_logical));
					if (hdr_match(i)) {
						if (this_logical <= logical && this_logical > closest_logical) {
							closest_logical = this_logical;
							closest_item = i;
						}
						BTFRLB_DEBUG("(match)");
						rv.insert(scaled_hdr_logical);
					}
					if (scaled_hdr_logical > upper_bound || hdr_stop(i)) {
						if (scaled_hdr_logical >= upper_bound) {
							BTFRLB_DEBUG("(" << to_hex(scaled_hdr_logical) << " >= " << to_hex(upper_bound) << ")");
						}
						if (hdr_stop(i)) {
							rv.insert(numeric_limits<uint64_t>::max());
							BTFRLB_DEBUG("(stop)");
						}
						break;
					} else {
						BTFRLB_DEBUG("(cont'd)");
					}
				}
				BTFRLB_DEBUG(endl);
				// We might get a search result that contains only non-matching items.
				// Keep looping until we find any matching item or we run out of tree.
			} while (rv.empty() && !sk.m_result.empty());
			return rv;
		}, scale_logical(lookbehind_size()));
		return closest_item;
	#undef BTFRLB_DEBUG
	}

	BtrfsTreeItem
	BtrfsTreeFetcher::lower_bound(uint64_t logical)
	{
		BtrfsIoctlSearchKey &sk = m_sk;
		fill_sk(sk, logical);
		do {
			assert(sk.max_offset == s_max_logical);
			sk.do_ioctl(fd());
			for (const auto &i : sk.m_result) {
				if (hdr_match(i)) {
					return i;
				}
				if (hdr_stop(i)) {
					return BtrfsTreeItem();
				}
				next_sk(sk, i);
			}
		} while (!sk.m_result.empty());
		return BtrfsTreeItem();
	}

	BtrfsTreeItem
	BtrfsTreeFetcher::next(uint64_t logical)
	{
		const auto scaled_logical = scale_logical(logical);
		if (scaled_logical + 1 > scaled_max_logical()) {
			return BtrfsTreeItem();
		}
		return lower_bound(unscale_logical(scaled_logical + 1));
	}

	BtrfsTreeItem
	BtrfsTreeFetcher::prev(uint64_t logical)
	{
		const auto scaled_logical = scale_logical(logical);
		if (scaled_logical < 1) {
			return BtrfsTreeItem();
		}
		return rlower_bound(unscale_logical(scaled_logical - 1));
	}

	void
	BtrfsTreeObjectFetcher::fill_sk(BtrfsIoctlSearchKey &sk, uint64_t object)
	{
		BtrfsTreeFetcher::fill_sk(sk, object);
		sk.min_offset = 0;
		sk.max_offset = numeric_limits<decltype(sk.max_offset)>::max();
		sk.min_objectid = object;
		sk.max_objectid = numeric_limits<decltype(sk.max_objectid)>::max();
	}

	uint64_t
	BtrfsTreeObjectFetcher::hdr_logical(const BtrfsIoctlSearchHeader &hdr)
	{
		return hdr.objectid;
	}

	bool
	BtrfsTreeObjectFetcher::hdr_match(const BtrfsIoctlSearchHeader &hdr)
	{
		// If you're calling this method without overriding it, you should have set type first
		assert(m_type);
		return hdr.type == m_type;
	}

	bool
	BtrfsTreeObjectFetcher::hdr_stop(const BtrfsIoctlSearchHeader &hdr)
	{
		return false;
		(void)hdr;
	}

	uint64_t
	BtrfsTreeOffsetFetcher::hdr_logical(const BtrfsIoctlSearchHeader &hdr)
	{
		return hdr.offset;
	}

	bool
	BtrfsTreeOffsetFetcher::hdr_match(const BtrfsIoctlSearchHeader &hdr)
	{
		assert(m_type);
		return hdr.type == m_type && hdr.objectid == m_objectid;
	}

	bool
	BtrfsTreeOffsetFetcher::hdr_stop(const BtrfsIoctlSearchHeader &hdr)
	{
		assert(m_type);
		return hdr.objectid > m_objectid || hdr.type > m_type;
	}

	void
	BtrfsTreeOffsetFetcher::objectid(uint64_t objectid)
	{
		m_objectid = objectid;
	}

	uint64_t
	BtrfsTreeOffsetFetcher::objectid() const
	{
		return m_objectid;
	}

	void
	BtrfsTreeOffsetFetcher::fill_sk(BtrfsIoctlSearchKey &sk, uint64_t offset)
	{
		BtrfsTreeFetcher::fill_sk(sk, offset);
		sk.min_offset = offset;
		sk.max_offset = numeric_limits<decltype(sk.max_offset)>::max();
		sk.min_objectid = m_objectid;
		sk.max_objectid = m_objectid;
	}

	void
	BtrfsCsumTreeFetcher::get_sums(uint64_t const logical, size_t count, function<void(uint64_t logical, const uint8_t *buf, size_t bytes)> output)
	{
	#if 0
	#define BCTFGS_DEBUG(x) do { cerr << x; } while (false)
	#else
	#define BCTFGS_DEBUG(x) do { } while (false)
	#endif
		const uint64_t logical_end = logical + count * block_size();
		BtrfsTreeItem bti = rlower_bound(logical);
		size_t __attribute__((unused)) loops = 0;
		BCTFGS_DEBUG("get_sums " << to_hex(logical) << ".." << to_hex(logical_end) << endl);
		while (!!bti) {
			BCTFGS_DEBUG("get_sums[" << loops << "]: " << bti << endl);
			++loops;
			// Reject wrong type or objectid
			THROW_CHECK1(runtime_error, bti.type(), bti.type() == BTRFS_EXTENT_CSUM_KEY);
			THROW_CHECK1(runtime_error, bti.objectid(), bti.objectid() == BTRFS_EXTENT_CSUM_OBJECTID);
			// Is this object in range?
			const uint64_t data_logical = bti.offset();
			if (data_logical >= logical_end) {
				// csum object is past end of range, we are done
				return;
			}
			// Figure out how long this csum item is in various units
			const size_t csum_byte_count = bti.data().size();
			THROW_CHECK1(runtime_error, csum_byte_count, (csum_byte_count % m_sum_size) == 0);
			THROW_CHECK1(runtime_error, csum_byte_count, csum_byte_count > 0);
			const size_t csum_count = csum_byte_count / m_sum_size;
			const uint64_t data_byte_count = csum_count * block_size();
			const uint64_t data_logical_end = data_logical + data_byte_count;
			if (data_logical_end <= logical) {
				// too low, look at next item
				bti = lower_bound(logical);
				continue;
			}
			// There is some overlap?
			const uint64_t overlap_begin = max(logical, data_logical);
			const uint64_t overlap_end = min(logical_end, data_logical_end);
			THROW_CHECK2(runtime_error, overlap_begin, overlap_end, overlap_begin < overlap_end);
			const uint64_t overlap_offset = overlap_begin - data_logical;
			THROW_CHECK1(runtime_error, overlap_offset, (overlap_offset % block_size()) == 0);
			const uint64_t overlap_index = overlap_offset * m_sum_size / block_size();
			const uint64_t overlap_byte_count = overlap_end - overlap_begin;
			const uint64_t overlap_csum_byte_count = overlap_byte_count * m_sum_size / block_size();
			// Can't be bigger than a btrfs item
			THROW_CHECK1(runtime_error, overlap_index, overlap_index < 65536);
			THROW_CHECK1(runtime_error, overlap_csum_byte_count, overlap_csum_byte_count < 65536);
			// Yes, process the overlap
			output(overlap_begin, bti.data().data() + overlap_index, overlap_csum_byte_count);
			// Advance
			bti = lower_bound(overlap_end);
		}
	#undef BCTFGS_DEBUG
	}

	uint32_t
	BtrfsCsumTreeFetcher::sum_type() const
	{
		return m_sum_type;
	}

	size_t
	BtrfsCsumTreeFetcher::sum_size() const
	{
		return m_sum_size;
	}

	BtrfsCsumTreeFetcher::BtrfsCsumTreeFetcher(const Fd &new_fd) :
		BtrfsTreeOffsetFetcher(new_fd)
	{
		type(BTRFS_EXTENT_CSUM_KEY);
		tree(BTRFS_CSUM_TREE_OBJECTID);
		objectid(BTRFS_EXTENT_CSUM_OBJECTID);
		BtrfsIoctlFsInfoArgs bifia;
		bifia.do_ioctl(fd());
		m_sum_type = static_cast<btrfs_compression_type>(bifia.csum_type());
		m_sum_size = bifia.csum_size();
		if (m_sum_type == BTRFS_CSUM_TYPE_CRC32 && m_sum_size == 0) {
			// Older kernel versions don't fill in this field
			m_sum_size = 4;
		}
		THROW_CHECK1(runtime_error, m_sum_size, m_sum_size > 0);
	}

	BtrfsExtentItemFetcher::BtrfsExtentItemFetcher(const Fd &new_fd) :
		BtrfsTreeObjectFetcher(new_fd)
	{
		tree(BTRFS_EXTENT_TREE_OBJECTID);
		type(BTRFS_EXTENT_ITEM_KEY);
	}

	BtrfsExtentDataFetcher::BtrfsExtentDataFetcher(const Fd &new_fd) :
		BtrfsTreeOffsetFetcher(new_fd)
	{
		type(BTRFS_EXTENT_DATA_KEY);
	}

	BtrfsFsTreeFetcher::BtrfsFsTreeFetcher(const Fd &new_fd, uint64_t subvol) :
		BtrfsTreeObjectFetcher(new_fd)
	{
		tree(subvol);
		type(BTRFS_EXTENT_DATA_KEY);
		scale_size(1);
	}

	BtrfsInodeFetcher::BtrfsInodeFetcher(const Fd &fd) :
		BtrfsTreeObjectFetcher(fd)
	{
		type(BTRFS_INODE_ITEM_KEY);
		scale_size(1);
	}

	BtrfsTreeItem
	BtrfsInodeFetcher::stat(uint64_t subvol, uint64_t inode)
	{
		tree(subvol);
		const auto item = at(inode);
		if (!!item) {
			THROW_CHECK2(runtime_error, item.objectid(), inode, inode == item.objectid());
			THROW_CHECK2(runtime_error, item.type(), BTRFS_INODE_ITEM_KEY, item.type() == BTRFS_INODE_ITEM_KEY);
		}
		return item;
	}

	BtrfsRootFetcher::BtrfsRootFetcher(const Fd &fd) :
		BtrfsTreeObjectFetcher(fd)
	{
		tree(BTRFS_ROOT_TREE_OBJECTID);
		type(BTRFS_ROOT_ITEM_KEY);
		scale_size(1);
	}

	BtrfsTreeItem
	BtrfsRootFetcher::root(uint64_t subvol)
	{
		const auto item = at(subvol);
		if (!!item) {
			THROW_CHECK2(runtime_error, item.objectid(), subvol, subvol == item.objectid());
			THROW_CHECK2(runtime_error, item.type(), BTRFS_ROOT_ITEM_KEY, item.type() == BTRFS_ROOT_ITEM_KEY);
		}
		return item;
	}
}
