Good Btrfs Feature Interactions
-------------------------------

bees has been tested in combination with the following:

* btrfs compression (zlib, lzo, zstd), mixtures of compressed and uncompressed extents
* PREALLOC extents (unconditionally replaced with holes)
* HOLE extents and btrfs no-holes feature
* Other deduplicators, reflink copies (though bees may decide to redo their work)
* btrfs snapshots and non-snapshot subvols (RW and RO)
* Concurrent file modification (e.g. PostgreSQL and sqlite databases, build daemons)
* all btrfs RAID profiles
* IO errors during dedupe (read errors will throw exceptions, bees will catch them and skip over the affected extent)
* Filesystems mounted *with* the flushoncommit option (system crashes, power failures OK)
* 4K filesystem data block size / clone alignment
* 64-bit and 32-bit host CPUs (amd64, x86, arm)
* Huge files (>1TB--although Btrfs performance on such files isn't great in general)
* filesystems up to 30T+ bytes, 100M+ files
* btrfs receive
* btrfs nodatacow/nodatasum inode attribute or mount option (bees skips all nodatasum files)
* open(O_DIRECT) (seems to work as well--or as poorly--with bees as with any other btrfs feature)

Bad Btrfs Feature Interactions
------------------------------

bees has been tested in combination with the following, and various problems are known:

* bcache, lvmcache:  **severe (filesystem-destroying) metadata corruption
  issues** observed in testing and reported by users, apparently only when
  used with bees.  Plain SSD and HDD seem to be OK.
* btrfs send:  some kernel versions have bugs in btrfs send that can be
  triggered by bees.  The send can be restarted and will work if bees
  has finished processing the snapshot being sent.  No data corruption
  observed other than the truncated send.
* btrfs qgroups:  very slow, sometimes hangs...and it's even worse when
  bees is running.
* btrfs autodefrag mount option:  hangs and high CPU usage problems
  reported by users.  bees cannot distinguish autodefrag activity from
  normal filesystem activity and will likely try to undo the autodefrag
  if duplicate copies of the defragmented data exist.

Untested Btrfs Feature Interactions
-----------------------------------

bees has not been tested with the following, and undesirable interactions may occur:

* Non-4K filesystem data block size (should work if recompiled)
* Non-equal hash (SUM) and filesystem data block (CLONE) sizes (need to fix that eventually)
* btrfs seed filesystems (does anyone even use those?)
* btrfs out-of-tree kernel patches (e.g. in-kernel dedupe or encryption)
* btrfs-convert from ext2/3/4 (never tested, might run out of space or ignore significant portions of the filesystem due to sanity checks)
* btrfs mixed block groups (don't know a reason why it would *not* work, but never tested)
* Filesystems mounted *without* the flushoncommit option (don't know the data integrity impact of crashes during dedupe writes vs. ordinary writes)
