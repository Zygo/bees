bees Gotchas
============

Snapshots
---------

bees can dedupe filesystems with many snapshots, but bees only does
well in this situation if bees was running on the filesystem from
the beginning.

Each time bees dedupes an extent that is referenced by a snapshot,
the entire metadata page in the snapshot subvol (16KB by default) must
be CoWed in btrfs.  This can result in a substantial increase in btrfs
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
  If an extent requires over 0.1 kernel CPU seconds to perform a
  `LOGICAL_INO` ioctl, then bees blacklists the extent and avoids
  referencing it in future operations.  In most cases, fewer than 0.1%
  of extents in a filesystem must be avoided this way.  This results
  in short write latency spikes as btrfs will not allow writes to the
  filesystem while `LOGICAL_INO` is running.  Generally the CPU spends
  most of the runtime of the `LOGICAL_INO` ioctl running the kernel,
  so on a single-core CPU the entire system can freeze up for a second
  during operations on toxic extents.

* If a process holds a directory FD open, the subvol containing the
  directory cannot be deleted (`btrfs sub del` will start the deletion
  process, but it will not proceed past the first open directory FD).
  `btrfs-cleaner` will simply skip over the directory *and all of its
  children* until the FD is closed.  bees avoids this gotcha by closing
  all of the FDs in its directory FD cache every 10 btrfs transactions.

* If a file is deleted while bees is caching an open FD to the file,
  bees continues to scan the file.  For very large files (e.g. VM
  images), the deletion of the file can be delayed indefinitely.
  To limit this delay, bees closes all FDs in its file FD cache every
  10 btrfs transactions.

* If a snapshot is deleted, bees will generate a burst of exceptions
  for references to files in the snapshot that no longer exist.  This
  lasts until the FD caches are cleared.
