Good Btrfs Feature Interactions
-------------------------------

bees has been tested in combination with the following:

* btrfs compression (zlib, lzo, zstd)
* PREALLOC extents (unconditionally replaced with holes)
* HOLE extents and btrfs no-holes feature
* Other deduplicators (`duperemove`, `jdupes`)
* Reflink copies (modern coreutils `cp` and `mv`)
* Concurrent file modification (e.g. PostgreSQL and sqlite databases, VMs, build daemons)
* All btrfs RAID profiles:  single, dup, raid0, raid1, raid10, raid1c3, raid1c4, raid5, raid6
* IO errors during dedupe (affected extents are skipped)
* 4K filesystem data block size / clone alignment
* 64-bit and 32-bit LE host CPUs (amd64, x86, arm)
* Large files (kernel 5.4 or later strongly recommended)
* Filesystem data sizes up to 100T+ bytes, 1000M+ files
* `open(O_DIRECT)` (seems to work as well--or as poorly--with bees as with any other btrfs feature)
* btrfs-convert from ext2/3/4
* btrfs `autodefrag` mount option
* btrfs balance (data balances cause rescan of relocated data)
* btrfs block-group-tree
* btrfs `flushoncommit` and `noflushoncommit` mount options
* btrfs mixed block groups
* btrfs `nodatacow`/`nodatasum` inode attribute or mount option (bees skips all nodatasum files)
* btrfs qgroups and quota support (_not_ squotas)
* btrfs receive
* btrfs scrub
* btrfs send (dedupe pauses automatically, kernel 5.4 or later required)
* btrfs snapshot, non-snapshot subvols (RW and RO), snapshot delete

**Note:** some btrfs features have minimum kernel versions which are
higher than the minimum kernel version for bees.

Untested Btrfs Feature Interactions
-----------------------------------

bees has not been tested with the following, and undesirable interactions may occur:

* Non-4K filesystem data block size (should work if recompiled)
* Non-equal hash (SUM) and filesystem data block (CLONE) sizes (need to fix that eventually)
* btrfs seed filesystems, raid-stripe-tree, squotas (no particular reason these wouldn't work, but no one has reported trying)
* btrfs out-of-tree kernel patches (e.g. encryption, extent tree v2)
* Host CPUs with exotic page sizes, alignment requirements, or endianness (ppc, alpha, sparc, strongarm, s390, mips, m68k...)
