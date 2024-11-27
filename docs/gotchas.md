bees Gotchas
============

C++ Exceptions
--------------

bees is very paranoid about the data it gets from btrfs, and if btrfs
does anything bees does not expect, bees will throw an exception and move
on without touching the offending data.  This will trigger a stack trace
to the log containing data which is useful for developers to understand
what happened.

In all cases C++ exceptions in bees are harmless to data on the
filesystem.  bees handles most exceptions by aborting processing of
the current extent and moving to the next extent.  In some cases an
exception may occur in a critical bees thread, which will stop the bees
process from making any further progress; however, these cases are rare
and are typically caused by unusual filesystem conditions (e.g. [freshly
formatted filesystem with no
data](https://github.com/Zygo/bees/issues/93)) or lack of memory or
other resources.

The following are common cases that users may encounter:

* If a snapshot is deleted, bees will generate a burst of exceptions for
references to files in the snapshot that no longer exist.  This lasts
until the FD caches are cleared, usually a few minutes with default
btrfs mount options.  These generally look like:

	`std::system_error: BTRFS_IOC_TREE_SEARCH_V2: [path] at fs.cc:844: No such file or directory`

* If data is modified at the same time it is being scanned, bees will get
an inconsistent version of the data layout in the filesystem, causing
the `ExtentWalker` class to throw various constraint-check exceptions.
The exception causes bees to retry the extent in a later filesystem scan
(hopefully when the file is no longer being modified).  The exception
text is similar to:

	`std::runtime_error: fm.rbegin()->flags() = 776 failed constraint check (fm.rbegin()->flags() & FIEMAP_EXTENT_LAST) at extentwalker.cc:229`

  but the line number or specific code fragment may vary.

* If there are too many possible matching blocks within a pair of extents,
bees will loop billions of times considering all possibilities.  This is
a waste of time, so an exception is currently used to break out of such
loops early.  The exception text in this case is:

	`FIXME: too many duplicate candidates, bailing out here`


Terminating bees with SIGTERM
-----------------------------

bees is designed to survive host crashes, so it is safe to terminate bees
using SIGKILL; however, when bees next starts up, it will repeat some
work that was performed between the last bees crawl state save point
and the SIGKILL (up to 15 minutes), and a large hash table may not be
completely written back to disk, so some duplicate matches will be lost.

If bees is stopped and started less than once per week, then this is not
a problem as the proportional impact is quite small; however, users who
stop and start bees daily or even more often may prefer to have a clean
shutdown with SIGTERM so bees can restart faster.

The shutdown procedure performs these steps:

   1.  Crawl state is saved to `$BEESHOME`.  This is the most
       important bees state to save to disk as it directly impacts
       restart time, so it is done as early as possible

   2.  Hash table is written to disk.  Normally the hash table is
       trickled back to disk at a rate of about 128KiB per second;
       however, SIGTERM causes bees to attempt to flush the whole table
       immediately.  The time spent here depends on the size of RAM, speed
       of disks, and aggressiveness of competing filesystem workloads.
       It can trigger `vm.dirty_bytes` limits and block other processes
       writing to the filesystem for a while.

   3.  The bees process calls `_exit`, which terminates all running
       worker threads, closes and deletes all temporary files.  This
       can take a while _after_ the bees process exits, especially on
       slow spinning disks.


Balances
--------

A btrfs balance relocates data on disk by making a new copy of the
data, replacing all references to the old data with references to the
new copy, and deleting the old copy.  To bees, this is the same as any
other combination of new and deleted data (e.g. from defrag, or ordinary
file operations):  some new data has appeared (to be scanned) and some
old data has disappeared (to be removed from the hash table when it is
detected).

As bees scans the newly balanced data, it will get hits on the hash
table pointing to the old data (it's identical data, so it would look
like a duplicate).  These old hash table entries will not be valid any
more, so when bees tries to compare new data with old data, it will not
be able to find the old data at the old address, and bees will delete
the hash table entries.  If no other duplicates are found, bees will
then insert new hash table entries pointing to the new data locations.
The erase is performed before the insert, so the new data simply replaces
the old and there is (little or) no impact on hash table entry lifetimes
(depending on how overcommitted the hash table is).  Each block is
processed one at a time, which can be slow if there are many of them.

Routine btrfs maintenance balances rarely need to relocate more than 0.1%
of the total filesystem data, so the impact on bees is small even after
taking into account the extra work bees has to do.

If the filesystem must undergo a full balance (e.g. because disks were
added or removed, or to change RAID profiles), then every data block on
the filesystem will be relocated to a new address, which invalidates all
the data in the bees hash table at once.  In such cases it is a good idea to:

  1.  Stop bees before the full balance starts,
  2.  Wipe the `$BEESHOME` directory (or delete and recreate `beeshash.dat`),
  3.  Restart bees after the full balance is finished.

bees will perform a full filesystem scan automatically after the balance
since all the data has "new" btrfs transids.  bees won't waste any time
invalidating stale hash table data after the balance if the hash table
is empty.  This can considerably improve the performance of both bees
(since it has no stale hash table entries to invalidate) and btrfs balance
(since it's not competing with bees for iops).

Snapshots
---------

bees can dedupe filesystems with many snapshots, but bees only does
well in this situation if bees was running on the filesystem from
the beginning.

Each time bees dedupes an extent that is referenced by a snapshot,
the entire metadata page in the snapshot subvol (16KB by default) must
be CoWed in btrfs.  Since all references must be removed at the same
time, this CoW operation is repeated in every snapshot containing the
duplicate data.  This can result in a substantial increase in btrfs
metadata size if there are many snapshots on a filesystem.

Normally, metadata is small (less than 1% of the filesystem) and dedupe
hit rates are large (10-40% of the filesystem), so the increase in
metadata size is offset by much larger reductions in data size and the
total space used by the entire filesystem is reduced.

If a subvol is deduped _before_ a snapshot is created, the snapshot will
have the same deduplication as the subvol.  This does _not_ result in
unusually large metadata sizes.  If a snapshot is made after bees has
fully scanned the origin subvol, bees can avoid scanning most of the
data in the snapshot subvol, as it will be provably identical to the
origin subvol that was already scanned.

If a subvol is deduped _after_ a snapshot is created, the origin and
snapshot subvols must be deduplicated separately.  In the worst case, this
will double the amount of reading the bees scanner must perform, and will
also double the amount of btrfs metadata used for the snapshot; however,
the "worst case" is a dedupe hit rate of 1% or more, so a doubling of
metadata size is certain for all but the most unique data sets.  Also,
bees will not be able to free any space until the last snapshot has been
scanned and deduped, so payoff in data space savings is deferred until
the metadata has almost finished expanding.

If a subvol is deduped after _many_ snapshots have been created, all
subvols must be deduplicated individually.  In the worst case, this will
multiply the scanning work and metadata size by the number of snapshots.
For 100 snapshots this can mean a 100x growth in metadata size and
bees scanning time, which typically exceeds the possible savings from
reducing the data size by dedupe.  In such cases using bees will result
in a net increase in disk space usage that persists until the snapshots
are deleted.

Snapshot case studies
---------------------

 * bees running on an empty filesystem
   * filesystem is mkfsed
   * bees is installed and starts running
   * data is written to the filesystem
   * bees dedupes the data as it appears
   * a snapshot is made of the data
      * The snapshot will already be 99% deduped, so the metadata will
      not expand very much because only 1% of the data in the snapshot
      must be deduped.
   * more snapshots are made of the data
      * as long as dedupe has been completed on the origin subvol,
      bees will quickly scan each new snapshot because it can skip
      all the previously scanned data.  Metadata usage remains low
      (it may even shrink because there are fewer csums).

 * bees installed on a non-empty filesystem with snapshots
   * filesystem is mkfsed
   * data is written to the filesystem
   * multiple snapshots are made of the data
   * bees is installed and starts running
   * bees dedupes each snapshot individually
      * The snapshot metadata will no longer be shared, resulting in
      substantial growth of metadata usage.
      * Disk space savings do not occur until bees processes the
      last snapshot reference to data.


Other Gotchas
-------------

* bees avoids the [slow backrefs kernel bug](btrfs-kernel.md) by
  measuring the time required to perform `LOGICAL_INO` operations.
  If an extent requires over 5.0 kernel CPU seconds to perform a
  `LOGICAL_INO` ioctl, then bees blacklists the extent and avoids
  referencing it in future operations.  In most cases, fewer than 0.1%
  of extents in a filesystem must be avoided this way.  This results
  in short write latency spikes as btrfs will not allow writes to the
  filesystem while `LOGICAL_INO` is running.  Generally the CPU spends
  most of the runtime of the `LOGICAL_INO` ioctl running the kernel,
  so on a single-core CPU the entire system can freeze up for a second
  during operations on toxic extents.  Note this only occurs on older
  kernels.  See [the slow backrefs kernel bug section](btrfs-kernel.md).

* If a process holds a directory FD open, the subvol containing the
  directory cannot be deleted (`btrfs sub del` will start the deletion
  process, but it will not proceed past the first open directory FD).
  `btrfs-cleaner` will simply skip over the directory *and all of its
  children* until the FD is closed.  bees avoids this gotcha by closing
  all of the FDs in its directory FD cache every btrfs transaction.

* If a file is deleted while bees is caching an open FD to the file,
  bees continues to scan the file.  For very large files (e.g. VM
  images), the deletion of the file can be delayed indefinitely.
  To limit this delay, bees closes all FDs in its file FD cache every
  btrfs transaction.
