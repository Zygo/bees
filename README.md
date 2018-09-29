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
fresh full-filesystem rescan, and restart `bees`.

Things You Might Expect That Bees Doesn't Have
----------------------------------------------

* There's no configuration file (patches welcome!).  There are some tunables
hardcoded in the source that could eventually become configuration options.
There's also an incomplete option parser (patches welcome!).

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

* btrfs compression (zlib, lzo, zstd), mixtures of compressed and uncompressed extents
* PREALLOC extents (unconditionally replaced with holes)
* HOLE extents and btrfs no-holes feature
* Other deduplicators, reflink copies (though Bees may decide to redo their work)
* btrfs snapshots and non-snapshot subvols (RW and RO)
* Concurrent file modification (e.g. PostgreSQL and sqlite databases, build daemons)
* all btrfs RAID profiles (people ask about this, but it's irrelevant to bees)
* IO errors during dedup (read errors will throw exceptions, Bees will catch them and skip over the affected extent)
* Filesystems mounted *with* the flushoncommit option
* 4K filesystem data block size / clone alignment
* 64-bit and 32-bit host CPUs (amd64, x86, arm)
* Large (>16M) extents
* Huge files (>1TB--although Btrfs performance on such files isn't great in general)
* filesystems up to 25T bytes, 100M+ files
* btrfs receive
* btrfs nodatacow/nodatasum inode attribute or mount option (bees skips all nodatasum files)
* open(O_DIRECT) (seems to work as well--or as poorly--with bees as with any other btrfs feature)

Bad Btrfs Feature Interactions
------------------------------

Bees has been tested in combination with the following, and various problems are known:

* bcache, lvmcache:  *severe (filesystem-destroying) metadata corruption
  issues* observed in testing and reported by users, apparently only when
  used with bees.  Plain SSD and HDD seem to be OK.
* btrfs send:  sometimes aborts with an I/O error when bees changes the
  data layout during a send.  The send can be restarted and will work
  if bees has finished processing the snapshot being sent.  No data
  corruption observed other than the truncated send.
* btrfs qgroups:  very slow, sometimes hangs
* btrfs autodefrag mount option:  hangs and high CPU usage problems
  reported by users.  bees cannot distinguish autodefrag activity from
  normal filesystem activity and will likely try to undo the autodefrag,
  so it should probably be turned off for bees in any case.

Untested Btrfs Feature Interactions
-----------------------------------

Bees has not been tested with the following, and undesirable interactions may occur:

* Non-4K filesystem data block size (should work if recompiled)
* Non-equal hash (SUM) and filesystem data block (CLONE) sizes (probably never will work)
* btrfs seed filesystems (does anyone even use those?)
* btrfs out-of-tree kernel patches (e.g. in-band dedup or encryption)
* btrfs-convert from ext2/3/4 (never tested, might run out of space or ignore significant portions of the filesystem due to sanity checks)
* btrfs mixed block groups (don't know a reason why it would *not* work, but never tested)
* Filesystems mounted *without* the flushoncommit option (don't know the impact of crashes during dedup writes vs. ordinary writes)

Other Caveats
-------------

* btrfs balance will invalidate parts of the dedup hash table.  Bees will
  happily rebuild the table, but it will have to scan all the blocks
  again.

* btrfs defrag will cause Bees to rescan the defragmented file.  If it
  contained duplicate blocks and other references to the original
  fragmented duplicates still exist, Bees will replace the defragmented
  extents with the original fragmented ones.

* Bees creates temporary files (with O_TMPFILE) and uses them to split
  and combine extents elsewhere in btrfs.  These will take up to 2GB
  of disk space per thread during normal operation.

* Like all deduplicators, Bees will replace data blocks with metadata
  references.  It is a good idea to ensure there is sufficient unallocated
  space (see `btrfs fi usage`) on the filesystem to allow the metadata
  to multiply in size by the number of snapshots before running Bees
  for the first time.  Use

        btrfs balance start -dusage=100,limit=N /your/filesystem

  where the `limit` parameter 'N' should be calculated as follows:

	* start with the current size of metadata usage (from `btrfs fi
	  df`) in GB, plus 1

	* multiply by the proportion of disk space in subvols with
	  snapshots (i.e. if there are no snapshots, multiply by 0;
	  if all of the data is shared between at least one origin
	  and one snapshot subvol, multiply by 1)

	* multiply by the number of snapshots (i.e. if there is only
	  one subvol, multiply by 0; if there are 3 snapshots and one
	  origin subvol, multiply by 3)

  `limit = GB_metadata * (disk_space_in_snapshots / total_disk_space) * number_of_snapshots`

  Monitor unallocated space to ensure that the filesystem never runs out
  of metadata space (whether Bees is running or not--this is a general
  btrfs requirement).


A Brief List Of Btrfs Kernel Bugs
---------------------------------

Missing features (usually not available in older LTS kernels):

* 3.13: `FILE_EXTENT_SAME` ioctl added.  No way to reliably dedup with
  concurrent modifications before this.
* 3.16: `SEARCH_V2` ioctl added.  Bees could use `SEARCH` instead.
* 4.2: `FILE_EXTENT_SAME` no longer updates mtime, can be used at EOF.

Future features (kernel features Bees does not yet use, but may rely on
in the future):

* 4.14: `LOGICAL_INO_V2` allows userspace to create forward and backward
  reference maps to entire physical extents with a single ioctl call,
  and raises the limit of 2730 references per extent.  Bees has not yet
  been rewritten to take full advantage of these features.

Bug fixes (sometimes included in older LTS kernels):

* Bugs fixed prior to 4.4.107 are not listed here.
* 4.5: hang in the `INO_PATHS` ioctl used by Bees.
* 4.5: use-after-free in the `FILE_EXTENT_SAME` ioctl used by Bees.
* 4.6: lost inodes after a rename, crash, and log tree replay
  (triggered by the fsync() while writing `beescrawl.dat`).
* 4.7: *slow backref* bug no longer triggers a softlockup panic.  It still
  takes too long to resolve a block address to a root/inode/offset triple.
* 4.10: reduced CPU time cost of the LOGICAL_INO ioctl and dedup
  backref processing in general.
* 4.11: yet another dedup deadlock case is fixed.  Alas, it is not the
  last one.
* 4.14: backref performance improvements make LOGICAL_INO even faster
  in the worst cases (but possibly slower in the best cases?).
* 4.14.29: WARN_ON(ref->count < 0) in fs/btrfs/backref.c triggers
  almost once per second.  The WARN_ON is incorrect and can be removed.

Unfixed kernel bugs (as of 4.14.34) with workarounds in Bees:

* *Deadlocks* in the kernel dedup ioctl when files are modified
  immediately before dedup.  `BeesTempFile::make_copy` calls `fsync()`
  immediately before dedup to work around this.  If the `fsync()` is
  removed, the filesystem hangs within a few hours, requiring a reboot
  to recover.  Even with the `fsync()`, it is possible to lose the
  kernel race condition and encounter a deadlock within a machine-year.
  VM image workloads may trigger this faster.  Over the past years
  several specific deadlock cases have been fixed, but at least one
  remains.

* *Bad interactions* with other Linux block layers:  bcache and lvmcache
  can fail spectacularly, and apparently only while running bees.
  This is definitely a kernel bug, either in btrfs or the lower block
  layers.  Avoid using bees with these tools, or test very carefully
  before deployment.

* *slow backrefs* (aka toxic extents): If the number of references to a
  single shared extent within a single file grows above a few thousand,
  the kernel consumes CPU for minutes at a time while holding various
  locks that block access to the filesystem.  Bees avoids this bug by
  measuring the time the kernel spends performing certain operations
  and permanently blacklisting any extent or hash where the kernel
  starts to get slow.  Inside Bees, such blocks are marked as 'toxic'
  hash/block addresses.  Linux kernel v4.14 is better but can still
  have problems.

* `LOGICAL_INO` output is arbitrarily limited to 2730 references
  even if more buffer space is provided for results.  Once this number
  has been reached, Bees can no longer replace the extent since it can't
  find and remove all existing references.  Bees refrains from adding
  any more references after the first 2560.  Offending blocks are
  marked 'toxic' even if there is no corresponding performance problem.
  This places an obvious limit on dedup efficiency for extremely common
  blocks or filesystems with many snapshots (although this limit is
  far greater than the effective limit imposed by the *slow backref* bug).
  *Fixed in v4.14.*

* `LOGICAL_INO` on compressed extents returns a list of root/inode/offset
  tuples matching the extent bytenr of its argument.  On uncompressed
  extents, any r/i/o tuple whose extent offset does not match the
  argument's extent offset is discarded, i.e. only the single 4K block
  matching the argument is returned, so a complete map of the extent
  references requires calling `LOGICAL_INO` for every single block of
  the extent.  This is undesirable behavior for Bees, which wants a
  list of all extent refs referencing a data extent (i.e. Bees wants
  the compressed-extent behavior in all cases).  *Fixed in v4.14.*

* `FILE_EXTENT_SAME` is arbitrarily limited to 16MB.  This is less than
  128MB which is the maximum extent size that can be created by defrag
  or prealloc.  Bees avoids feedback loops this can generate while
  attempting to replace extents over 16MB in length.

Not really bugs, but gotchas nonetheless:

* If a process holds a directory FD open, the subvol containing the
  directory cannot be deleted (`btrfs sub del` will start the deletion
  process, but it will not proceed past the first open directory FD).
  `btrfs-cleaner` will simply skip over the directory *and all of its
  children* until the FD is closed.  Bees avoids this gotcha by closing
  all of the FDs in its directory FD cache every 10 btrfs transactions.

* If a file is deleted while Bees is caching an open FD to the file,
  Bees continues to scan the file.  For very large files (e.g. VM
  images), the deletion of the file can be delayed indefinitely.
  To limit this delay, Bees closes all FDs in its file FD cache every
  10 btrfs transactions.

* If a snapshot is deleted, bees will generate a burst of exceptions
  for references to files in the snapshot that no longer exist.  This
  lasts until the FD caches are cleared.

Installation
============

Bees can be installed by following one these instructions:

Arch package
------------

Bees is available in Arch Linux AUR. Install with:

`$ pacaur -S bees-git`

Gentoo package
--------------

Bees is officially available in Gentoo Portage. Just emerge a stable
version:

`$ emerge --ask bees`

or build a live version from git master:

`$ emerge --ask =bees-9999`

You can opt-out of building the support tools with

`USE="-tools" emerge ...`

If you want to start hacking on bees and contribute changes, just emerge
the live version which automatically pulls in all required development
packages.

Build from source
-----------------

Build with `make`. The build produces `bin/bees` and `lib/libcrucible.so`,
which must be copied to somewhere in `$PATH` and `$LD_LIBRARY_PATH`
on the target system respectively.

It will also generate `scripts/beesd@.service` for systemd users. This
service makes use of a helper script `scripts/beesd` to boot the service.
Both of the latter use the filesystem UUID to mount the root subvolume
within a temporary runtime directory.

### Ubuntu 16.04 - 17.04:
`$ apt -y install build-essential btrfs-tools uuid-dev markdown && make`

### Ubuntu 14.04:
You can try to carry on the work done here: https://gist.github.com/dagelf/99ee07f5638b346adb8c058ab3d57492

Packaging
---------

See 'Dependencies' below. Package maintainers can pick ideas for building and
configuring the source package from the Gentoo ebuild:

https://github.com/gentoo/gentoo/tree/master/sys-fs/bees

You can configure some build options by creating a file `localconf` and
adjust settings for your distribution environment there.

Please also review the Makefile for additional hints.

Dependencies
------------

* C++11 compiler (tested with GCC 4.9, 6.2.0, 8.1.0)

  Sorry.  I really like closures and shared_ptr, so support
  for earlier compiler versions is unlikely.

* btrfs-progs (tested with 4.1..4.15.1) or libbtrfs-dev
  (tested with version 4.16.1)

  Needed for btrfs.h and ctree.h during compile.
  Also needed by the service wrapper script.

* libuuid-dev

  This library is only required for a feature that was removed after v0.1.
  The lingering support code can be removed.

* Linux kernel version: *minimum* 4.4.107, *4.14.29 or later recommended*

  Don't bother trying to make Bees work with kernel versions older than
  4.4.107.  It may appear to work, but it won't end well:  there are
  too many missing features and bugs (including data corruption bugs)
  to work around in older kernels.

  Kernel versions between 4.4.107 and 4.14.29 are usable with bees,
  but bees can trigger known performance bugs and hangs in dedup-related
  functions.

* markdown

* util-linux version that provides `blkid` command for the helper
  script `scripts/beesd` to work

Setup
-----

If you don't want to use the helper script `scripts/beesd` to setup and
configure bees, here's how you manually setup bees.

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

There are some runtime configurable options using environment variables:

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
in src/bees.h or on the cmdline (see 'Command Line Options' below).

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



Command Line Options
--------------------

* --thread-count (-c) COUNT
  * Specify maximum number of worker threads for scanning.  Overrides
    --thread-factor (-C) and default/autodetected values.
* --thread-factor (-C) FACTOR
  * Specify ratio of worker threads to CPU cores.  Overridden by --thread-count (-c).
    Default is 1.0, i.e. 1 worker thread per detected CPU.  Use values
    below 1.0 to leave some cores idle, or above 1.0 if there are more
    disks than CPUs in the filesystem.
* --loadavg-target (-g) LOADAVG
  * Specify load average target for dynamic worker threads.
    Threads will be started or stopped subject to the upper limit imposed
    by thread-factor, thread-min and thread-count until the load average
    is within +/- 0.5 of LOADAVG.
* --thread-min (-G) COUNT
  * Specify minimum number of worker threads for scanning.
    Ignored unless -g option is used to specify a target load.

* --scan-mode (-m) MODE
  * Specify extent scanning algorithm.  Default mode is 0.
    _EXPERIMENTAL_ feature that may go away.
    * Mode 0: scan extents in ascending order of (inode, subvol, offset).
      Keeps shared extents between snapshots together.  Reads files sequentially.
      Minimizes temporary space usage.
    * Mode 1: scan extents from all subvols in parallel.  Good performance
      on non-spinning media when subvols are unrelated.
    * Mode 2: scan all extents from one subvol at a time.  Good sequential
      read performance for spinning media.  Maximizes temporary space usage.

* --timestamps (-t)
  * Enable timestamps in log output.
* --no-timestamps (-T)
  * Disable timestamps in log output.
* --absolute-paths (-p)
  * Paths in log output will be absolute.
* --strip-paths (-P)
  * Paths in log output will have the working directory at Bees startup
    stripped.
* --verbose (-v)
  * Set log verbosity (0 = no output, 8 = all output, default 8).


Bug Reports and Contributions
-----------------------------

Email bug reports and patches to Zygo Blaxell <bees@furryterror.org>.

You can also use Github:

        https://github.com/Zygo/bees



Copyright & License
===================

Copyright 2015-2017 Zygo Blaxell <bees@furryterror.org>.

GPL (version 3 or later).
