Recommended kernel version
==========================

Currently 5.0.21, 5.3.4, and *chronologically* later versions are
recommended to avoid all currently known and fixed kernel issues and
obtain best performance.  Older kernel versions can be used with bees
with some caveats (see below).

Kernels 5.1.21 and 5.2.21 are *not recommended* due to possible conflicts
between LOGICAL_INO and btrfs balance.

All unmaintained kernel trees (those which do not receive -stable updates)
should be avoided due to potential data corruption bugs.

**Kernels older than 4.2 cannot run bees at all** due to missing features.

DATA CORRUPTION WARNING
-----------------------

There is a data corruption bug in older Linux kernel versions that can
be triggered by bees.  The bug can be triggered in other ways, but bees
will trigger it especially often.

This bug is **fixed** in the following kernel versions:

* **5.1 or later** versions.

* **5.0.4 or later 5.0.y** versions.

* **4.19.31 or later 4.19.y** LTS versions.

* **4.14.108 or later 4.14.y** LTS versions.

* **4.9.165 or later 4.9.y** LTS versions.

* **4.4.177 or later 4.4.y** LTS versions.

* **v3.18.137 or later 3.18.y** LTS versions (note these versions cannot
run bees).

All older kernel versions (including 4.20.17, 4.18.20, 4.17.19, 4.16.18,
4.15.18) have the data corruption bug.

The commit that fixes the last known data corruption bug is
8e928218780e2f1cf2f5891c7575e8f0b284fcce "btrfs: fix corruption reading
shared and compressed extents after hole punching".


Lockup/hang WARNING
-------------------

Kernel versions prior to 5.0.4 have a deadlock bug when file A is
renamed to replace B while both files A and B are referenced in a
dedupe operation.  This situation may arise often while bees is running,
which will make processes accessing the filesystem hang while writing.
A reboot is required to recover.  No data is lost when this occurs
(other than unflushed writes due to the reboot).

A common problem case is rsync receiving updates to large files when not
in `--inplace` mode.  If the file is sufficiently large, bees will start
to dedupe the original file and rsync's temporary modified version of
the file while rsync is still writing the modified version of the file.
Later, when rsync renames the modified temporary file over the original
file, the rename in rsync can occasionally deadlock with the dedupe
in bees.

This bug is **fixed** in 5.0.4 and later kernel versions.

The commit that fixes this bug is 4ea748e1d2c9f8a27332b949e8210dbbf392987e
"btrfs: fix deadlock between clone/dedupe and rename".


LOGICAL_INO and btrfs balance WARNING
-------------------------------------

There are at least two bugs that can be triggered by running the
`LOGICAL_INO` ioctl (which bees uses heavily) and btrfs balance at
the same time.  One of these is fixed as of kernel 5.3.4 with commit
efad8a853ad2057f96664328a0d327a05ce39c76 "Btrfs: fix use-after-free when
using the tree modification log".

The other bug(s) still cause crashes in testing, their root cause is
unknown, and no fix is currently available as of 5.3.13.

As a workaround, bees will simply detect that a btrfs balance is running,
and pause bees execution until the balance is done.  This avoids running
both the `LOGICAL_INO` ioctl and btrfs balance at the same time, which so
far seems to prevent the bug from occurring.

Note that in the worst cases, this bug is believed to cause filesystem
metadata corruption on 5.1.21 and 5.2.21 kernels (i.e. metadata corruption
definitely happens on these kernels, and it seems to happen under the
same conditions as other crashes, though the connection between the
known behavior and unknown bug(s) is unknown).

Kernel 5.2 will detect the metadata corruption before writing it to disk,
and force a transaction abort, leaving the filesystem mounted read-only.
Kernel 5.1 has no such detection capability, and will corrupt the metadata
on disk.  Once metadata corruption is persisted on disk, a `btrfs check
--repair` often repairs the damage.  Note that `btrfs check --repair` is a
high-risk operation, so make a backup of the disk, or copy all of the data
with `btrfs restore`, before attempting to run `btrfs check --repair`.

