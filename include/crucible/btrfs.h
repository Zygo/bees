#ifndef CRUCIBLE_BTRFS_H
#define CRUCIBLE_BTRFS_H

// Copied from Linux kernel sources as of 3.15 or so.
// These are probably missing from /usr/include at the moment.

// NULL
#include <cstdio>

// _IOWR macro and friends
#include <asm-generic/ioctl.h>

// __u64 typedef and friends
#include <linux/types.h>

// the btrfs headers
#include <linux/btrfs.h>
#include <linux/btrfs_tree.h>

// And now all the things that have been missing in some version of
// the headers.

enum btrfs_compression_type {
	BTRFS_COMPRESS_NONE,
	BTRFS_COMPRESS_ZLIB,
	BTRFS_COMPRESS_LZO,
	BTRFS_COMPRESS_ZSTD,
};

// BTRFS_CSUM_ITEM_KEY is not defined in include/uapi
#ifndef BTRFS_CSUM_ITEM_KEY

	#define BTRFS_ROOT_TREE_OBJECTID 1ULL
	#define BTRFS_EXTENT_TREE_OBJECTID 2ULL
	#define BTRFS_CHUNK_TREE_OBJECTID 3ULL
	#define BTRFS_DEV_TREE_OBJECTID 4ULL
	#define BTRFS_FS_TREE_OBJECTID 5ULL
	#define BTRFS_ROOT_TREE_DIR_OBJECTID 6ULL
	#define BTRFS_CSUM_TREE_OBJECTID 7ULL
	#define BTRFS_QUOTA_TREE_OBJECTID 8ULL
	#define BTRFS_UUID_TREE_OBJECTID 9ULL
	#define BTRFS_FREE_SPACE_TREE_OBJECTID 10ULL
	#define BTRFS_BALANCE_OBJECTID -4ULL
	#define BTRFS_ORPHAN_OBJECTID -5ULL
	#define BTRFS_TREE_LOG_OBJECTID -6ULL
	#define BTRFS_TREE_LOG_FIXUP_OBJECTID -7ULL
	#define BTRFS_TREE_RELOC_OBJECTID -8ULL
	#define BTRFS_DATA_RELOC_TREE_OBJECTID -9ULL
	#define BTRFS_EXTENT_CSUM_OBJECTID -10ULL
	#define BTRFS_FREE_SPACE_OBJECTID -11ULL
	#define BTRFS_FREE_INO_OBJECTID -12ULL
	#define BTRFS_MULTIPLE_OBJECTIDS -255ULL
	#define BTRFS_FIRST_FREE_OBJECTID 256ULL
	#define BTRFS_LAST_FREE_OBJECTID -256ULL
	#define BTRFS_FIRST_CHUNK_TREE_OBJECTID 256ULL
	#define BTRFS_DEV_ITEMS_OBJECTID 1ULL

	#define BTRFS_INODE_ITEM_KEY            1
	#define BTRFS_INODE_REF_KEY             12
	#define BTRFS_INODE_EXTREF_KEY          13
	#define BTRFS_XATTR_ITEM_KEY            24
	#define BTRFS_ORPHAN_ITEM_KEY           48
	#define BTRFS_DIR_LOG_ITEM_KEY  60
	#define BTRFS_DIR_LOG_INDEX_KEY 72
	#define BTRFS_DIR_ITEM_KEY      84
	#define BTRFS_DIR_INDEX_KEY     96
	#define BTRFS_EXTENT_DATA_KEY   108
	#define BTRFS_CSUM_ITEM_KEY     120
	#define BTRFS_EXTENT_CSUM_KEY   128
	#define BTRFS_ROOT_ITEM_KEY     132
	#define BTRFS_ROOT_BACKREF_KEY  144
	#define BTRFS_ROOT_REF_KEY      156
	#define BTRFS_EXTENT_ITEM_KEY   168
	#define BTRFS_METADATA_ITEM_KEY 169
	#define BTRFS_TREE_BLOCK_REF_KEY        176
	#define BTRFS_EXTENT_DATA_REF_KEY       178
	#define BTRFS_EXTENT_REF_V0_KEY         180
	#define BTRFS_SHARED_BLOCK_REF_KEY      182
	#define BTRFS_SHARED_DATA_REF_KEY       184
	#define BTRFS_BLOCK_GROUP_ITEM_KEY 192
	#define BTRFS_DEV_EXTENT_KEY    204
	#define BTRFS_DEV_ITEM_KEY      216
	#define BTRFS_CHUNK_ITEM_KEY    228
	#define BTRFS_BALANCE_ITEM_KEY  248
	#define BTRFS_QGROUP_STATUS_KEY         240
	#define BTRFS_QGROUP_INFO_KEY           242
	#define BTRFS_QGROUP_LIMIT_KEY          244
	#define BTRFS_QGROUP_RELATION_KEY       246
	#define BTRFS_DEV_STATS_KEY     249
	#define BTRFS_DEV_REPLACE_KEY   250
	#define BTRFS_UUID_KEY_SUBVOL   251
	#define BTRFS_UUID_KEY_RECEIVED_SUBVOL  252
	#define BTRFS_STRING_ITEM_KEY   253

