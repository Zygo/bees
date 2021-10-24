#include "crucible/fs.h"

#include "crucible/error.h"
#include "crucible/fd.h"
#include "crucible/hexdump.h"
#include "crucible/limits.h"
#include "crucible/ntoa.h"
#include "crucible/string.h"

// FS_IOC_FIEMAP
#include <linux/fs.h>

#include <cassert>
#include <cstddef>
#include <iostream>
#include <exception>

#include <sys/ioctl.h>

namespace crucible {

	void
	punch_hole(int fd, off_t offset, off_t len)
	{
#ifdef FALLOC_FL_PUNCH_HOLE
		DIE_IF_MINUS_ONE(::fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
			offset, len));
#else
		(void)fd;
		(void)offset;
		(void)len;
		throw runtime_error("FALLOC_FL_PUNCH_HOLE not implemented");
#endif
	}

	BtrfsExtentSame::BtrfsExtentSame(int src_fd, off_t src_offset, off_t src_length) :
		m_logical_offset(src_offset),
		m_length(src_length),
		m_fd(src_fd)
	{
	}

	BtrfsExtentSame::~BtrfsExtentSame()
	{
	}

	void
	BtrfsExtentSame::add(int const fd, uint64_t const offset)
	{
		m_info.push_back( (btrfs_ioctl_same_extent_info) {
			.fd = fd,
			.logical_offset = offset,
		});
	}

	ostream &
	operator<<(ostream &os, const btrfs_ioctl_same_extent_info *info)
	{
		if (!info) {
			return os << "btrfs_ioctl_same_extent_info NULL";
		}
		os << "btrfs_ioctl_same_extent_info {";
		os << " .fd = " << info->fd;
		if (info->fd >= 0) {
			catch_all([&](){
				string fd_name = name_fd(info->fd);
				os << " '" << fd_name << "'";
			});
		}
		os << ", .logical_offset = " << to_hex(info->logical_offset);
		os << ", .bytes_deduped = " << to_hex(info->bytes_deduped);
		os << ", .status = " << info->status;
		if (info->status < 0) {
			os << " (" << strerror(-info->status) << ")";
		}
		os << ", .reserved = " << info->reserved;
		return os << " }";
	}

	ostream &
	operator<<(ostream &os, const btrfs_ioctl_same_args *args)
	{
		if (!args) {
			return os << "btrfs_ioctl_same_args NULL";
		}
		os << "btrfs_ioctl_same_args {";
		os << " .logical_offset = " << to_hex(args->logical_offset);
		os << ", .length = " << to_hex(args->length);
		os << ", .dest_count = " << args->dest_count;
		os << ", .reserved1 = " << args->reserved1;
		os << ", .reserved2 = " << args->reserved2;
		os << ", .info[] = {";
		for (int i = 0; i < args->dest_count; ++i) {
			os << " [" << i << "] = " << &(args->info[i]) << ",";
		}
		return os << " }";
	}

	ostream &
	operator<<(ostream &os, const BtrfsExtentSame &bes)
	{
		os << "BtrfsExtentSame {";
		os << " .m_fd = " << bes.m_fd;
		if (bes.m_fd >= 0) {
			catch_all([&](){
				string fd_name = name_fd(bes.m_fd);
				os << " '" << fd_name << "'";
			});
		}
		os << ", .logical_offset = " << to_hex(bes.m_logical_offset);
		os << ", .length = " << to_hex(bes.m_length);
		os << ", .info[] = {";
		for (size_t i = 0; i < bes.m_info.size(); ++i) {
			os << " [" << i << "] = " << &(bes.m_info[i]) << ",";
		}
		return os << " }";
	}

	void
	btrfs_clone_range(int src_fd, off_t src_offset, off_t src_length, int dst_fd, off_t dst_offset)
	{
		btrfs_ioctl_clone_range_args args ( (btrfs_ioctl_clone_range_args) {
			.src_fd = src_fd,
			.src_offset = ranged_cast<uint64_t, off_t>(src_offset),
			.src_length = ranged_cast<uint64_t, off_t>(src_length),
			.dest_offset = ranged_cast<uint64_t, off_t>(dst_offset),
		} );
		DIE_IF_MINUS_ONE(ioctl(dst_fd, BTRFS_IOC_CLONE_RANGE, &args));
	}

	void
	BtrfsExtentSame::do_ioctl()
	{
		const size_t buf_size = sizeof(btrfs_ioctl_same_args) + m_info.size() * sizeof(btrfs_ioctl_same_extent_info);
		ByteVector ioctl_arg( (btrfs_ioctl_same_args) {
			.logical_offset = m_logical_offset,
			.length = m_length,
			.dest_count = ranged_cast<decltype(btrfs_ioctl_same_args::dest_count)>(m_info.size()),
		}, buf_size);
		btrfs_ioctl_same_args *const ioctl_ptr = ioctl_arg.get<btrfs_ioctl_same_args>();
		size_t count = 0;
		for (auto i = m_info.cbegin(); i != m_info.cend(); ++i) {
			ioctl_ptr->info[count] = static_cast<const btrfs_ioctl_same_extent_info &>(m_info[count]);
			++count;
		}
		int rv = ioctl(m_fd, BTRFS_IOC_FILE_EXTENT_SAME, ioctl_ptr);
		if (rv) {
			THROW_ERRNO("After FILE_EXTENT_SAME (fd = " << m_fd << " '" << name_fd(m_fd) << "') : " << ioctl_ptr);
		}
		count = 0;
		for (auto i = m_info.cbegin(); i != m_info.cend(); ++i) {
			static_cast<btrfs_ioctl_same_extent_info &>(m_info[count]) = ioctl_ptr->info[count];
			++count;
		}
	}

	bool
	btrfs_extent_same(int src_fd, off_t src_offset, off_t src_length, int dst_fd, off_t dst_offset)
	{
		THROW_CHECK1(invalid_argument, src_length, src_length > 0);
		while (src_length > 0) {
			BtrfsExtentSame bes(src_fd, src_offset, src_length);
			bes.add(dst_fd, dst_offset);
			bes.do_ioctl();
			const auto status = bes.m_info.at(0).status;
			if (status == 0) {
				const off_t length = bes.m_info.at(0).bytes_deduped;
				THROW_CHECK0(invalid_argument, length > 0);
				src_offset += length;
				dst_offset += length;
				src_length -= length;
				continue;
			}
			if (status == BTRFS_SAME_DATA_DIFFERS) {
				return false;
			}
			if (status < 0) {
				THROW_ERRNO_VALUE(-status, "btrfs-extent-same: " << bes);
			}
			// THROW_ERROR(runtime_error, "btrfs-extent-same src_fd " << name_fd(src_fd) << " src_offset " << src_offset << " length " << length << " dst_fd " << name_fd(dst_fd) << " dst_offset " << dst_offset << " status " << status);
			THROW_ERROR(runtime_error, "btrfs-extent-same unknown status " << status << ": " << bes);
		}
		return true;
	}

	BtrfsDataContainer::BtrfsDataContainer(size_t buf_size) :
		m_data(buf_size)
	{
	}

	void *
	BtrfsDataContainer::prepare(size_t container_size)
	{
		const size_t min_size = offsetof(btrfs_data_container, val);
		if (container_size < min_size) {
			THROW_ERROR(out_of_range, "container size " << container_size << " smaller than minimum " << min_size);
		}
		if (m_data.size() < container_size) {
			m_data = ByteVector(container_size);
		}
		const auto p = m_data.get<btrfs_data_container>();
		*p = (btrfs_data_container) { };
		return p;
	}

	size_t
	BtrfsDataContainer::get_size() const
	{
		return m_data.size();
	}

	decltype(btrfs_data_container::bytes_left)
	BtrfsDataContainer::get_bytes_left() const
	{
		const auto p = m_data.get<btrfs_data_container>();
		return p->bytes_left;
	}

	decltype(btrfs_data_container::bytes_missing)
	BtrfsDataContainer::get_bytes_missing() const
	{
		const auto p = m_data.get<btrfs_data_container>();
		return p->bytes_missing;
	}

	decltype(btrfs_data_container::elem_cnt)
	BtrfsDataContainer::get_elem_cnt() const
	{
		const auto p = m_data.get<btrfs_data_container>();
		return p->elem_cnt;
	}

	decltype(btrfs_data_container::elem_missed)
	BtrfsDataContainer::get_elem_missed() const
	{
		const auto p = m_data.get<btrfs_data_container>();
		return p->elem_missed;
	}

	ostream &
	operator<<(ostream &os, const BtrfsIoctlLogicalInoArgs *p)
	{
		if (!p) {
			return os << "BtrfsIoctlLogicalInoArgs NULL";
		}
		os << "BtrfsIoctlLogicalInoArgs {";
		os << " .m_logical = " << to_hex(p->m_logical);
		os << " .inodes[] = {\n";
		unsigned count = 0;
		for (auto i = p->m_iors.cbegin(); i != p->m_iors.cend(); ++i) {
			os << "\t\t[" << count++ << "] = " << *i << ",\n";
		}
		os << "}\n";
		return os;
	}

	BtrfsIoctlLogicalInoArgs::BtrfsIoctlLogicalInoArgs(uint64_t new_logical, size_t new_size) :
		m_container_size(new_size),
		m_container(new_size),
		m_logical(new_logical)
	{
	}

	size_t
	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::size() const
	{
		return m_end - m_begin;
	}

	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::const_iterator
	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::cbegin() const
	{
		return m_begin;
	}

	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::const_iterator
	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::cend() const
	{
		return m_end;
	}

	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::iterator
	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::begin() const
	{
		return m_begin;
	}

	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::iterator
	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::end() const
	{
		return m_end;
	}

	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::iterator
	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::data() const
	{
		return m_begin;
	}

	void
	BtrfsIoctlLogicalInoArgs::BtrfsInodeOffsetRootSpan::clear()
	{
		m_end = m_begin = nullptr;
	}

	void
	BtrfsIoctlLogicalInoArgs::set_flags(uint64_t new_flags)
	{
		m_flags = new_flags;
	}

	uint64_t
	BtrfsIoctlLogicalInoArgs::get_flags() const
	{
		// We are still supporting building with old headers that don't have .flags yet
		return m_flags;
	}

	void
	BtrfsIoctlLogicalInoArgs::set_logical(uint64_t new_logical)
	{
		m_logical = new_logical;
	}

	void
	BtrfsIoctlLogicalInoArgs::set_size(uint64_t new_size)
	{
		m_container_size = new_size;
	}

	bool
	BtrfsIoctlLogicalInoArgs::do_ioctl_nothrow(int fd)
	{
		btrfs_ioctl_logical_ino_args args = (btrfs_ioctl_logical_ino_args) {
			.logical = m_logical,
			.size = m_container_size,
			.inodes = reinterpret_cast<uintptr_t>(m_container.prepare(m_container_size)),
		};
		// We are still supporting building with old headers that don't have .flags yet
		*(&args.reserved[0] + 3) = m_flags;

		btrfs_ioctl_logical_ino_args *const p = &args;

		m_iors.clear();

		static unsigned long bili_version = 0;

		if (get_flags() == 0) {
			// Could use either V1 or V2
			if (bili_version) {
				// We tested both versions and came to a decision
				if (ioctl(fd, bili_version, p)) {
					return false;
				}
			}  else {
				// Try V2
				if (ioctl(fd, BTRFS_IOC_LOGICAL_INO_V2, p)) {
					// V2 failed, try again with V1
					if (ioctl(fd, BTRFS_IOC_LOGICAL_INO, p)) {
						// both V1 and V2 failed, doesn't tell us which one to choose
						return false;
					}
					// V1 and V2 both tested with same arguments, V1 OK, and V2 failed
					bili_version = BTRFS_IOC_LOGICAL_INO;
				} else {
					// V2 succeeded, don't use V1 any more
					bili_version = BTRFS_IOC_LOGICAL_INO_V2;
				}
			}
		} else {
			// Flags/size require a V2 feature, no fallback to V1 possible
			if (ioctl(fd, BTRFS_IOC_LOGICAL_INO_V2, p)) {
				return false;
			}
			// V2 succeeded so we don't need to probe any more
			bili_version = BTRFS_IOC_LOGICAL_INO_V2;
		}

		btrfs_data_container *const bdc = reinterpret_cast<btrfs_data_container *>(p->inodes);
		BtrfsInodeOffsetRoot *const ior_iter = reinterpret_cast<BtrfsInodeOffsetRoot *>(bdc->val);

		// elem_cnt counts uint64_t, but BtrfsInodeOffsetRoot is 3x uint64_t
		THROW_CHECK1(runtime_error, bdc->elem_cnt, bdc->elem_cnt % 3 == 0);
		m_iors.m_begin = ior_iter;
		m_iors.m_end = ior_iter + bdc->elem_cnt / 3;
		return true;
	}

	void
	BtrfsIoctlLogicalInoArgs::do_ioctl(int fd) {
		if (!do_ioctl_nothrow(fd)) {
			THROW_ERRNO("BTRFS_IOC_LOGICAL_INO: " << name_fd(fd) << ", " << this);
		}
	}

	ostream &
	operator<<(ostream &os, const BtrfsInodeOffsetRoot &ior)
	{
		os << "BtrfsInodeOffsetRoot {";
		os << " .m_inum = " << ior.m_inum << ",";
		os << " .m_offset = " << to_hex(ior.m_offset) << ",";
		os << " .m_root = " << ior.m_root;
		os << " }";
		return os;
	}

	BtrfsIoctlInoPathArgs::BtrfsIoctlInoPathArgs(uint64_t inode, size_t new_size) :
		btrfs_ioctl_ino_path_args( (btrfs_ioctl_ino_path_args) { } ),
		m_container_size(new_size)
	{
		assert(inum == 0);
		inum = inode;
	}

	bool
	BtrfsIoctlInoPathArgs::do_ioctl_nothrow(int fd)
	{
		btrfs_ioctl_ino_path_args *p = static_cast<btrfs_ioctl_ino_path_args *>(this);
		BtrfsDataContainer container(m_container_size);
		fspath = reinterpret_cast<uintptr_t>(container.prepare(m_container_size));
		size = container.get_size();

		m_paths.clear();

		if (ioctl(fd, BTRFS_IOC_INO_PATHS, p) < 0) {
			return false;
		}

		btrfs_data_container *const bdc = reinterpret_cast<btrfs_data_container *>(p->fspath);
		m_paths.reserve(bdc->elem_cnt);

		const uint64_t *up = reinterpret_cast<const uint64_t *>(bdc->val);
		const char *const cp = reinterpret_cast<const char *>(bdc->val);

		for (auto count = bdc->elem_cnt; count > 0; --count) {
			const char *const path = cp + *up++;
			if (static_cast<size_t>(path - cp) > container.get_size()) {
				THROW_ERROR(out_of_range, "offset " << (path - cp) << " > size " << container.get_size() << " in " << __PRETTY_FUNCTION__);
			}
			m_paths.push_back(string(path));
		}

		return true;
	}

	void
	BtrfsIoctlInoPathArgs::do_ioctl(int fd) {
		if (!do_ioctl_nothrow(fd)) {
			THROW_ERRNO("BTRFS_IOC_INO_PATHS: " << name_fd(fd));
		}
	}

	ostream &
	operator<<(ostream &os, const BtrfsIoctlInoPathArgs &ipa)
	{
		const BtrfsIoctlInoPathArgs *p = &ipa;
		if (!p) {
			return os << "BtrfsIoctlInoPathArgs NULL";
		}
		os << "BtrfsIoctlInoPathArgs {";
		os << " .inum = " << p->inum;
		os << " .paths[] = {\n";
		unsigned count = 0;
		for (auto i = p->m_paths.cbegin(); i != p->m_paths.cend(); ++i) {
			os << "\t\t[" << count++ << "] = \"" << *i << "\",\n";
		}
		os << "\t}\n";
		return os;
	}

	BtrfsIoctlInoLookupArgs::BtrfsIoctlInoLookupArgs(uint64_t new_objectid) :
		btrfs_ioctl_ino_lookup_args( (btrfs_ioctl_ino_lookup_args) { } )
	{
		assert(objectid == 0);
		objectid = new_objectid;
	}

	bool
	BtrfsIoctlInoLookupArgs::do_ioctl_nothrow(int fd)
	{
		btrfs_ioctl_ino_lookup_args *ioctl_ptr = static_cast<btrfs_ioctl_ino_lookup_args *>(this);
		return ioctl(fd, BTRFS_IOC_INO_LOOKUP, ioctl_ptr) == 0;
	}

	void
	BtrfsIoctlInoLookupArgs::do_ioctl(int fd) {
		if (!do_ioctl_nothrow(fd)) {
			THROW_ERRNO("BTRFS_IOC_INO_LOOKUP: " << name_fd(fd));
		}
	}

	BtrfsIoctlDefragRangeArgs::BtrfsIoctlDefragRangeArgs() :
		btrfs_ioctl_defrag_range_args( (btrfs_ioctl_defrag_range_args) { } )
	{
	}

	bool
	BtrfsIoctlDefragRangeArgs::do_ioctl_nothrow(int fd)
	{
		btrfs_ioctl_defrag_range_args *ioctl_ptr = static_cast<btrfs_ioctl_defrag_range_args *>(this);
		return 0 == ioctl(fd, BTRFS_IOC_DEFRAG_RANGE, ioctl_ptr);
	}

	void
	BtrfsIoctlDefragRangeArgs::do_ioctl(int fd)
	{
		if (!do_ioctl_nothrow(fd)) {
			THROW_ERRNO("BTRFS_IOC_DEFRAG_RANGE: " << name_fd(fd));
		}
	}

	string
	btrfs_ioctl_defrag_range_flags_ntoa(uint64_t flags)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_BITS(BTRFS_DEFRAG_RANGE_COMPRESS),
			NTOA_TABLE_ENTRY_BITS(BTRFS_DEFRAG_RANGE_START_IO),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(flags, table);
	}

	string
	btrfs_compress_type_ntoa(uint8_t compress_type)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_ENUM(BTRFS_COMPRESS_NONE),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_COMPRESS_ZLIB),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_COMPRESS_LZO),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_COMPRESS_ZSTD),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(compress_type, table);
	}

	ostream &
	operator<<(ostream &os, const BtrfsIoctlDefragRangeArgs *p)
	{
		if (!p) {
			return os << "BtrfsIoctlDefragRangeArgs NULL";
		}
		os << "BtrfsIoctlDefragRangeArgs {";
		os << " .start = " << p->start;
		os << " .len = " << p->len;
		os << " .flags = " << btrfs_ioctl_defrag_range_flags_ntoa(p->flags);
		os << " .extent_thresh = " << p->extent_thresh;
		os << " .compress_type = " << btrfs_compress_type_ntoa(p->compress_type);
		os << " .unused[4] = { " << p->unused[0] << ", " << p->unused[1] << ", " << p->unused[2] << ", " << p->unused[3] << "} }";
		return os;
	}

	FiemapExtent::FiemapExtent() :
		fiemap_extent( (fiemap_extent) { } )
	{
	}

	FiemapExtent::FiemapExtent(const fiemap_extent &that)
	{
		static_cast<fiemap_extent &>(*this) = that;
	}

	FiemapExtent::operator bool() const
	{
		return fe_length;
	}

	off_t
	FiemapExtent::begin() const
	{
		return ranged_cast<off_t>(fe_logical);
	}

	off_t
	FiemapExtent::end() const
	{
		return ranged_cast<off_t>(fe_logical + fe_length);
	}

	string
	fiemap_extent_flags_ntoa(unsigned long flags)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_LAST),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_UNKNOWN),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_DELALLOC),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_ENCODED),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_DATA_ENCRYPTED),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_NOT_ALIGNED),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_DATA_INLINE),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_DATA_TAIL),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_UNWRITTEN),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_MERGED),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_EXTENT_SHARED),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(flags, table);
	}

	ostream &
	operator<<(ostream &os, const fiemap_extent *args)
	{
		if (!args) {
			return os << "fiemap_extent NULL";
		}
		os << "fiemap_extent {";
		os << " .fe_logical = " << to_hex(args->fe_logical) << ".." << to_hex(args->fe_logical + args->fe_length);
		os << ", .fe_physical = " << to_hex(args->fe_physical) << ".." << to_hex(args->fe_physical + args->fe_length);
		os << ", .fe_length = " << to_hex(args->fe_length);
		if (args->fe_reserved64[0]) os << ", .fe_reserved64[0] = " << args->fe_reserved64[0];
		if (args->fe_reserved64[1]) os << ", .fe_reserved64[1] = " << args->fe_reserved64[1];
		if (args->fe_flags) os << ", .fe_flags = " << fiemap_extent_flags_ntoa(args->fe_flags);
		if (args->fe_reserved[0]) os << ", .fe_reserved[0] = " << args->fe_reserved[0];
		if (args->fe_reserved[1]) os << ", .fe_reserved[1] = " << args->fe_reserved[1];
		if (args->fe_reserved[2]) os << ", .fe_reserved[2] = " << args->fe_reserved[2];
		return os << " }";
	}

	ostream &
	operator<<(ostream &os, const FiemapExtent &args)
	{
		return os << static_cast<const fiemap_extent *>(&args);
	}

	string
	fiemap_flags_ntoa(unsigned long flags)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_BITS(FIEMAP_FLAGS_COMPAT),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_FLAG_SYNC),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_FLAG_XATTR),
			NTOA_TABLE_ENTRY_BITS(FIEMAP_FLAG_CACHE),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(flags, table);
	}

	ostream &
	operator<<(ostream &os, const fiemap *args)
	{
		if (!args) {
			return os << "fiemap NULL";
		}
		os << "fiemap {";
		os << " .fm_start = " << to_hex(args->fm_start) << ".." << to_hex(args->fm_start + args->fm_length);
		os << ", .fm_length = " << to_hex(args->fm_length);
		if (args->fm_flags) os << ", .fm_flags = " << fiemap_flags_ntoa(args->fm_flags);
		os << ", .fm_mapped_extents = " << args->fm_mapped_extents;
		os << ", .fm_extent_count = " << args->fm_extent_count;
		if (args->fm_reserved) os << ", .fm_reserved = " << args->fm_reserved;
		os << ", .fm_extents[] = {";
		for (uint32_t i = 0; i < args->fm_mapped_extents; ++i) {
			os << "\n\t[" << i << "] = " << &(args->fm_extents[i]) << ",";
		}
		return os << "\n}";
	}

	ostream &
	operator<<(ostream &os, const Fiemap &args)
	{
		os << "Fiemap {";
		os << " .m_start = " << to_hex(args.m_start) << ".." << to_hex(args.m_start + args.m_length);
		os << ", .m_length = " << to_hex(args.m_length);
		os << ", .m_flags = " << fiemap_flags_ntoa(args.m_flags);
		os << ", .fm_extents[" << args.m_extents.size() << "] = {";
		size_t count = 0;
		for (auto i = args.m_extents.cbegin(); i != args.m_extents.cend(); ++i) {
			os << "\n\t[" << count++ << "] = " << &(*i) << ",";
		}
		return os << "\n}";
	}

	Fiemap::Fiemap(uint64_t start, uint64_t length) :
		m_start(start),
		m_length(length)
	{
	}

	void
	Fiemap::do_ioctl(int fd)
	{
		THROW_CHECK1(out_of_range, m_min_count, m_min_count <= m_max_count);
		THROW_CHECK1(out_of_range, m_min_count, m_min_count > 0);

		const auto extent_count = m_min_count;
		ByteVector ioctl_arg(sizeof(fiemap) + extent_count * sizeof(fiemap_extent));

		fiemap *const ioctl_ptr = ioctl_arg.get<fiemap>();

		auto start = m_start;
		const auto end = m_start + m_length;

		vector<FiemapExtent> extents;

		while (start < end && extents.size() < m_max_count) {
			*ioctl_ptr = (fiemap) {
				.fm_start = start,
				.fm_length = end - start,
				.fm_flags = m_flags,
				.fm_extent_count = extent_count,
			};

			// cerr << "Before (fd = " << fd << ") : " << ioctl_ptr << endl;
			DIE_IF_MINUS_ONE(ioctl(fd, FS_IOC_FIEMAP, ioctl_ptr));
			// cerr << " After (fd = " << fd << ") : " << ioctl_ptr << endl;

			auto extents_left = ioctl_ptr->fm_mapped_extents;
			if (extents_left == 0) {
				start = end;
				break;
			}

			fiemap_extent *fep = ioctl_ptr->fm_extents;
			while (extents_left-- && extents.size() < m_max_count) {
				extents.push_back(FiemapExtent(*fep));
				if (fep->fe_flags & FIEMAP_EXTENT_LAST) {
					assert(extents_left == 0);
					start = end;
					break;
				} else {
					start = fep->fe_logical + fep->fe_length;
				}
				++fep;
			}
		}

		m_extents = extents;
	}

	BtrfsIoctlSearchKey::BtrfsIoctlSearchKey(size_t buf_size) :
		btrfs_ioctl_search_key( (btrfs_ioctl_search_key) {
			.max_objectid = numeric_limits<decltype(max_objectid)>::max(),
			.max_offset = numeric_limits<decltype(max_offset)>::max(),
			.max_transid = numeric_limits<decltype(max_transid)>::max(),
			.max_type = numeric_limits<decltype(max_type)>::max(),
			.nr_items = 1,
		}),
		m_buf_size(buf_size)
	{
	}

	BtrfsIoctlSearchHeader::BtrfsIoctlSearchHeader() :
		btrfs_ioctl_search_header( (btrfs_ioctl_search_header) { } )
	{
	}

	size_t
	BtrfsIoctlSearchHeader::set_data(const ByteVector &v, size_t offset)
	{
		THROW_CHECK2(invalid_argument, offset, v.size(), offset + sizeof(btrfs_ioctl_search_header) <= v.size());
		memcpy(static_cast<btrfs_ioctl_search_header *>(this), &v[offset], sizeof(btrfs_ioctl_search_header));
		offset += sizeof(btrfs_ioctl_search_header);
		THROW_CHECK2(invalid_argument, offset + len, v.size(), offset + len <= v.size());
		m_data = ByteVector(v, offset, len);
		return offset + len;
	}

	thread_local size_t BtrfsIoctlSearchKey::s_calls = 0;
	thread_local size_t BtrfsIoctlSearchKey::s_loops = 0;
	thread_local size_t BtrfsIoctlSearchKey::s_loops_empty = 0;

	bool
	BtrfsIoctlSearchKey::do_ioctl_nothrow(int fd)
	{
		// It would be really nice if the kernel tells us whether our
		// buffer overflowed or how big the overflowing object
		// was; instead, we have to guess.

		m_result.clear();
		// Make sure there is space for at least the search key and one (empty) header
		size_t buf_size = max(m_buf_size, sizeof(btrfs_ioctl_search_args_v2) + sizeof(btrfs_ioctl_search_header));
		ByteVector ioctl_arg;
		btrfs_ioctl_search_args_v2 *ioctl_ptr;
		do {
			// ioctl buffer size does not include search key header or buffer size
			ioctl_arg = ByteVector(buf_size + sizeof(btrfs_ioctl_search_args_v2));
			ioctl_ptr = ioctl_arg.get<btrfs_ioctl_search_args_v2>();
			ioctl_ptr->key = static_cast<const btrfs_ioctl_search_key&>(*this);
			ioctl_ptr->buf_size = buf_size;
			// Don't bother supporting V1.  Kernels that old have other problems.
			int rv = ioctl(fd, BTRFS_IOC_TREE_SEARCH_V2, ioctl_arg.data());
			++s_calls;
			if (rv != 0 && errno == ENOENT) {
				// If we are searching a tree that is deleted or no longer exists, just return an empty list
				nr_items = 0;
				break;
			}
			if (rv != 0 && errno != EOVERFLOW) {
				return false;
			}
			if (rv == 0 && nr_items <= ioctl_ptr->key.nr_items) {
				// got all the items we wanted, thanks
				m_buf_size = max(m_buf_size, buf_size);
				break;
			}
			// Didn't get all the items we wanted.  Increase the buf size and try again.
			// These sizes are very common on default-formatted btrfs, so use these
			// instead of naive doubling.
			if (buf_size < 4096) {
				buf_size = 4096;
			} else if (buf_size < 16384) {
				buf_size = 16384;
			} else if (buf_size < 65536) {
				buf_size = 65536;
			} else {
				buf_size *= 2;
			}
			// don't automatically raise the buf size higher than 64K, the largest possible btrfs item
			++s_loops;
			if (ioctl_ptr->key.nr_items == 0) {
				++s_loops_empty;
			}
		} while (buf_size < 65536);

		// ioctl changes nr_items, this has to be copied back
		static_cast<btrfs_ioctl_search_key&>(*this) = ioctl_ptr->key;

		size_t offset = pointer_distance(ioctl_ptr->buf, ioctl_ptr);
		for (decltype(nr_items) i = 0; i < nr_items; ++i) {
			BtrfsIoctlSearchHeader item;
			offset = item.set_data(ioctl_arg, offset);
			m_result.insert(item);
		}
		return true;
	}

	void
	BtrfsIoctlSearchKey::do_ioctl(int fd)
	{
		if (!do_ioctl_nothrow(fd)) {
			THROW_ERRNO("BTRFS_IOC_TREE_SEARCH_V2: " << name_fd(fd) << ": " << *this);
		}
	}

	void
	BtrfsIoctlSearchKey::next_min(const BtrfsIoctlSearchHeader &ref)
	{
		min_objectid = ref.objectid;
		min_type = ref.type;
		min_offset = ref.offset + 1;
		if (min_offset < ref.offset) {
			// We wrapped, try the next type
			++min_type;
			assert(min_offset == 0);
			if (min_type < ref.type) {
				assert(min_type == 0);
				// We wrapped, try the next objectid
				++min_objectid;
				// no advancement possible at end
				THROW_CHECK1(runtime_error, min_type, min_type == 0);
			}
		}
	}

	void
	BtrfsIoctlSearchKey::next_min(const BtrfsIoctlSearchHeader &ref, const uint8_t type)
	{
		if (ref.type < type) {
			// forward to type in same object with zero offset
			min_objectid = ref.objectid;
			min_type = type;
			min_offset = 0;
		} else if (ref.type > type) {
			// skip directly to start of next objectid with target type
			min_objectid = ref.objectid + 1;
			// no advancement possible at end
			THROW_CHECK2(out_of_range, min_objectid, ref.objectid, min_objectid > ref.objectid);
			min_type = type;
			min_offset = 0;
		} else {
			// advance within this type
			min_objectid = ref.objectid;
			min_type = ref.type;
			min_offset = ref.offset + 1;
			if (min_offset < ref.offset) {
				// We wrapped, try the next objectid, same type
				++min_objectid;
				THROW_CHECK2(out_of_range, min_objectid, ref.objectid, min_objectid > ref.objectid);
				min_type = type;
				assert(min_offset == 0);
			}
		}
	}

	string
	btrfs_search_type_ntoa(unsigned type)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_ENUM(BTRFS_INODE_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_INODE_REF_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_INODE_EXTREF_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_XATTR_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_ORPHAN_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DIR_LOG_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DIR_LOG_INDEX_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DIR_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DIR_INDEX_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_EXTENT_DATA_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_CSUM_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_EXTENT_CSUM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_ROOT_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_ROOT_BACKREF_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_ROOT_REF_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_EXTENT_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_METADATA_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_TREE_BLOCK_REF_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_EXTENT_DATA_REF_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_EXTENT_REF_V0_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_SHARED_BLOCK_REF_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_SHARED_DATA_REF_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_BLOCK_GROUP_ITEM_KEY),