So far, 5.0 and earlier kernels will only crash when encountering these
bugs, no metadata corruption has yet been observed.  The known bug
affects kernels 3.10 and later (i.e. every kernel that can run bees).
The unknown bug's age is unknown, it has only been easily reproducible
after the first bug was fixed.





A Brief List Of btrfs Kernel Bugs
---------------------------------

Unfixed kernel bugs (as of 5.0.21):

Minor kernel problems with workarounds:

* **Conflicts between `LOGICAL_INO` ioctl and btrfs balance**:
  bees will simply check to see if a balance is running immediately
  before invoking the `LOGICAL_INO` ioctl, and delay execution until
  the balance is no longer running.

* **Slow backrefs** (aka toxic extents):  Under certain conditions,
  if the number of references to a single shared extent grows too high,
  the kernel consumes more and more CPU while holding locks that block
  access to the filesystem.  bees avoids this bug by measuring the time
  the kernel spends performing `LOGICAL_INO` operations and permanently
  blacklisting any extent or hash involved where the kernel starts
  to get slow.  In the bees log, such blocks are labelled as 'toxic'
  hash/block addresses.  Toxic extents are rare (about 1 in 100,000
  extents become toxic), but toxic extents can become 8 orders of
  magnitude more expensive to process than the fastest non-toxic
  extents.  This seems to affect all dedupe agents on btrfs; at this
  time of writing only bees has a workaround for this bug.

* **btrfs send** has bugs that are triggered when bees is
  deduping snapshots.  bees provides the [`--workaround-btrfs-send`
  option](options.md) which should be used whenever `btrfs send` and
  bees are run on the same filesystem.

  Note `btrfs receive` is not affected, nor is any other btrfs operation
  except `send`.  It is OK to run bees with no workarounds on a filesystem
  that receives btrfs snapshots.

  A fix for one problem has been [merged into kernel
  5.2-rc1](https://github.com/torvalds/linux/commit/62d54f3a7fa27ef6a74d6cdf643ce04beba3afa7).
  bees has not been updated to handle the new EAGAIN case optimally,
  but the excess error messages that are produced are harmless.

  The other problem is that [parent snapshots for incremental sends
  are broken by bees](https://github.com/Zygo/bees/issues/115), even
  when the snapshots are deduped while send is not running.

* **btrfs send** also seems to have severe performance issues with
  dedupe agents that produce toxic extents.  bees has a workaround to
  prevent this where possible.

* **Systems with many CPU cores** may [lock up when bees runs with one
  worker thread for every core](https://github.com/Zygo/bees/issues/91).
  bees limits the number of threads it will try to create based on
  detected CPU core count.  Users may override this limit with the
  [`--thread-count` option](options.md).  It is possible this is the
  same bug as the next one:

* **Spurious warnings in `fs/fs-writeback.c`** on kernel 4.15 and later
  when filesystem is mounted with `flushoncommit`.  These
  seem to be harmless (there are other locks which prevent
  concurrent umount of the filesystem), but the underlying
  problems that trigger the `WARN_ON` are [not trivial to
  fix](https://www.spinics.net/lists/linux-btrfs/msg87752.html).
  Workarounds:

  1. mount with `-o noflushoncommit`
  2. patch kernel to remove warning in `fs/fs-writeback.c`.

  Note that using kernels 4.14 and earlier is *not* a viable workaround
  for this issue, because kernels 4.14 and earlier will eventually
  deadlock when a filesystem is mounted with `-o flushoncommit` (a single
  commit fixes one bug and introduces the other).

* **Spurious kernel warnings in `fs/btrfs/delayed-ref.c`** on 5.0.x.
  This also seems harmless, but there have been [no comments
  since this issue was reported to the `linux-btrfs` mailing
  list](https://www.spinics.net/lists/linux-btrfs/msg89061.html).
  Later kernels do not produce this warning.
  Workaround:  patch kernel to remove the warning.