#endif

#ifndef BTRFS_FREE_SPACE_INFO_KEY
	#define BTRFS_FREE_SPACE_INFO_KEY 198
	#define BTRFS_FREE_SPACE_EXTENT_KEY 199
	#define BTRFS_FREE_SPACE_BITMAP_KEY 200
	#define BTRFS_FREE_SPACE_OBJECTID -11ULL
#endif

#ifndef BTRFS_BLOCK_GROUP_RAID1C4
	#define BTRFS_BLOCK_GROUP_RAID1C3       (1ULL << 9)
	#define BTRFS_BLOCK_GROUP_RAID1C4       (1ULL << 10)
#endif

#ifndef BTRFS_DEFRAG_RANGE_START_IO

	// For some reason uapi has BTRFS_DEFRAG_RANGE_COMPRESS and
	// BTRFS_DEFRAG_RANGE_START_IO but not btrfs_ioctl_defrag_range_args
	// Never mind, it's too broken to be useful anyway
	struct btrfs_ioctl_defrag_range_args {
		/* start of the defrag operation */
		__u64 start;

		/* number of bytes to defrag, use (u64)-1 to say all */
		__u64 len;

		/*
		 * flags for the operation, which can include turning
		 * on compression for this one defrag
		 */
		__u64 flags;

		/*
		 * any extent bigger than this will be considered
		 * already defragged.  Use 0 to take the kernel default
		 * Use 1 to say every single extent must be rewritten
		 */
		__u32 extent_thresh;

		/*
		 * which compression method to use if turning on compression
		 * for this defrag operation.  If unspecified, zlib will
		 * be used
		 */
		__u32 compress_type;

		/* spare for later */
		__u32 unused[4];
	};

#endif

#ifndef BTRFS_IOC_CLONE_RANGE

	struct btrfs_ioctl_clone_range_args {
	  __s64 src_fd;
	  __u64 src_offset, src_length;
	  __u64 dest_offset;
	};

	// We definitely have this
	#define BTRFS_IOCTL_MAGIC 0x94

	#define BTRFS_IOC_CLONE        _IOW(BTRFS_IOCTL_MAGIC, 9, int)

	#define BTRFS_IOC_CLONE_RANGE _IOW(BTRFS_IOCTL_MAGIC, 13, \
					  struct btrfs_ioctl_clone_range_args)
#endif

