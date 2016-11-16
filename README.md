BEES
====

Best-Effort Extent-Same, a btrfs deduplication daemon.

About Bees
----------

Bees is a daemon designed to run continuously on live file servers.
Bees consumes entire filesystems and deduplicates in a single pass, using
minimal RAM to store data.  Bees maintains persistent state so it can be
interrupted and resumed, whether by planned upgrades or unplanned crashes.
Bees makes continuous incremental progress instead of using separate
scan and dedup phases.  Bees uses the Linux kernel's `dedupe_file_range`
system call to ensure data is handled safely even if other applications
concurrently modify it.

Bees is intentionally btrfs-specific for performance and capability.
Bees uses the btrfs `SEARCH_V2` ioctl to scan for new data 
without the overhead of repeatedly walking filesystem trees with the
POSIX API.  Bees uses `LOGICAL_INO` and `INO_PATHS` to leverage btrfs's
existing metadata instead of building its own redundant data structures.
Bees can cope with Btrfs filesystem compression.  Bees can reassemble
Btrfs extents to deduplicate extents that contain a mix of duplicate
and unique data blocks.

Bees includes a number of workarounds for Btrfs kernel bugs to (try to)
avoid ruining your day.  You're welcome.

How Bees Works
--------------

Bees uses a fixed-size persistent dedup hash table with a variable dedup
block size.  Any size of hash table can be dedicated to dedup.  Bees will
scale the dedup block size to fit the filesystem's unique data size
using a weighted sampling algorithm.  This allows Bees to adapt itself
to its filesystem size without forcing admins to do math at install time.
At the same time, the duplicate block alignment constraint can be as low
as 4K, allowing efficient deduplication of files with narrowly-aligned
duplicate block offsets (e.g. compiled binaries and VM/disk images).

The Bees hash table is loaded into RAM at startup (using hugepages if
available), mlocked, and synced to persistent storage by trickle-writing
over a period of several hours.  This avoids issues related to seeking
or fragmentation, and enables the hash table to be efficiently stored
on Btrfs with compression (or an ext4 filesystem, or a raw disk, or
on CIFS...).

Once a duplicate block is identified, Bees examines the nearby blocks
in the files where block appears.  This allows Bees to find long runs
of adjacent duplicate block pairs if it has an entry for any one of
the blocks in its hash table.  The stored hash entry plus the block
recently scanned from disk form a duplicate pair.  On typical data sets,
this means most of the blocks in the hash table are redundant and can
be discarded without significant performance impact.

Hash table entries are grouped together into LRU lists.  As each block
is scanned, its hash table entry is inserted into the LRU list at a
random position.  If the LRU list is full, the entry at the end of the
list is deleted.  If a hash table entry is used to discover duplicate
blocks, the entry is moved to the beginning of the list.  This makes Bees
unable to detect a small number of duplicates (less than 1% on typical
filesystems), but it dramatically improves efficiency on filesystems
with many small files.  Bees has found a net 13% more duplicate bytes
than a naive fixed-block-size algorithm with a 64K block size using the
same size of hash table, even after discarding 1% of the duplicate bytes.

Hash Table Sizing
-----------------

Hash table entries are 16 bytes each (64-bit hash, 52-bit block number,
and some metadata bits).  Each entry represents a minimum of 4K on disk.

    unique data size    hash table size    average dedup block size
        1TB                 4GB                  4K
        1TB                 1GB                 16K
        1TB               256MB                 64K
        1TB                16MB               1024K
       64TB                 1GB               1024K

Things You Might Expect That Bees Doesn't Have
----------------------------------------------

* There's no configuration file or getopt command line option processing
(patches welcome!).  There are some tunables hardcoded in the source
that could eventually become configuration options.

* There's no way to *stop* the Bees daemon.  Use SIGKILL, SIGTERM, or
Ctrl-C for now.  Some of the destructors are unreachable and have never
been tested.  Bees will repeat some work when restarted.

* The Bees process doesn't fork and writes its log to stdout/stderr.
A shell wrapper is required to make it behave more like a daemon.

* There's no facility to exclude any part of a filesystem (patches
welcome).

* PREALLOC extents and extents containing blocks filled with zeros will
be replaced by holes unconditionally.

* Duplicate block groups that are less than 12K in length can take 30%
of the run time while saving only 3% of the disk space.  There should
be an option to just not bother with those.

* There is a lot of duplicate reading of blocks in snapshots.  Bees will
scan all snapshots at close to the same time to try to get better
performance by caching, but really fixing this requires rewriting the
crawler to scan the btrfs extent tree directly instead of the subvol
FS trees.

* Bees had support for multiple worker threads in the past; however,
this was removed because it made Bees too aggressive to coexist with
other applications on the same machine.  It also hit the *slow backrefs*
on N CPU cores instead of just one.

