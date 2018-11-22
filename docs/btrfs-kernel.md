Recommended kernel version
==========================

Linux **4.14.34** or later.

A Brief List Of Btrfs Kernel Bugs
---------------------------------

Recent kernel bug fixes:

* 4.14.29: `WARN_ON(ref->count < 0)` in fs/btrfs/backref.c triggers
  almost once per second.  The `WARN_ON` is incorrect, and is now removed.

Unfixed kernel bugs (as of 4.14.71):

* **Bad _filesystem destroying_ interactions** with other Linux block
  layers:  `bcache` and `lvmcache` can fail spectacularly, and apparently
  only do so while running bees.  This is definitely a kernel bug,
  either in btrfs or the lower block layers.  **Avoid using bees with
  these tools unless your filesystem is disposable and you intend to
  debug the kernel.**

* **Compressed data corruption** is possible when using the `fallocate`
  system call to punch holes into compressed extents that contain long
  runs of zeros.  The [bug results in intermittent corruption during
  reads](https://www.spinics.net/lists/linux-btrfs/msg81293.html), but
  due to the bug, the kernel might sometimes mistakenly determine data
  is duplicate, and deduplication will corrupt the data permanently.
  This bug also affects compressed `kvm` raw images with the `discard`
  feature on btrfs or any compressed file where `fallocate -d` or
  `fallocate -p` has been used.

* **Deadlock** when [simultaneously using the same files in dedupe and
  `rename`](https://www.spinics.net/lists/linux-btrfs/msg81109.html).
  There is no way for bees to reliably know when another process is
  about to rename a file while bees is deduping it.  In the `rsync` case,
  bees will dedupe the new file `rsync` is creating using the old file
  `rsync` is copying from, while `rsync` will rename the new file over
  the old file to replace it.

Minor kernel problems with workarounds:

* **Slow backrefs** (aka toxic extents):  Under certain conditions,
  if the number of references to a single shared extent grows too high,
  the kernel consumes more and more CPU while holding locks that block
  access to the filesystem.  bees avoids this bug by measuring the time
  the kernel spends performing `LOGICAL_INO` operations and permanently
  blacklisting any extent or hash involved where the kernel starts
  to get slow.  In the bees log, such blocks are labelled as 'toxic'
  hash/block addresses.

* **btrfs send** has various bugs that are triggered when bees is
  deduping snapshots.  bees provides the [`--workaround-btrfs-send`
  option](options.md) which should be used whenever `btrfs send` and
  bees are run on the same filesystem.

  This issue affects:
   * `btrfs send` (any mode) and bees active at the same time.
   * `btrfs send` in incremental mode (using `-p` option) with bees
     active at the same or different times.

  Note `btrfs receive` is not affected.  It is OK to run bees with no
  workarounds on a filesystem that receives btrfs snapshots.

* **Systems with many CPU cores** may [lock up when bees runs with one
  worker thread for every core](https://github.com/Zygo/bees/issues/91).
  bees limits the number of threads it will try to create based on
  detected CPU core count.  Users may override this limit with the
  [`--thread-count` option](options.md).

Older kernels:

* Older kernels have various data corruption and deadlock/hang issues
  that are no longer listed here, and older kernels are missing important
  features such as `LOGICAL_INO_V2`.  Using an older kernel is not
  recommended.