#ifndef BTRFS_SAME_DATA_DIFFERS

	#define BTRFS_SAME_DATA_DIFFERS 1
	/* For extent-same ioctl */
	struct btrfs_ioctl_same_extent_info {
		__s64 fd;               /* in - destination file */
		__u64 logical_offset;   /* in - start of extent in destination */
		__u64 bytes_deduped;    /* out - total # of bytes we were able
					 * to dedupe from this file */
		/* status of this dedupe operation:
		 * 0 if dedupe succeeds
		 * < 0 for error
		 * == BTRFS_SAME_DATA_DIFFERS if data differs
		 */
		__s32 status;           /* out - see above description */
		__u32 reserved;
	};

	struct btrfs_ioctl_same_args {
		__u64 logical_offset;   /* in - start of extent in source */
		__u64 length;           /* in - length of extent */
		__u16 dest_count;       /* in - total elements in info array */
		__u16 reserved1;
		__u32 reserved2;
		struct btrfs_ioctl_same_extent_info info[0];
	};

	#define BTRFS_IOC_FILE_EXTENT_SAME _IOWR(BTRFS_IOCTL_MAGIC, 54, \
						 struct btrfs_ioctl_same_args)

#endif

#ifndef BTRFS_MAX_DEDUPE_LEN
	#define BTRFS_MAX_DEDUPE_LEN    (16 * 1024 * 1024)
#endif

#ifndef BTRFS_IOC_TREE_SEARCH_V2

	/*
	 * Extended version of TREE_SEARCH ioctl that can return more than 4k of bytes.
	 * The allocated size of the buffer is set in buf_size.
	 */
	struct btrfs_ioctl_search_args_v2 {
		struct btrfs_ioctl_search_key key; /* in/out - search parameters */
		__u64 buf_size;                    /* in - size of buffer
						    * out - on EOVERFLOW: needed size
						    *       to store item */
		__u64 buf[0];                      /* out - found items */
	};

	#define BTRFS_IOC_TREE_SEARCH_V2 _IOWR(BTRFS_IOCTL_MAGIC, 17, \
					   struct btrfs_ioctl_search_args_v2)
#endif

#ifndef BTRFS_IOC_LOGICAL_INO_V2
	#define BTRFS_IOC_LOGICAL_INO_V2 _IOWR(BTRFS_IOCTL_MAGIC, 59, struct btrfs_ioctl_logical_ino_args)
	#define BTRFS_LOGICAL_INO_ARGS_IGNORE_OFFSET (1ULL << 0)
#endif

#ifndef BTRFS_FS_INFO_FLAG_CSUM_INFO
	/* Request information about checksum type and size */
	#define BTRFS_FS_INFO_FLAG_CSUM_INFO                    (1 << 0)
#endif

#ifndef BTRFS_FS_INFO_FLAG_GENERATION
/* Request information about filesystem generation */
#define BTRFS_FS_INFO_FLAG_GENERATION                   (1 << 1)
#endif

#ifndef BTRFS_FS_INFO_FLAG_METADATA_UUID
/* Request information about filesystem metadata UUID */
#define BTRFS_FS_INFO_FLAG_METADATA_UUID                (1 << 2)
#endif

// BTRFS_CSUM_TYPE_CRC32 was a #define from 2008 to 2019.
// After that, it's an enum with the other 3 types.
// So if we do _not_ have CRC32 defined, it means we have the other 3;
// if we _do_ have CRC32 defined, it means we need the other 3.
// This seems likely to break some day.
#ifdef BTRFS_CSUM_TYPE_CRC32
	#define BTRFS_CSUM_TYPE_XXHASH 1
	#define BTRFS_CSUM_TYPE_SHA256 2
	#define BTRFS_CSUM_TYPE_BLAKE2 3
#endif

struct btrfs_ioctl_fs_info_args_v3 {
	__u64 max_id;                           /* out */
	__u64 num_devices;                      /* out */
	__u8 fsid[BTRFS_FSID_SIZE];             /* out */
	__u32 nodesize;                         /* out */
	__u32 sectorsize;                       /* out */
	__u32 clone_alignment;                  /* out */
	/* See BTRFS_FS_INFO_FLAG_* */
	__u16 csum_type;                        /* out */
	__u16 csum_size;                        /* out */
	__u64 flags;                            /* in/out */
	__u64 generation;                       /* out */
	__u8 metadata_uuid[BTRFS_FSID_SIZE];    /* out */
	__u8 reserved[944];                     /* pad to 1k */
};

#endif // CRUCIBLE_BTRFS_H