Good Btrfs Feature Interactions
-------------------------------

Bees has been tested in combination with the following:

* btrfs compression (either method), mixtures of compressed and uncompressed extents
* PREALLOC extents (unconditionally replaced with holes)
* HOLE extents and btrfs no-holes feature
* Other deduplicators, reflink copies (though Bees may decide to redo their work)
* btrfs snapshots and non-snapshot subvols (RW only)
* Concurrent file modification (e.g. PostgreSQL and sqlite databases, build daemons)
* all btrfs RAID profiles (people ask about this, but it's irrelevant)
* IO errors during dedup (read errors will throw exceptions, Bees will catch them and skip over the affected extent)
* Filesystems mounted *with* the flushoncommit option
* 4K filesystem data block size / clone alignment
* 64-bit CPUs (amd64)
* Large (>16M) extents
* Huge files (>1TB--although Btrfs performance on such files isn't great in general)
* filesystems up to 25T bytes, 100M+ files


Bad Btrfs Feature Interactions
------------------------------

Bees has not been tested with the following, and undesirable interactions may occur:

* Non-4K filesystem data block size (should work if recompiled)
* 32-bit CPUs (x86, arm)
* Non-equal hash (SUM) and filesystem data block (CLONE) sizes (probably never will work)
* btrfs read-only snapshots (never tested, probably wouldn't work well)
* btrfs send/receive (receive is probably OK, but send requires RO snapshots.  See above)
* btrfs qgroups (never tested, no idea what might happen)
* btrfs seed filesystems (does anyone even use those?)
* btrfs autodefrag mount option (never tested, could fight with Bees)
* btrfs nodatacow mount option or inode attribute (*could* work, but might not)
* btrfs out-of-tree kernel patches (e.g. in-band dedup or encryption)
* btrfs-convert from ext2/3/4 (never tested)
* btrfs mixed block groups (don't know a reason why it would *not* work, but never tested)
* open(O_DIRECT)
* Filesystems mounted *without* the flushoncommit option

Other Caveats
-------------

* btrfs balance will invalidate parts of the dedup table.  Bees will
  happily rebuild the table, but it will have to scan all the blocks
  again.

* btrfs defrag will cause Bees to rescan the defragmented file.  If it
  contained duplicate blocks and other references to the original
  fragmented duplicates still exist, Bees will replace the defragmented
  extents with the original fragmented ones.

* Bees creates temporary files (with O_TMPFILE) and uses them to split
  and combine extents elsewhere in btrfs.  These will take up to 2GB
  during normal operation.

* Like all deduplicators, Bees will replace data blocks with metadata
  references.  It is a good idea to ensure there are several GB of
  unallocated space (see `btrfs fi df`) on the filesystem before running
  Bees for the first time.  Use

	btrfs balance start -dusage=100,limit=1 /your/filesystem

  If possible, raise the `limit` parameter to the current size of metadata
  usage (from `btrfs fi df`) plus 1.


A Brief List Of Btrfs Kernel Bugs
---------------------------------

Fixed bugs:

* 3.13: `FILE_EXTENT_SAME` ioctl added.  No way to reliably dedup with
  concurrent modifications before this.
* 3.16: `SEARCH_V2` ioctl added.  Bees could use `SEARCH` instead.
* 4.2: `FILE_EXTENT_SAME` no longer updates mtime, can be used at EOF.
  Kernel deadlock bugs fixed.
* 4.7: *slow backref* bug no longer triggers a softlockup panic.  It still
  too long to resolve a block address to a root/inode/offset triple.

Unfixed kernel bugs (as of 4.5.7) with workarounds in Bees:

* *slow backref*: If the number of references to a single shared extent
  within a single file grows above a few thousand, the kernel consumes CPU
  for up to 40 uninterruptible minutes while holding various locks that
  block access to the filesystem.  Bees avoids this bug by measuring the
  time the kernel spends performing certain operations and permanently
  blacklisting any extent or hash where the kernel starts to get slow.
  Inside Bees, such blocks are marked as 'toxic' hash/block addresses.

* `LOGICAL_INO` output is arbitrarily limited to 2730 references
  even if more buffer space is provided for results.  Once this number
  has been reached, Bees can no longer replace the extent since it can't
  find and remove all existing references.  Bees refrains from adding
  any more references after the first 2560.  Offending blocks are
  marked 'toxic' even if there is no corresponding performance problem.
  This places an obvious limit on dedup efficiency for extremely common
  blocks or filesystems with many snapshots (although this limit is
  far greater than the effective limit imposed by the *slow backref* bug).

* `FILE_EXTENT_SAME` is arbitrarily limited to 16MB.  This is less than
  128MB which is the maximum extent size that can be created by defrag
  or prealloc.  Bees avoids feedback loops this can generate while
  attempting to replace extents over 16MB in length.

* `DEFRAG_RANGE` is useless.  The ioctl attempts to implement `btrfs
  fi defrag` in the kernel, and will arbitrarily defragment more or
  less than the range requested to match the behavior expected from the
  userspace tool.  Bees implements its own defrag instead, copying data
  to a temporary file and using the `FILE_EXTENT_SAME` ioctl to replace
  precisely the specified range of offending fragmented blocks.

* When writing BeesStringFile, a crash can cause the directory entry
  `beescrawl.UUID.dat.tmp` to exist without a corresponding inode.
  This directory entry cannot be renamed or removed; however, it does
  not prevent the creation of a second directory entry with the same
  name that functions normally, so it doesn't prevent Bees operation.

  The orphan directory entry can be removed by deleting its subvol,
  so place BEESHOME on a separate subvol so you can delete these orphan
  directory entries when they occur (or use btrfs zero-log before mounting
  the filesystem after a crash).

* If the fsync() BeesTempFile::make_copy is removed, the filesystem
  hangs within a few hours, requiring a reboot to recover.

Not really a bug, but a gotcha nonetheless:

* If a process holds a directory FD open, the subvol containing the
  directory cannot be deleted (`btrfs sub del` will start the deletion
  process, but it will not proceed past the first open directory FD).
  `btrfs-cleaner` will simply skip over the directory *and all of its
  children* until the FD is closed.  Bees avoids this gotcha by closing
  all of the FDs in its directory FD cache every 15 minutes.



Requirements
------------

* C++11 compiler (tested with GCC 4.9)

  Sorry.  I really like closures.

* btrfs-progs (tested with 4.1..4.7)

  Needed for btrfs.h and ctree.h during compile.
  Not needed at runtime.

* libuuid-dev

  TODO: remove the one function used from this library.
  It supports a feature Bees no longer implements.

* Linux kernel 4.2 or later

  Don't bother trying to make Bees work with older kernels.
  It won't end well.

* 64-bit host and target CPU

  This code has never been tested on a 32-bit target CPU.

  A 64-bit host CPU may be required for the self-tests.
  Some of the ioctls don't work properly with a 64-bit
  kernel and 32-bit userspace.

Build
-----

Build with `make`.

The build produces `bin/bees` and `lib/libcrucible.so`, which must be
copied to somewhere in `$PATH` and `$LD_LIBRARY_PATH` on the target
system respectively.

Setup
-----

Create a directory for bees state files:

	export BEESHOME=/some/path
	mkdir -p "$BEESHOME"

Create an empty hash table (your choice of size, but it must be a multiple
of 16M).  This example creates a 1GB hash table:

	truncate -s 1g "$BEESHOME/beeshash.dat"
	chmod 700 "$BEESHOME/beeshash.dat"

Configuration
-------------

The only runtime configurable options are environment variables:

* BEESHOME: Directory containing Bees state files:
 * beeshash.dat         | persistent hash table (must be a multiple of 16M)
 * beescrawl.`UUID`.dat | state of SEARCH_V2 crawlers
 * beesstats.txt        | statistics and performance counters
* BEESSTATS: File containing a snapshot of current Bees state (performance
  counters and current status of each thread).

Other options (e.g. interval between filesystem crawls) can be configured
in src/bees.h.

Running
-------

We created this directory in the previous section:

	export BEESHOME=/some/path

Use a tmpfs for BEESSTATUS, it updates once per second:

	export BEESSTATUS=/run/bees.status

bees can only process the root subvol of a btrfs (seriously--if the
argument is not the root subvol directory, Bees will just throw an
exception and stop).

Use a bind mount, and let only bees access it:

	mount -osubvol=/ /dev/<your-filesystem> /var/lib/bees/root

Reduce CPU and IO priority to be kinder to other applications
sharing this host (or raise them for more aggressive disk space
recovery).  If you use cgroups, put bees in its own cgroup, then reduce
the `blkio.weight` and `cpu.shares` parameters.  You can also use
`schedtool` and `ionice in the shell script that launches bees:

	schedtool -D -n20 $$
	ionice -c3 -p $$

Let the bees fly:

	bees /var/lib/bees/root >> /var/log/bees.log 2>&1

You'll probably want to arrange for /var/log/bees.log to be rotated
periodically.  You may also want to set umask to 077 to prevent disclosure
of information about the contents of the filesystem through the log file.


Bug Reports and Contributions
-----------------------------

Email bug reports and patches to Zygo Blaxell <bees@furryterror.org>.

You can also use Github:

	https://github.com/Zygo/bees



Copyright & License
===================

Copyright 2015-2016 Zygo Blaxell <bees@furryterror.org>.

GPL (version 3 or later).
