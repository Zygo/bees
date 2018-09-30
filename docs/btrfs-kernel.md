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

* **Slow backrefs** (aka toxic extents): If the number of references to a
  single shared extent within a single file grows above a few thousand,
  the kernel consumes CPU for minutes at a time while holding various
  locks that block access to the filesystem.  bees avoids this bug
  by measuring the time the kernel spends performing `LOGICAL_INO`
  operations and permanently blacklisting any extent or hash involved
  where the kernel starts to get slow.  Inside bees, such blocks are
  known as 'toxic' hash/block addresses.

* **`FILE_EXTENT_SAME` is arbitrarily limited to 16MB**.  This is
  less than 128MB which is the maximum extent size that can be created
  by defrag, prealloc, or filesystems without the `compress-force`
  mount option.  bees avoids feedback loops this can generate while
  attempting to replace extents over 16MB in length.
