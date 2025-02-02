#ifndef CRUCIBLE_FS_H
#define CRUCIBLE_FS_H

#include "crucible/bytevector.h"
#include "crucible/endian.h"
#include "crucible/error.h"

// Terribly Linux-specific FS-wrangling functions

// BTRFS
#include "crucible/btrfs.h"

// FIEMAP_* structs and flags
#include <linux/fiemap.h>

#include <cstdint>
#include <iosfwd>
#include <set>
#include <vector>

#include <fcntl.h>
#include <sys/statvfs.h>

namespace crucible {
	using namespace std;

	// wrapper around fallocate(...FALLOC_FL_PUNCH_HOLE...)
	void punch_hole(int fd, off_t offset, off_t len);

	struct BtrfsExtentSame {
		virtual ~BtrfsExtentSame();
		BtrfsExtentSame(int src_fd, off_t src_offset, off_t src_length);
		void add(int fd, uint64_t offset);
		virtual void do_ioctl();

		uint64_t m_logical_offset = 0;
		uint64_t m_length = 0;
		int m_fd;
		vector<btrfs_ioctl_same_extent_info> m_info;
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

	struct BtrfsDataContainer {
		BtrfsDataContainer(size_t size = 64 * 1024);
		void *prepare(size_t size);

		size_t get_size() const;
		decltype(btrfs_data_container::bytes_left) get_bytes_left() const;
		decltype(btrfs_data_container::bytes_missing) get_bytes_missing() const;
		decltype(btrfs_data_container::elem_cnt) get_elem_cnt() const;
		decltype(btrfs_data_container::elem_missed) get_elem_missed() const;

		ByteVector m_data;
	};

	struct BtrfsIoctlLogicalInoArgs {
		BtrfsIoctlLogicalInoArgs(uint64_t logical, size_t buf_size = 16 * 1024 * 1024);

		uint64_t get_flags() const;
		void set_flags(uint64_t new_flags);
		void set_logical(uint64_t new_logical);
		void set_size(uint64_t new_size);

		void do_ioctl(int fd);
		bool do_ioctl_nothrow(int fd);

		struct BtrfsInodeOffsetRootSpan {
			using iterator = BtrfsInodeOffsetRoot*;
			using const_iterator = const BtrfsInodeOffsetRoot*;
			size_t size() const;
			iterator begin() const;
			iterator end() const;
			const_iterator cbegin() const;
			const_iterator cend() const;
			iterator data() const;
			void clear();
		private:
			iterator m_begin = nullptr;
			iterator m_end = nullptr;
		friend struct BtrfsIoctlLogicalInoArgs;
		} m_iors;
	private:
		size_t m_container_size;
		BtrfsDataContainer m_container;
		uint64_t m_logical;
		uint64_t m_flags = 0;
	friend ostream & operator<<(ostream &os, const BtrfsIoctlLogicalInoArgs *p);
	};

	ostream & operator<<(ostream &os, const BtrfsIoctlLogicalInoArgs &p);

	struct BtrfsIoctlInoPathArgs : public btrfs_ioctl_ino_path_args {
		BtrfsIoctlInoPathArgs(uint64_t inode, size_t buf_size = 64 * 1024);
		virtual void do_ioctl(int fd);
		virtual bool do_ioctl_nothrow(int fd);

		size_t m_container_size;
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

	struct FiemapExtent : public fiemap_extent {
		FiemapExtent();
		FiemapExtent(const fiemap_extent &that);
		operator bool() const;
		off_t begin() const;
		off_t end() const;
	};

	struct Fiemap {

		// because fiemap.h insists on giving FIEMAP_MAX_OFFSET
		// a different type from the struct fiemap members
		static const uint64_t s_fiemap_max_offset = FIEMAP_MAX_OFFSET;

		// Get entire file
		Fiemap(uint64_t start = 0, uint64_t length = s_fiemap_max_offset);

		void do_ioctl(int fd);

