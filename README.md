BEES
====

Best-Effort Extent-Same, a btrfs dedup agent.

About Bees
----------

Bees is a block-oriented userspace dedup agent designed to avoid
scalability problems on large filesystems.

Bees is designed to degrade gracefully when underprovisioned with RAM.
Bees does not use more RAM or storage as filesystem data size increases.
The dedup hash table size is fixed at creation time and does not change.
The effective dedup block size is dynamic and adjusts automatically to
fit the hash table into the configured RAM limit.  Hash table overflow
is not implemented to eliminate the IO overhead of hash table overflow.
Hash table entries are only 16 bytes per dedup block to keep the average
dedup block size small.

Bees does not require alignment between dedup blocks or extent boundaries
(i.e. it can handle any multiple-of-4K offset between dup block pairs).
Bees rearranges blocks into shared and unique extents if required to
work within current btrfs kernel dedup limitations.

Bees can dedup any combination of compressed and uncompressed extents.

Bees operates in a single pass which removes duplicate extents immediately
during scan.  There are no separate scanning and dedup phases.

Bees uses only data-safe btrfs kernel operations, so it can dedup live
data (e.g. build servers, sqlite databases, VM disk images).  It does
not modify file attributes or timestamps.

Bees does not store any information about filesystem structure, so it is
not affected by the number or size of files (except to the extent that
these cause performance problems for btrfs in general).  It retrieves such
information on demand through btrfs SEARCH_V2 and LOGICAL_INO ioctls.
This eliminates the storage required to maintain the equivalents of
these functions in userspace.  It's also why bees has no XFS support.

Bees is a daemon designed to run continuously and maintain its state
across crahes and reboots.  Bees uses checkpoints for persistence to
eliminate the IO overhead of a transactional data store.  On restart,
bees will dedup any data that was added to the filesystem since the
last checkpoint.

Bees is used to dedup filesystems ranging in size from 16GB to 35TB, with
hash tables ranging in size from 128MB to 11GB.

How Bees Works
--------------

Bees uses a fixed-size persistent dedup hash table with a variable dedup
block size.  Any size of hash table can be dedicated to dedup.  Bees will
scale the dedup block size to fit the filesystem's unique data size
using a weighted sampling algorithm.  This allows Bees to adapt itself
to its filesystem size without forcing admins to do math at install time.
At the same time, the duplicate block alignment constraint can be as low
as 4K, allowing efficient deduplication of files with narrowly-aligned
duplicate block offsets (e.g. compiled binaries and VM/disk images)
even if the effective block size is much larger.

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