#ifdef BTRFS_FREE_SPACE_INFO_KEY
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FREE_SPACE_INFO_KEY),
#endif
#ifdef BTRFS_FREE_SPACE_EXTENT_KEY
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FREE_SPACE_EXTENT_KEY),
#endif
#ifdef BTRFS_FREE_SPACE_BITMAP_KEY
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FREE_SPACE_BITMAP_KEY),
#endif
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DEV_EXTENT_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DEV_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_CHUNK_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_BALANCE_ITEM_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_QGROUP_STATUS_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_QGROUP_INFO_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_QGROUP_LIMIT_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_QGROUP_RELATION_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DEV_STATS_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DEV_REPLACE_KEY),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_UUID_KEY_SUBVOL),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_UUID_KEY_RECEIVED_SUBVOL),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_STRING_ITEM_KEY),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(type, table);
	}

	string
	btrfs_search_objectid_ntoa(uint64_t objectid)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_ENUM(BTRFS_ROOT_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_EXTENT_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_CHUNK_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DEV_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FS_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_ROOT_TREE_DIR_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_CSUM_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_QUOTA_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_UUID_TREE_OBJECTID),
#ifdef BTRFS_FREE_SPACE_TREE_OBJECTID
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FREE_SPACE_TREE_OBJECTID),
#endif
			NTOA_TABLE_ENTRY_ENUM(BTRFS_BALANCE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_ORPHAN_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_TREE_LOG_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_TREE_LOG_FIXUP_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_TREE_RELOC_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DATA_RELOC_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_EXTENT_CSUM_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FREE_SPACE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FREE_INO_OBJECTID),
		// One of these is not an objectid
			NTOA_TABLE_ENTRY_ENUM(BTRFS_MULTIPLE_OBJECTIDS),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FIRST_FREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_LAST_FREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_FIRST_CHUNK_TREE_OBJECTID),
			NTOA_TABLE_ENTRY_ENUM(BTRFS_DEV_ITEMS_OBJECTID),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(objectid, table);
	}

	ostream &
	operator<<(ostream &os, const btrfs_ioctl_search_key &key)
	{
		return os << "btrfs_ioctl_search_key {"
			<< " tree_id = " << key.tree_id
			<< ", min_objectid = " << key.min_objectid
			<< ", max_objectid = " << key.max_objectid
			<< ", min_offset = " << key.min_offset
			<< ", max_offset = " << key.max_offset
			<< ", min_transid = " << key.min_transid
			<< ", max_transid = " << key.max_transid
			<< ", min_type = " << key.min_type
			<< ", max_type = " << key.max_type
			<< ", nr_items = " << key.nr_items
			<< ", unused = " << key.unused
			<< ", unused1 = " << key.unused1
			<< ", unused2 = " << key.unused2
			<< ", unused3 = " << key.unused3
			<< ", unused4 = " << key.unused4
			<< " }";
	}

	ostream &
	operator<<(ostream &os, const btrfs_ioctl_search_header &hdr)
	{
		return os << "btrfs_ioctl_search_header {"
			<< " transid = " << hdr.transid
			<< ", objectid = " << btrfs_search_objectid_ntoa(hdr.objectid) << " (" << hdr.objectid << ")"
			<< ", offset = " << hdr.offset
			<< ", type = " << btrfs_search_type_ntoa(hdr.type) << " (" << hdr.type << ")"
			<< ", len = " << hdr.len
			<< " }";
	}

	ostream &
	operator<<(ostream &os, const BtrfsIoctlSearchHeader &hdr)
	{
		os << "BtrfsIoctlSearchHeader { "
			<< static_cast<const btrfs_ioctl_search_header &>(hdr)
			<< ", data = ";
		hexdump(os, hdr.m_data);
		return os << "}";
	}

	ostream &
	operator<<(ostream &os, const BtrfsIoctlSearchKey &key)
	{
		os << "BtrfsIoctlSearchKey { "
			<< static_cast<const btrfs_ioctl_search_key &>(key)
			<< ", buf_size = " << key.m_buf_size
			<< ", buf[" << key.m_result.size() << "] = {";
		for (auto e : key.m_result) {
			os << "\n\t" << e;
		}
		return os << "}}";
	}

	uint64_t
	btrfs_get_root_id(int fd)
	{
		BtrfsIoctlInoLookupArgs biila(BTRFS_FIRST_FREE_OBJECTID);
		biila.do_ioctl(fd);
		return biila.treeid;
	}

	uint64_t
	btrfs_get_root_transid(int fd)
	{
		BtrfsIoctlSearchKey sk;
		auto root_id = btrfs_get_root_id(fd);
		sk.tree_id = BTRFS_ROOT_TREE_OBJECTID;
		sk.min_objectid = root_id;
		sk.max_objectid = root_id;
		sk.max_type = BTRFS_ROOT_ITEM_KEY;
		sk.min_type = BTRFS_ROOT_ITEM_KEY;
		sk.nr_items = 4096;
		uint64_t rv = 0;
		do {
			sk.do_ioctl(fd);
			if (sk.nr_items == 0) {
				break;
			}
			for (auto i : sk.m_result) {
				sk.min_objectid = i.objectid;
				sk.min_type     = i.type;
				sk.min_offset   = i.offset;

				if (i.objectid > root_id) {
					break;
				}

				if (i.objectid == root_id && i.type == BTRFS_ROOT_ITEM_KEY) {
					rv = max(rv, uint64_t(btrfs_get_member(&btrfs_root_item::generation, i.m_data)));
				}
			}
			if (sk.min_offset < numeric_limits<decltype(sk.min_offset)>::max()) {
				++sk.min_offset;
			} else {
				break;
			}
		} while (sk.min_type == BTRFS_ROOT_ITEM_KEY && sk.min_objectid == sk.tree_id);
		return rv;
	}

	Statvfs::Statvfs() :
		statvfs( (statvfs) { } )
	{
	}

	Statvfs::Statvfs(int fd) :
		Statvfs()
	{
		DIE_IF_NON_ZERO(::fstatvfs(fd, this));
	}

	Statvfs::Statvfs(string path) :
		Statvfs()
	{
		DIE_IF_NON_ZERO(::statvfs(path.c_str(), this));
	}

	unsigned long
	Statvfs::size() const
	{
		return f_frsize * f_blocks;
	}

	unsigned long
	Statvfs::free() const
	{
		return f_frsize * f_bfree;
	}

	unsigned long
	Statvfs::available() const
	{
		return f_frsize * f_bavail;
	}

	ostream &
	operator<<(ostream &os, const BtrfsIoctlFsInfoArgs &a)
	{
		os << "BtrfsIoctlFsInfoArgs {"
			<< " max_id = " << a.max_id << ","
			<< " num_devices = " << a.num_devices << ","
#if 0
			<< " nodesize = " << a.nodesize << ","
			<< " sectorsize = " << a.sectorsize << ","
			<< " clone_alignment = " << a.clone_alignment << ","
			<< " reserved32 = " << a.reserved32;
#else
			;
#endif
		// probably don't need to bother with the other 122 reserved fields
		return os << " }";
	};

	BtrfsIoctlFsInfoArgs::BtrfsIoctlFsInfoArgs() :
		btrfs_ioctl_fs_info_args_v3( (btrfs_ioctl_fs_info_args_v3) {
			.flags = 0
				| BTRFS_FS_INFO_FLAG_CSUM_INFO
				| BTRFS_FS_INFO_FLAG_GENERATION
			,
		})
	{
	}

	void
	BtrfsIoctlFsInfoArgs::do_ioctl(int fd)
	{
		btrfs_ioctl_fs_info_args_v3 *p = static_cast<btrfs_ioctl_fs_info_args_v3 *>(this);
		if (ioctl(fd, BTRFS_IOC_FS_INFO, p)) {
			THROW_ERRNO("BTRFS_IOC_FS_INFO: fd " << fd);
		}
	}

	uint16_t
	BtrfsIoctlFsInfoArgs::csum_type() const
	{
		return this->btrfs_ioctl_fs_info_args_v3::csum_type;
	}

	uint16_t
	BtrfsIoctlFsInfoArgs::csum_size() const
	{
		return this->btrfs_ioctl_fs_info_args_v3::csum_size;
	}

	uint64_t
	BtrfsIoctlFsInfoArgs::generation() const
	{
		return this->btrfs_ioctl_fs_info_args_v3::generation;
	}

};