		vector<FiemapExtent> m_extents;
		decltype(fiemap::fm_extent_count) m_min_count = (4096 - sizeof(fiemap)) / sizeof(fiemap_extent);
		decltype(fiemap::fm_extent_count) m_max_count = 16 * 1024 * 1024 / sizeof(fiemap_extent);
		uint64_t m_start;
		uint64_t m_length;
		// FIEMAP is slow and full of lies.
		// This makes FIEMAP even slower, but reduces the lies a little.
		decltype(fiemap::fm_flags) m_flags = FIEMAP_FLAG_SYNC;
	friend ostream &operator<<(ostream &, const Fiemap &);
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
		ByteVector m_data;
		size_t set_data(const ByteVector &v, size_t offset);
		bool operator<(const BtrfsIoctlSearchHeader &that) const;
	};

	// Perf blames this function for a few percent overhead; move it here so it can be inline
	inline bool BtrfsIoctlSearchHeader::operator<(const BtrfsIoctlSearchHeader &that) const
	{
		return tie(objectid, type, offset, len, transid) < tie(that.objectid, that.type, that.offset, that.len, that.transid);
	}

	ostream & operator<<(ostream &os, const btrfs_ioctl_search_header &hdr);
	ostream & operator<<(ostream &os, const BtrfsIoctlSearchHeader &hdr);

	struct BtrfsIoctlSearchKey : public btrfs_ioctl_search_key {
		BtrfsIoctlSearchKey(size_t buf_size = 1024);
		bool do_ioctl_nothrow(int fd);
		void do_ioctl(int fd);

		// Copy objectid/type/offset so we move forward
		void next_min(const BtrfsIoctlSearchHeader& ref);

		// move forward to next object of a single type
		void next_min(const BtrfsIoctlSearchHeader& ref, const uint8_t type);

		size_t m_buf_size;
		set<BtrfsIoctlSearchHeader> m_result;

		static thread_local size_t s_calls;
		static thread_local size_t s_loops;
		static thread_local size_t s_loops_empty;
		static thread_local shared_ptr<ostream> s_debug_ostream;
	};

	ostream & operator<<(ostream &os, const btrfs_ioctl_search_key &key);
	ostream & operator<<(ostream &os, const BtrfsIoctlSearchKey &key);

	string btrfs_chunk_type_ntoa(uint64_t type);
	string btrfs_search_type_ntoa(unsigned type);
	string btrfs_search_objectid_ntoa(uint64_t objectid);
	string btrfs_compress_type_ntoa(uint8_t type);

	uint64_t btrfs_get_root_id(int fd);
	uint64_t btrfs_get_root_transid(int fd);

	template<class T, class V>
	const T*
	get_struct_ptr(const V &v, size_t offset = 0)
	{
		THROW_CHECK2(out_of_range, v.size(), offset + sizeof(T), offset + sizeof(T) <= v.size());
		const uint8_t *const data_ptr = v.data();
		return reinterpret_cast<const T*>(data_ptr + offset);
	}

	template<class S, class T, class V>
	T
	btrfs_get_member(T S::* member, V &v, size_t offset = 0)
	{
		const S *const sp = nullptr;
		const T *const spm = &(sp->*member);
		const auto member_offset = reinterpret_cast<const uint8_t *>(spm) - reinterpret_cast<const uint8_t *>(sp);
		const void *struct_ptr = get_struct_ptr<T>(v, offset + member_offset);
		const T unaligned_t = get_unaligned<T>(struct_ptr);
		return le_to_cpu(unaligned_t);
	}

	struct Statvfs : public statvfs {
		Statvfs();
		Statvfs(string path);
		Statvfs(int fd);
		unsigned long size() const;
		unsigned long free() const;
		unsigned long available() const;
	};

	struct BtrfsIoctlFsInfoArgs : public btrfs_ioctl_fs_info_args_v3 {
		BtrfsIoctlFsInfoArgs();
		void do_ioctl(int fd);
		bool do_ioctl_nothrow(int fd);
		uint16_t csum_type() const;
		uint16_t csum_size() const;
		uint64_t generation() const;
		vector<uint8_t> fsid() const;
	};

	ostream & operator<<(ostream &os, const BtrfsIoctlFsInfoArgs &a);
};

#endif // CRUCIBLE_FS_H
