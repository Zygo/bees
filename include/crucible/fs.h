#ifndef CRUCIBLE_FS_H
#define CRUCIBLE_FS_H

#include "crucible/error.h"

// Terribly Linux-specific FS-wrangling functions

// BTRFS
#include "crucible/btrfs.h"

// FIEMAP_* structs and flags
#include <linux/fiemap.h>

#include <cstdint>
#include <iosfwd>
#include <vector>

#include <fcntl.h>
#include <sys/statvfs.h>

namespace crucible {
	using namespace std;

	// wrapper around fallocate(...FALLOC_FL_PUNCH_HOLE...)
	void punch_hole(int fd, off_t offset, off_t len);

	struct BtrfsExtentInfo : public btrfs_ioctl_same_extent_info {
		BtrfsExtentInfo(int dst_fd, off_t dst_offset);
	};

	struct BtrfsExtentSame : public btrfs_ioctl_same_args {
		virtual ~BtrfsExtentSame();
		BtrfsExtentSame(int src_fd, off_t src_offset, off_t src_length);
		void add(int fd, off_t offset);
		virtual void do_ioctl();

		int m_fd;
		vector<BtrfsExtentInfo> m_info;
	};

	struct BtrfsExtentSameByClone : public BtrfsExtentSame {
		using BtrfsExtentSame::BtrfsExtentSame;
		void do_ioctl() override;
	};

	ostream & operator<<(ostream &os, const btrfs_ioctl_same_extent_info *info);
	ostream & operator<<(ostream &os, const btrfs_ioctl_same_args *info);
	ostream & operator<<(ostream &os, const BtrfsExtentSame &bes);

	struct BtrfsInodeOffsetRoot {
		uint64_t m_inum;
		uint64_t m_offset;
		uint64_t m_root;
	};

	ostream & operator<<(ostream &os, const BtrfsInodeOffsetRoot &p);

	struct BtrfsDataContainer : public btrfs_data_container {
		BtrfsDataContainer(size_t size = 64 * 1024);
		void *prepare();

		size_t get_size() const;
		decltype(bytes_left) get_bytes_left() const;
		decltype(bytes_missing) get_bytes_missing() const;
		decltype(elem_cnt) get_elem_cnt() const;
		decltype(elem_missed) get_elem_missed() const;

		vector<char> m_data;
	};

	struct BtrfsIoctlLogicalInoArgs : public btrfs_ioctl_logical_ino_args {
		BtrfsIoctlLogicalInoArgs(uint64_t logical, size_t buf_size = 64 * 1024);
		virtual void do_ioctl(int fd);
		virtual bool do_ioctl_nothrow(int fd);

		BtrfsDataContainer m_container;
		vector<BtrfsInodeOffsetRoot> m_iors;
	};

	ostream & operator<<(ostream &os, const BtrfsIoctlLogicalInoArgs &p);

	struct BtrfsIoctlInoPathArgs : public btrfs_ioctl_ino_path_args {
		BtrfsIoctlInoPathArgs(uint64_t inode, size_t buf_size = 64 * 1024);
		virtual void do_ioctl(int fd);
		virtual bool do_ioctl_nothrow(int fd);

		BtrfsDataContainer m_container;
		vector<string> m_paths;
	};

	ostream & operator<<(ostream &os, const BtrfsIoctlInoPathArgs &p);

	struct BtrfsIoctlInoLookupArgs : public btrfs_ioctl_ino_lookup_args {
		BtrfsIoctlInoLookupArgs(uint64_t objectid);
		virtual void do_ioctl(int fd);
		virtual bool do_ioctl_nothrow(int fd);
		// use objectid = BTRFS_FIRST_FREE_OBJECTID
		// this->treeid is the rootid for the path (we get the path too)
	};

	struct BtrfsIoctlDefragRangeArgs : public btrfs_ioctl_defrag_range_args {
		BtrfsIoctlDefragRangeArgs();
		virtual void do_ioctl(int fd);
		virtual bool do_ioctl_nothrow(int fd);
	};

	ostream & operator<<(ostream &os, const BtrfsIoctlDefragRangeArgs *p);

	// in btrfs/ctree.h, but that's a nightmare to #include here
	typedef enum {
		BTRFS_COMPRESS_NONE  = 0,
		BTRFS_COMPRESS_ZLIB  = 1,
		BTRFS_COMPRESS_LZO   = 2,
		BTRFS_COMPRESS_TYPES = 2,
		BTRFS_COMPRESS_LAST  = 3,
	} btrfs_compression_type;

	struct FiemapExtent : public fiemap_extent {
		FiemapExtent();
		FiemapExtent(const fiemap_extent &that);
		operator bool() const;
		off_t begin() const;
		off_t end() const;
	};

