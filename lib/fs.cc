#include "crucible/fs.h"

#include "crucible/error.h"
#include "crucible/fd.h"
#include "crucible/limits.h"
#include "crucible/ntoa.h"
#include "crucible/string.h"
#include "crucible/uuid.h"

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

	BtrfsExtentInfo::BtrfsExtentInfo(int dst_fd, off_t dst_offset)
	{
		memset_zero<btrfs_ioctl_same_extent_info>(this);
		fd = dst_fd;
		logical_offset = dst_offset;
	}

	BtrfsExtentSame::BtrfsExtentSame(int src_fd, off_t src_offset, off_t src_length) :
		m_fd(src_fd)
	{
		memset_zero<btrfs_ioctl_same_args>(this);
		logical_offset = src_offset;
		length = src_length;
	}

	BtrfsExtentSame::~BtrfsExtentSame()
	{
	}

	void
	BtrfsExtentSame::add(int fd, off_t offset)
	{
		m_info.push_back(BtrfsExtentInfo(fd, offset));
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
		os << ", .logical_offset = " << to_hex(bes.logical_offset);
		os << ", .length = " << to_hex(bes.length);
		os << ", .dest_count = " << bes.dest_count;
		os << ", .reserved1 = " << bes.reserved1;
		os << ", .reserved2 = " << bes.reserved2;
		os << ", .info[] = {";
		for (size_t i = 0; i < bes.m_info.size(); ++i) {
			os << " [" << i << "] = " << &(bes.m_info[i]) << ",";
		}
		return os << " }";
	}

	void
	btrfs_clone_range(int src_fd, off_t src_offset, off_t src_length, int dst_fd, off_t dst_offset)
	{
		struct btrfs_ioctl_clone_range_args args;
		memset_zero(&args);
		args.src_fd = src_fd;
		args.src_offset = src_offset;
		args.src_length = src_length;
		args.dest_offset = dst_offset;
		DIE_IF_MINUS_ONE(ioctl(dst_fd, BTRFS_IOC_CLONE_RANGE, &args));
	}

	// Userspace emulation of extent-same ioctl to work around kernel bugs
	// (a memory leak, a deadlock, inability to cope with unaligned EOF, and a length limit)
	// The emulation is incomplete:  no locking, and we always change ctime
	void
	BtrfsExtentSameByClone::do_ioctl()
	{
		if (length <= 0) {
			throw out_of_range(string("length = 0 in ") + __PRETTY_FUNCTION__);
		}
		vector<char> cmp_buf_common(length);
		vector<char> cmp_buf_iter(length);
		pread_or_die(m_fd, cmp_buf_common.data(), length, logical_offset);
		for (auto i = m_info.begin(); i != m_info.end(); ++i) {
			i->status = -EIO;
			i->bytes_deduped = 0;

			// save atime/ctime for later
			Stat target_stat(i->fd);

			pread_or_die(i->fd, cmp_buf_iter.data(), length, i->logical_offset);
			if (cmp_buf_common == cmp_buf_iter) {

				// This never happens, so stop checking.
				// assert(!memcmp(cmp_buf_common.data(), cmp_buf_iter.data(), length));

				btrfs_clone_range(m_fd, logical_offset, length, i->fd, i->logical_offset);
				i->status = 0;
				i->bytes_deduped = length;

				// The extent-same ioctl does not change mtime (as of patch v4)
				struct timespec restore_ts[2] = {
					target_stat.st_atim,
					target_stat.st_mtim
				};

				// Ignore futimens failure as the real extent-same ioctl would never raise it
				futimens(i->fd, restore_ts);

			} else {
				assert(memcmp(cmp_buf_common.data(), cmp_buf_iter.data(), length));
				i->status = BTRFS_SAME_DATA_DIFFERS;
			}
		}
	}

	void
	BtrfsExtentSame::do_ioctl()
	{
		dest_count = m_info.size();
		vector<char> ioctl_arg = vector_copy_struct<btrfs_ioctl_same_args>(this);
		ioctl_arg.resize(sizeof(btrfs_ioctl_same_args) + dest_count * sizeof(btrfs_ioctl_same_extent_info), 0);
		btrfs_ioctl_same_args *ioctl_ptr = reinterpret_cast<btrfs_ioctl_same_args *>(ioctl_arg.data());
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
			off_t length = min(off_t(BTRFS_MAX_DEDUPE_LEN), src_length);
			BtrfsExtentSame bes(src_fd, src_offset, length);
			bes.add(dst_fd, dst_offset);
			bes.do_ioctl();
			auto status = bes.m_info.at(0).status;
			if (status == 0) {
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
		m_data(buf_size, 0)
	{
	}

	void *
	BtrfsDataContainer::prepare()
	{
		btrfs_data_container *p = reinterpret_cast<btrfs_data_container *>(m_data.data());
		size_t min_size = offsetof(btrfs_data_container, val);
		size_t container_size = m_data.size();
		if (container_size < min_size) {
			THROW_ERROR(out_of_range, "container size " << container_size << " smaller than minimum " << min_size);
		}
		p->bytes_left = 0;
		p->bytes_missing = 0;
		p->elem_cnt = 0;
		p->elem_missed = 0;
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
		return bytes_left;
	}

	decltype(btrfs_data_container::bytes_missing)
	BtrfsDataContainer::get_bytes_missing() const
	{
		return bytes_missing;
	}

	decltype(btrfs_data_container::elem_cnt)
	BtrfsDataContainer::get_elem_cnt() const
	{
		return elem_cnt;
	}

	decltype(btrfs_data_container::elem_missed)
	BtrfsDataContainer::get_elem_missed() const
	{
		return elem_missed;
	}

	ostream &
	operator<<(ostream &os, const BtrfsIoctlLogicalInoArgs *p)
	{
		if (!p) {
			return os << "BtrfsIoctlLogicalInoArgs NULL";
		}
		os << "BtrfsIoctlLogicalInoArgs {";
		os << " .logical = " << to_hex(p->logical);
		os << " .inodes[] = {\n";
		unsigned count = 0;
		for (auto i = p->m_iors.cbegin(); i != p->m_iors.cend(); ++i) {
			os << "\t\t[" << count++ << "] = " << *i << ",\n";
		}
		os << "}\n";
		return os;
	}

	BtrfsIoctlLogicalInoArgs::BtrfsIoctlLogicalInoArgs(uint64_t new_logical, size_t new_size) :
		m_container(new_size)
	{
		memset_zero<btrfs_ioctl_logical_ino_args>(this);
		logical = new_logical;
	}

	bool
	BtrfsIoctlLogicalInoArgs::do_ioctl_nothrow(int fd)
	{
		btrfs_ioctl_logical_ino_args *p = static_cast<btrfs_ioctl_logical_ino_args *>(this);
		inodes = reinterpret_cast<uint64_t>(m_container.prepare());
		size = m_container.get_size();

		m_iors.clear();

		if (ioctl(fd, BTRFS_IOC_LOGICAL_INO, p)) {
			return false;
		}

		btrfs_data_container *bdc = reinterpret_cast<btrfs_data_container *>(p->inodes);
		BtrfsInodeOffsetRoot *input_iter = reinterpret_cast<BtrfsInodeOffsetRoot *>(bdc->val);
		m_iors.reserve(bdc->elem_cnt);

		for (auto count = bdc->elem_cnt; count > 2; count -= 3) {
			m_iors.push_back(*input_iter++);
		}

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
		m_container(new_size)
	{
		memset_zero<btrfs_ioctl_ino_path_args>(this);
		inum = inode;
	}

	bool
	BtrfsIoctlInoPathArgs::do_ioctl_nothrow(int fd)
	{
		btrfs_ioctl_ino_path_args *p = static_cast<btrfs_ioctl_ino_path_args *>(this);
		fspath = reinterpret_cast<uint64_t>(m_container.prepare());
		size = m_container.get_size();

		m_paths.clear();

		if (ioctl(fd, BTRFS_IOC_INO_PATHS, p) < 0) {
			return false;
		}

		btrfs_data_container *bdc = reinterpret_cast<btrfs_data_container *>(p->fspath);
		m_paths.reserve(bdc->elem_cnt);

		const uint64_t *up = reinterpret_cast<const uint64_t *>(bdc->val);
		const char *cp = reinterpret_cast<const char *>(bdc->val);

		for (auto count = bdc->elem_cnt; count > 0; --count) {
			const char *path = cp + *up++;
			if (static_cast<size_t>(path - cp) > m_container.get_size()) {
				THROW_ERROR(out_of_range, "offset " << (path - cp) << " > size " << m_container.get_size() << " in " << __PRETTY_FUNCTION__);
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

	BtrfsIoctlInoLookupArgs::BtrfsIoctlInoLookupArgs(uint64_t new_objectid)
	{
		memset_zero<btrfs_ioctl_ino_lookup_args>(this);
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

	BtrfsIoctlDefragRangeArgs::BtrfsIoctlDefragRangeArgs()
	{
		memset_zero<btrfs_ioctl_defrag_range_args>(this);
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
	btrfs_ioctl_defrag_range_compress_type_ntoa(uint32_t compress_type)
	{
		static const bits_ntoa_table table[] = {
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
		os << " .compress_type = " << btrfs_ioctl_defrag_range_compress_type_ntoa(p->compress_type);
		os << " .unused[4] = { " << p->unused[0] << ", " << p->unused[1] << ", " << p->unused[2] << ", " << p->unused[3] << "} }";
		return os;
	}

	FiemapExtent::FiemapExtent()
	{
		memset_zero<fiemap_extent>(this);
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
		os << " .fm_start = " << to_hex(args.fm_start) << ".." << to_hex(args.fm_start + args.fm_length);
		os << ", .fm_length = " << to_hex(args.fm_length);
		if (args.fm_flags) os << ", .fm_flags = " << fiemap_flags_ntoa(args.fm_flags);
		os << ", .fm_mapped_extents = " << args.fm_mapped_extents;
		os << ", .fm_extent_count = " << args.fm_extent_count;
		if (args.fm_reserved) os << ", .fm_reserved = " << args.fm_reserved;
		os << ", .fm_extents[] = {";
		size_t count = 0;
		for (auto i = args.m_extents.cbegin(); i != args.m_extents.cend(); ++i) {
			os << "\n\t[" << count++ << "] = " << &(*i) << ",";
		}
		return os << "\n}";
	}

	Fiemap::Fiemap(uint64_t start, uint64_t length)
	{
		memset_zero<fiemap>(this);
		fm_start = start;
		fm_length = length;
		// FIEMAP is slow and full of lines.
		// This makes FIEMAP even slower, but reduces the lies a little.
		fm_flags = FIEMAP_FLAG_SYNC;
	}

	void
	Fiemap::do_ioctl(int fd)
	{
		CHECK_CONSTRAINT(m_min_count, m_min_count <= m_max_count);

		auto extent_count = m_min_count;
		vector<char> ioctl_arg = vector_copy_struct<fiemap>(this);

		ioctl_arg.resize(sizeof(fiemap) + extent_count * sizeof(fiemap_extent), 0);

		fiemap *ioctl_ptr = reinterpret_cast<fiemap *>(ioctl_arg.data());

		auto start = fm_start;
		auto end = fm_start + fm_length;

		auto orig_start = fm_start;
		auto orig_length = fm_length;

		vector<FiemapExtent> extents;

		while (start < end && extents.size() < m_max_count) {
			ioctl_ptr->fm_start = start;
			ioctl_ptr->fm_length = end - start;
			ioctl_ptr->fm_extent_count = extent_count;
			ioctl_ptr->fm_mapped_extents = 0;

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

		fiemap *this_ptr = static_cast<fiemap *>(this);
		*this_ptr = *ioctl_ptr;
		fm_start = orig_start;
		fm_length = orig_length;
		fm_extent_count = extents.size();
		m_extents = extents;
	}

	BtrfsIoctlSearchKey::BtrfsIoctlSearchKey(size_t buf_size) :
		m_buf_size(buf_size)
	{
		memset_zero<btrfs_ioctl_search_key>(this);
		max_objectid = numeric_limits<decltype(max_objectid)>::max();
		max_offset = numeric_limits<decltype(max_offset)>::max();
		max_transid = numeric_limits<decltype(max_transid)>::max();
		max_type = numeric_limits<decltype(max_type)>::max();
		nr_items = numeric_limits<decltype(nr_items)>::max();
	}

	BtrfsIoctlSearchHeader::BtrfsIoctlSearchHeader()
	{
		memset_zero<btrfs_ioctl_search_header>(this);
	}

	size_t
	BtrfsIoctlSearchHeader::set_data(const vector<char> &v, size_t offset)
	{
		THROW_CHECK2(invalid_argument, offset, v.size(), offset + sizeof(btrfs_ioctl_search_header) <= v.size());
		memcpy(this, &v[offset], sizeof(btrfs_ioctl_search_header));
		offset += sizeof(btrfs_ioctl_search_header);
		THROW_CHECK2(invalid_argument, offset + len, v.size(), offset + len <= v.size());
		m_data = vector<char>(&v[offset], &v[offset + len]);
		return offset + len;
	}

	bool
	BtrfsIoctlSearchHeader::operator<(const BtrfsIoctlSearchHeader &that) const
	{
		return tie(objectid, type, offset, len, transid) < tie(that.objectid, that.type, that.offset, that.len, that.transid);
	}

	bool
	BtrfsIoctlSearchKey::do_ioctl_nothrow(int fd)
	{
		// Normally we like to be paranoid and fill empty bytes with zero,
		// but these buffers can be huge.  80% of a 4GHz CPU huge.

		// Keep the ioctl buffer from one run to the next to save on malloc costs
		size_t target_buf_size = sizeof(btrfs_ioctl_search_args_v2) + m_buf_size;

		thread_local vector<char> ioctl_arg;
		if (ioctl_arg.size() < m_buf_size) {
			ioctl_arg = vector_copy_struct<btrfs_ioctl_search_key>(this);
			ioctl_arg.resize(target_buf_size);
		} else {
			memcpy(ioctl_arg.data(), static_cast<btrfs_ioctl_search_key*>(this), sizeof(btrfs_ioctl_search_key));
		}

		btrfs_ioctl_search_args_v2 *ioctl_ptr = reinterpret_cast<btrfs_ioctl_search_args_v2 *>(ioctl_arg.data());

		ioctl_ptr->buf_size = m_buf_size;

		// Don't bother supporting V1.  Kernels that old have other problems.
		int rv = ioctl(fd, BTRFS_IOC_TREE_SEARCH_V2, ioctl_ptr);
		if (rv != 0) {
			return false;
		}

		static_cast<btrfs_ioctl_search_key&>(*this) = ioctl_ptr->key;

		m_result.clear();

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
			THROW_ERRNO("BTRFS_IOC_TREE_SEARCH_V2: " << name_fd(fd));
		}
	}

	void
	BtrfsIoctlSearchKey::next_min(const BtrfsIoctlSearchHeader &ref)
	{
		min_objectid = ref.objectid;
		min_type = ref.type;
		min_offset = ref.offset + 1;
		if (min_offset < ref.offset) {
			// We wrapped, try the next objectid
			++min_objectid;
		}
	}

	ostream &hexdump(ostream &os, const vector<char> &v)
	{
		os << "vector<char> { size = " << v.size() << ", data:\n";
		for (size_t i = 0; i < v.size(); i += 8) {
			string hex, ascii;
			for (size_t j = i; j < i + 8; ++j) {
				if (j < v.size()) {
					unsigned char c = v[j];
					char buf[8];
					sprintf(buf, "%02x ", c);
					hex += buf;
					ascii += (c < 32 || c > 126) ? '.' : c;
				} else {
					hex += "   ";
					ascii += ' ';
				}
			}
			os << astringprintf("\t%08x %s %s\n", i, hex.c_str(), ascii.c_str());
		}
		return os << "}";
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
					rv = max(rv, uint64_t(call_btrfs_get(btrfs_root_generation, i.m_data)));
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

	Statvfs::Statvfs()
	{
		memset_zero<statvfs>(this);
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
			<< " fsid = " << a.uuid() << ","
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

	BtrfsIoctlFsInfoArgs::BtrfsIoctlFsInfoArgs()
	{
		memset_zero<btrfs_ioctl_fs_info_args>(this);
	}

	void
	BtrfsIoctlFsInfoArgs::do_ioctl(int fd)
	{
		btrfs_ioctl_fs_info_args *p = static_cast<btrfs_ioctl_fs_info_args *>(this);
		if (ioctl(fd, BTRFS_IOC_FS_INFO, p)) {
			THROW_ERRNO("BTRFS_IOC_FS_INFO: fd " << fd);
		}
	}

	string
	BtrfsIoctlFsInfoArgs::uuid() const
	{
		return uuid_unparse(fsid);
	}

};