To change the size of the hash table, use 'truncate' to change the hash
table size, delete `beescrawl.dat` so that bees will start over with a
fresh full-filesystem rescan, and restart `bees'.

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

* Block reads are currently more allocation- and CPU-intensive than they
should be, especially for filesystems on SSD where the IO overhead is
much smaller.  This is a problem for power-constrained environments
(e.g. laptops with slow CPU).

* Bees can currently fragment extents when required to remove duplicate
blocks, but has no defragmentation capability yet.  When possible, Bees
will attempt to work with existing extent boundaries, but it will not
aggregate blocks together from multiple extents to create larger ones.

* It is possible to resize the hash table without starting over with
a new full-filesystem scan; however, this has not been implemented yet.

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
* 64-bit and 32-bit host CPUs (amd64, x86, arm)
* Large (>16M) extents
* Huge files (>1TB--although Btrfs performance on such files isn't great in general)
* filesystems up to 25T bytes, 100M+ files

Bad Btrfs Feature Interactions
------------------------------

Bees has not been tested with the following, and undesirable interactions may occur:

* Non-4K filesystem data block size (should work if recompiled)
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

Missing features (usually not available in older LTS kernels):

* 3.13: `FILE_EXTENT_SAME` ioctl added.  No way to reliably dedup with
  concurrent modifications before this.
* 3.16: `SEARCH_V2` ioctl added.  Bees could use `SEARCH` instead.
* 4.2: `FILE_EXTENT_SAME` no longer updates mtime, can be used at EOF.

Bug fixes (sometimes included in older LTS kernels):

* 4.5: hang in the `INO_PATHS` ioctl used by Bees.
* 4.5: use-after-free in the `FILE_EXTENT_SAME` ioctl used by Bees.
* 4.7: *slow backref* bug no longer triggers a softlockup panic.  It still
  too long to resolve a block address to a root/inode/offset triple.
* 4.10-rc1: reduced CPU time cost of the LOGICAL_INO ioctl and dedup
  backref processing in general.

Unfixed kernel bugs (as of 4.5.7) with workarounds in Bees:

* *slow backrefs* (aka toxic extents): If the number of references to a
  single shared extent within a single file grows above a few thousand,
  the kernel consumes CPU for minutes at a time while holding various
  locks that block access to the filesystem.  Bees avoids this bug by
  measuring the time the kernel spends performing certain operations
  and permanently blacklisting any extent or hash where the kernel
  starts to get slow.  Inside Bees, such blocks are marked as 'toxic'
  hash/block addresses.

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
  `beescrawl.dat.tmp` to exist without a corresponding inode.
  This directory entry cannot be renamed or removed; however, it does
  not prevent the creation of a second directory entry with the same
  name that functions normally, so it doesn't prevent Bees operation.

  The orphan directory entry can be removed by deleting its subvol,
  so place BEESHOME on a separate subvol so you can delete these orphan
  directory entries when they occur (or use btrfs zero-log before mounting
  the filesystem after a crash).  Alternatively, place BEESHOME on a
  non-btrfs filesystem.

* If the `fsync()` in `BeesTempFile::make_copy` is removed, the filesystem
  hangs within a few hours, requiring a reboot to recover.  On the other
  hand, there may be net performance benefits to calling `fsync()` before
  or after each dedup.  This needs further investigation.

Not really a bug, but a gotcha nonetheless:

* If a process holds a directory FD open, the subvol containing the
  directory cannot be deleted (`btrfs sub del` will start the deletion
  process, but it will not proceed past the first open directory FD).
  `btrfs-cleaner` will simply skip over the directory *and all of its
  children* until the FD is closed.  Bees avoids this gotcha by closing
  all of the FDs in its directory FD cache every 15 minutes.

Build
-----

Build with `make`. The build produces `bin/bees` and `lib/libcrucible.so`, which must be copied to somewhere in `$PATH` and `$LD_LIBRARY_PATH` on the target system respectively.

### Ubuntu 16.04 - 17.04:
`$ apt -y install build-essential btrfs-tools uuid-dev markdown && make`

### Ubuntu 14.04:
You can try to carry on the work done here: https://gist.github.com/dagelf/99ee07f5638b346adb8c058ab3d57492 

Dependencies
------------

* C++11 compiler (tested with GCC 4.9 and 6.2.0)

  Sorry.  I really like closures and shared_ptr, so support
  for earlier compiler versions is unlikely.

* btrfs-progs (tested with 4.1..4.7)

  Needed for btrfs.h and ctree.h during compile.
  Not needed at runtime.

* libuuid-dev

  This library is only required for a feature that was removed after v0.1.
  The lingering support code can be removed.

* Linux kernel 4.4.3 or later

  Don't bother trying to make Bees work with older kernels.
  It won't end well.
  
* markdown


Setup
-----

Create a directory for bees state files:

        export BEESHOME=/some/path
        mkdir -p "$BEESHOME"

Create an empty hash table (your choice of size, but it must be a multiple
of 16M).  This example creates a 1GB hash table:

        truncate -s 1g "$BEESHOME/beeshash.dat"
        chmod 700 "$BEESHOME/beeshash.dat"

bees can only process the root subvol of a btrfs (seriously--if the
argument is not the root subvol directory, Bees will just throw an
exception and stop).

Use a bind mount, and let only bees access it:

	UUID=3399e413-695a-4b0b-9384-1b0ef8f6c4cd
	mkdir -p /var/lib/bees/$UUID
	mount /dev/disk/by-uuid/$UUID /var/lib/bees/$UUID -osubvol=/

If you don't set BEESHOME, the path ".beeshome" will be used relative
to the root subvol of the filesystem.  For example:

	btrfs sub create /var/lib/bees/$UUID/.beeshome
	truncate -s 1g /var/lib/bees/$UUID/.beeshome/beeshash.dat
	chmod 700 /var/lib/bees/$UUID/.beeshome/beeshash.dat

You can use any relative path in BEESHOME.  The path will be taken
relative to the root of the deduped filesystem (in other words it can
be the name of a subvol):

	export BEESHOME=@my-beeshome
	btrfs sub create /var/lib/bees/$UUID/$BEESHOME
	truncate -s 1g /var/lib/bees/$UUID/$BEESHOME/beeshash.dat
	chmod 700 /var/lib/bees/$UUID/$BEESHOME/beeshash.dat

Configuration
-------------

The only runtime configurable options are environment variables:

* BEESHOME: Directory containing Bees state files:
 * beeshash.dat  | persistent hash table.  Must be a multiple of 16M.
                   This contains 16-byte records:  8 bytes for CRC64,
                   8 bytes for physical address and some metadata bits.
 * beescrawl.dat | state of SEARCH_V2 crawlers.  ASCII text.
 * beesstats.txt | statistics and performance counters.  ASCII text.
* BEESSTATUS: File containing a snapshot of current Bees state:  performance
  counters and current status of each thread.  The file is meant to be
  human readable, but understanding it probably requires reading the source.
  You can watch bees run in realtime with a command like:

	watch -n1 cat $BEESSTATUS

Other options (e.g. interval between filesystem crawls) can be configured
in src/bees.h.

Running
-------

Reduce CPU and IO priority to be kinder to other applications sharing
this host (or raise them for more aggressive disk space recovery).  If you
use cgroups, put `bees` in its own cgroup, then reduce the `blkio.weight`
and `cpu.shares` parameters.  You can also use `schedtool` and `ionice`
in the shell script that launches `bees`:

        schedtool -D -n20 $$
        ionice -c3 -p $$

Let the bees fly:

	for fs in /var/lib/bees/*-*-*-*-*/; do
		bees "$fs" >> "$fs/.beeshome/bees.log" 2>&1 &
	done

You'll probably want to arrange for /var/log/bees.log to be rotated
periodically.  You may also want to set umask to 077 to prevent disclosure
of information about the contents of the filesystem through the log file.

There are also some shell wrappers in the `scripts/` directory.


Bug Reports and Contributions
-----------------------------

Email bug reports and patches to Zygo Blaxell <bees@furryterror.org>.

You can also use Github:

        https://github.com/Zygo/bees



Copyright & License
===================

Copyright 2015-2017 Zygo Blaxell <bees@furryterror.org>.

GPL (version 3 or later).