	struct Fiemap : public fiemap {

		// Get entire file
		Fiemap(uint64_t start = 0, uint64_t length = FIEMAP_MAX_OFFSET);

		void do_ioctl(int fd);

		vector<FiemapExtent> m_extents;
		uint64_t m_min_count = (4096 - sizeof(fiemap)) / sizeof(fiemap_extent);
		uint64_t m_max_count = 16 * 1024 * 1024 / sizeof(fiemap_extent);
	};

	ostream & operator<<(ostream &os, const fiemap_extent *info);
	ostream & operator<<(ostream &os, const FiemapExtent &info);
	ostream & operator<<(ostream &os, const fiemap *info);
	ostream & operator<<(ostream &os, const Fiemap &info);

	string fiemap_extent_flags_ntoa(unsigned long flags);

	// Helper functions
	void btrfs_clone_range(int src_fd, off_t src_offset, off_t src_length, int dst_fd, off_t dst_offset);
	bool btrfs_extent_same(int src_fd, off_t src_offset, off_t src_length, int dst_fd, off_t dst_offset);

	struct BtrfsIoctlSearchHeader : public btrfs_ioctl_search_header {
		BtrfsIoctlSearchHeader();
		vector<char> m_data;
		size_t set_data(const vector<char> &v, size_t offset);
	};

	ostream & operator<<(ostream &os, const btrfs_ioctl_search_header &hdr);
	ostream & operator<<(ostream &os, const BtrfsIoctlSearchHeader &hdr);

	struct BtrfsIoctlSearchKey : public btrfs_ioctl_search_key {
		BtrfsIoctlSearchKey(size_t buf_size = 1024 * 1024);
		virtual bool do_ioctl_nothrow(int fd);
		virtual void do_ioctl(int fd);

		// Copy objectid/type/offset so we move forward
		void next_min(const BtrfsIoctlSearchHeader& ref);

		size_t m_buf_size;
		vector<BtrfsIoctlSearchHeader> m_result;
	};

	ostream & operator<<(ostream &os, const btrfs_ioctl_search_key &key);
	ostream & operator<<(ostream &os, const BtrfsIoctlSearchKey &key);

	string btrfs_search_type_ntoa(unsigned type);
	string btrfs_search_objectid_ntoa(unsigned objectid);

	uint64_t btrfs_get_root_id(int fd);
	uint64_t btrfs_get_root_transid(int fd);

	template<class T>
	const T*
	get_struct_ptr(vector<char> &v, size_t offset = 0)
	{
		// OK so sometimes btrfs overshoots a little
		if (offset + sizeof(T) > v.size()) {
			v.resize(offset + sizeof(T), 0);
		}
		THROW_CHECK2(invalid_argument, v.size(), offset + sizeof(T), offset + sizeof(T) <= v.size());
		return reinterpret_cast<const T*>(v.data() + offset);
	}

	template<class A, class R>
	R
	call_btrfs_get(R (*func)(const A*), vector<char> &v, size_t offset = 0)
	{
		return func(get_struct_ptr<A>(v, offset));
	}

	template <class T> struct btrfs_get_le;

	template<> struct btrfs_get_le<__le64> {
		uint64_t operator()(const void *p) { return get_unaligned_le64(p); }
	};

	template<> struct btrfs_get_le<__le32> {
		uint32_t operator()(const void *p) { return get_unaligned_le32(p); }
	};

	template<> struct btrfs_get_le<__le16> {
		uint16_t operator()(const void *p) { return get_unaligned_le16(p); }
	};

	template<> struct btrfs_get_le<__le8> {
		uint8_t operator()(const void *p) { return get_unaligned_le8(p); }
	};

	template<class S, class T>
	T
	btrfs_get_member(T S::* member, vector<char> &v, size_t offset = 0)
	{
		const S *sp = reinterpret_cast<const S*>(NULL);
		const T *spm = &(sp->*member);
		auto member_offset = reinterpret_cast<const char *>(spm) - reinterpret_cast<const char *>(sp);
		return btrfs_get_le<T>()(get_struct_ptr<S>(v, offset + member_offset));
	}

	struct Statvfs : public statvfs {
		Statvfs();
		Statvfs(string path);
		Statvfs(int fd);
		unsigned long size() const;
		unsigned long free() const;
		unsigned long available() const;
	};

	ostream &hexdump(ostream &os, const vector<char> &v);

	struct BtrfsIoctlFsInfoArgs : public btrfs_ioctl_fs_info_args {
		BtrfsIoctlFsInfoArgs();
		void do_ioctl(int fd);
		string uuid() const;
	};

	ostream & operator<<(ostream &os, const BtrfsIoctlFsInfoArgs &a);
};

#endif // CRUCIBLE_FS_H
