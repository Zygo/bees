Good Btrfs Feature Interactions
-------------------------------

bees has been tested in combination with the following:

* btrfs compression (zlib, lzo, zstd), mixtures of compressed and uncompressed extents
* PREALLOC extents (unconditionally replaced with holes)
* HOLE extents and btrfs no-holes feature
* Other deduplicators, reflink copies (though bees may decide to redo their work)
* btrfs snapshots and non-snapshot subvols (RW and RO)
* Concurrent file modification (e.g. PostgreSQL and sqlite databases, VMs, build daemons)
* All btrfs RAID profiles
* IO errors during dedupe (read errors will throw exceptions, bees will catch them and skip over the affected extent)
* Filesystems mounted with or without the `flushoncommit` option
* 4K filesystem data block size / clone alignment
* 64-bit and 32-bit LE host CPUs (amd64, x86, arm)
* Large files (kernel 5.4 or later strongly recommended)
* Filesystems up to 90T+ bytes, 1000M+ files
* btrfs receive
* btrfs nodatacow/nodatasum inode attribute or mount option (bees skips all nodatasum files)
* open(O_DIRECT) (seems to work as well--or as poorly--with bees as with any other btrfs feature)
* lvm dm-cache, writecache

Bad Btrfs Feature Interactions
------------------------------

bees has been tested in combination with the following, and various problems are known:

* btrfs send:  there are bugs in `btrfs send` that can be triggered by
  bees on old kernels.  The [`--workaround-btrfs-send` option](options.md)
  works around this issue by preventing bees from modifying read-only
  snapshots.

* btrfs qgroups:  very slow, sometimes hangs...and it's even worse when
  bees is running.

* btrfs autodefrag mount option:  bees cannot distinguish autodefrag
  activity from normal filesystem activity, and may try to undo the
  autodefrag if duplicate copies of the defragmented data exist.

Untested Btrfs Feature Interactions
-----------------------------------

bees has not been tested with the following, and undesirable interactions may occur:

* Non-4K filesystem data block size (should work if recompiled)
* Non-equal hash (SUM) and filesystem data block (CLONE) sizes (need to fix that eventually)
* btrfs seed filesystems (no particular reason it wouldn't work, but no one has reported trying)
* btrfs out-of-tree kernel patches (e.g. in-kernel dedupe, encryption, extent tree v2)
* btrfs-convert from ext2/3/4 (never tested, might run out of space or ignore significant portions of the filesystem due to sanity checks)
* btrfs mixed block groups (don't know a reason why it would *not* work, but never tested)
* Host CPUs with exotic page sizes, alignment requirements, or endianness (ppc, alpha, sparc, strongarm, s390, mips, m68k...)
* bcache: used to be in the "bad" list, now in the "untested" list because nobody is rigorously testing, and bcache bugs come and go
* flashcache: an out-of-tree cache-HDD-on-SSD block layer helper
