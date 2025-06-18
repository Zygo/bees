Recommended Linux Kernel Version for bees
=========================================

First, a warning about old Linux kernel versions:

> **Linux kernel version 5.1, 5.2, and 5.3 should not be used with btrfs
due to a severe regression that can lead to fatal metadata corruption.**
This issue is fixed in version 5.4.14 and later.

**Recommended Linux kernel versions for bees are 5.4, 5.10, 5.15, 6.1,
6.6, or 6.12 with recent LTS and -stable updates.**  The latest released
kernel as of this writing is 6.12.9, and the earliest supported LTS
kernel is 5.4.

Some optional bees features use kernel APIs introduced in kernel 4.15
(extent scan) and 5.6 (`openat2` support).  These bees features are not
available on older kernels.  Support for older kernels may be removed
in a future bees release.

bees will not run at all on kernels before 4.2 due to lack of minimal
API support.



Kernel Bug Tracking Table
-------------------------

These bugs are particularly popular among bees users, though not all are specifically relevant to bees:

| First bad kernel | Last bad kernel | Issue Description | Fixed Kernel Versions | Fix Commit
| :---: | :---: | --- | :---: | ---
| - | 4.10 | garbage inserted in read data when reading compressed inline extent followed by a hole | 3.18.89, 4.1.49, 4.4.107, 4.9.71, 4.11 and later | e1699d2d7bf6 btrfs: add missing memset while reading compressed inline extents
| - | 4.14 | spurious warnings from `fs/btrfs/backref.c` in `find_parent_nodes` | 3.16.57, 4.14.29, 4.15.12, 4.16 and later | c8195a7b1ad5 btrfs: remove spurious WARN_ON(ref->count < 0) in find_parent_nodes
| 4.15 | 4.18 | compression ratio and performance regression on bees test corpus | improved in 4.19 | 4.14 performance not fully restored yet
| - | 5.0 | silently corrupted data returned when reading compressed extents around a punched hole (bees dedupes all-zero data blocks with holes which can produce a similar effect to hole punching) | 3.16.70, 3.18.137, 4.4.177, 4.9.165, 4.14.108, 4.19.31, 5.0.4, 5.1 and later | 8e928218780e Btrfs: fix corruption reading shared and compressed extents after hole punching
| - | 5.0 | deadlock when dedupe and rename are used simultaneously on the same files | 5.0.4, 5.1 and later | 4ea748e1d2c9 Btrfs: fix deadlock between clone/dedupe and rename
| - | 5.1 | send failure or kernel crash while running send and dedupe on same snapshot at same time | 5.0.18, 5.1.4, 5.2 and later | 62d54f3a7fa2 Btrfs: fix race between send and deduplication that lead to failures and crashes
| - | 5.2 | alternating send and dedupe results in incremental send failure | 4.9.188, 4.14.137, 4.19.65, 5.2.7, 5.3 and later | b4f9a1a87a48 Btrfs: fix incremental send failure after deduplication
| 4.20 | 5.3 | balance convert to single rejected with error on 32-bit CPUs | 5.3.7, 5.4 and later | 7a54789074a5 btrfs: fix balance convert to single on 32-bit host CPUs
| - | 5.3 | kernel crash due to tree mod log issue #1 (often triggered by bees) | 3.16.79, 4.4.195, 4.9.195, 4.14.147, 4.19.77, 5.2.19, 5.3.4, 5.4 and later | efad8a853ad2 Btrfs: fix use-after-free when using the tree modification log
| - | 5.4 | kernel crash due to tree mod log issue #2 (often triggered by bees) | 3.16.83, 4.4.208, 4.9.208, 4.14.161, 4.19.92, 5.4.7, 5.5 and later | 6609fee8897a Btrfs: fix removal logic of the tree mod log that leads to use-after-free issues
| 5.1 | 5.4 | metadata corruption resulting in loss of filesystem when a write operation occurs while balance starts a new block group.  **Do not use kernel 5.1 with btrfs.**  Kernel 5.2 and 5.3 have workarounds that may detect corruption in progress and abort before it becomes permanent, but do not prevent corruption from occurring.  Also kernel crash due to tree mod log issue #4. | 5.4.14, 5.5 and later | 6282675e6708 btrfs: relocation: fix reloc_root lifespan and access
| - | 5.4 | send performance failure when shared extents have too many references | 4.9.207, 4.14.159, 4.19.90, 5.3.17, 5.4.4, 5.5 and later | fd0ddbe25095 Btrfs: send, skip backreference walking for extents with many references
| 5.0 | 5.5 | dedupe fails to remove the last extent in a file if the file size is not a multiple of 4K | 5.4.19, 5.5.3, 5.6 and later | 831d2fa25ab8 Btrfs: make deduplication with range including the last block work
| 4.5, backported to 3.18.31, 4.1.22, 4.4.4 | 5.5 | `df` incorrectly reports 0 free space while data space is available.  Triggered by changes in metadata size, including those typical of large-scale dedupe.  Occurs more often starting in 5.3 and especially 5.4 | 4.4.213, 4.9.213, 4.14.170, 4.19.102, 5.4.18, 5.5.2, 5.6 and later | d55966c4279b btrfs: do not zero f_bavail if we have available space
| - | 5.5 | kernel crash due to tree mod log issue #3 (often triggered by bees) | 3.16.84, 4.4.214, 4.9.214, 4.14.171, 4.19.103, 5.4.19, 5.5.3, 5.6 and later | 7227ff4de55d Btrfs: fix race between adding and putting tree mod seq elements and nodes
| - | 5.6 | deadlock when enumerating file references to physical extent addresses while some references still exist in deleted subvols | 5.7 and later | 39dba8739c4e btrfs: do not resolve backrefs for roots that are being deleted
| - | 5.6 | deadlock when many extent reference updates are pending and available memory is low | 4.14.177, 4.19.116, 5.4.33, 5.5.18, 5.6.5, 5.7 and later | 351cbf6e4410 btrfs: use nofs allocations for running delayed items
| - | 5.6 | excessive CPU usage in `LOGICAL_INO` and `FIEMAP` ioctl and increased btrfs write latency in other processes when bees translates from extent physical address to list of referencing files and offsets.  Also affects other tools like `duperemove` and `btrfs send` | 5.4.96, 5.7 and later | b25b0b871f20 btrfs: backref, use correct count to resolve normal data refs, plus 3 parent commits.  Some improvements also in earlier kernels.
| - | 5.7 | filesystem becomes read-only if out of space while deleting snapshot | 4.9.238, 4.14.200, 4.19.149, 5.4.69, 5.8 and later | 7c09c03091ac btrfs: don't force read-only after error in drop snapshot
| 5.1 | 5.7 | balance, device delete, or filesystem shrink operations loop endlessly on a single block group without decreasing extent count | 5.4.54, 5.7.11, 5.8 and later | 1dae7e0e58b4 btrfs: reloc: clear DEAD\_RELOC\_TREE bit for orphan roots to prevent runaway balance
| - | 5.8 | deadlock in `TREE_SEARCH` ioctl (core component of bees filesystem scanner), followed by regression in deadlock fix | 4.4.237, 4.9.237, 4.14.199, 4.19.146, 5.4.66, 5.8.10 and later | a48b73eca4ce btrfs: fix potential deadlock in the search ioctl, 1c78544eaa46 btrfs: fix wrong address when faulting in pages in the search ioctl
| 5.7 | 5.10 | kernel crash if balance receives fatal signal e.g. Ctrl-C | 5.4.93, 5.10.11, 5.11 and later | 18d3bff411c8 btrfs: don't get an EINTR during drop_snapshot for reloc
| 5.10 | 5.10 | 20x write performance regression | 5.10.8, 5.11 and later | e076ab2a2ca7 btrfs: shrink delalloc pages instead of full inodes
| 5.4 | 5.11 | spurious tree checker failures on extent ref hash | 5.4.125, 5.10.43, 5.11.5, 5.12 and later | 1119a72e223f btrfs: tree-checker: do not error out if extent ref hash doesn't match
| - | 5.11 | tree mod log issue #5 | 4.4.263, 4.9.263, 4.14.227, 4.19.183, 5.4.108, 5.10.26, 5.11.9, 5.12 and later | dbcc7d57bffc btrfs: fix race when cloning extent buffer during rewind of an old root
| - | 5.12 | tree mod log issue #6 | 4.14.233, 4.19.191, 5.4.118, 5.10.36, 5.11.20, 5.12.3, 5.13 and later | f9690f426b21 btrfs: fix race when picking most recent mod log operation for an old root
| 5.11 | 5.12 | subvols marked for deletion with `btrfs sub del` become permanently undeletable ("ghost" subvols) | 5.12 stopped creation of new ghost subvols | Partially fixed in 8d488a8c7ba2 btrfs: fix subvolume/snapshot deletion not triggered on mount.  Qu wrote a [patch](https://github.com/adam900710/linux/commit/9de990fcc8864c376eb28aa7482c54321f94acd4) to allow `btrfs sub del -i` to remove "ghost" subvols, but it was never merged upstream.
| 4.15 | 5.16 | spurious warnings from `fs/fs-writeback.c` when `flushoncommit` is enabled | 5.15.27, 5.16.13, 5.17 and later | a0f0cf8341e3 btrfs: get rid of warning on transaction commit when using flushoncommit
| - | 5.17 | crash during device removal can make filesystem unmountable | 5.15.54, 5.16.20, 5.17.3, 5.18 and later | bbac58698a55 btrfs: remove device item and update super block in the same transaction
| - | 5.18 | wrong superblock num_devices makes filesystem unmountable | 4.14.283, 4.19.247, 5.4.198, 5.10.121, 5.15.46, 5.17.14, 5.18.3, 5.19 and later | d201238ccd2f btrfs: repair super block num_devices automatically
| 5.18 | 5.19 | parent transid verify failed during log tree replay after a crash during a rename operation | 5.18.18, 5.19.2, 6.0 and later | 723df2bcc9e1 btrfs: join running log transaction when logging new name
| 5.12 | 6.0 | space cache corruption and potential double allocations | 5.15.65, 5.19.6, 6.0 and later | ced8ecf026fd btrfs: fix space cache corruption and potential double allocations
| 6.0 | 6.5 | suboptimal allocation in multi-device filesystems due to chunk allocator regression | 6.1.60, 6.5.9, 6.6 and later | 8a540e990d7d btrfs: fix stripe length calculation for non-zoned data chunk allocation
| 6.3, backported to 5.15.107, 6.1.24, 6.2.11 | 6.3 | vmalloc error, failed to allocate pages | 6.3.10, 6.4 and later.  Bug (f349b15e183d "mm: vmalloc: avoid warn_alloc noise caused by fatal signal" in v6.3-rc6) backported to 6.1.24, 6.2.11, and 5.15.107. | 95a301eefa82 mm/vmalloc: do not output a spurious warning when huge vmalloc() fails
| 6.2 | 6.3 | `IGNORE_OFFSET` flag ignored in `LOGICAL_INO` ioctl | 6.2.16, 6.3.3, 6.4 and later | 0cad8f14d70c btrfs: fix backref walking not returning all inode refs
| 6.10 | 6.11 | `adding refs to an existing tree ref`, `failed to run delayed ref`, then read-only | 6.11.10, 6.12 and later | 7d493a5ecc26 btrfs: fix incorrect comparison for delayed refs
| 5.4 | - | kernel hang when multiple threads are running `LOGICAL_INO` and dedupe/clone ioctl on the same extent | - | workaround: avoid doing that

"Last bad kernel" refers to that version's last stable update from
kernel.org.  Distro kernels may backport additional fixes.  Consult
your distro's kernel support for details.

When the same version appears in both "last bad kernel" and "fixed kernel
version" columns, it means the bug appears in the `.0` release and is
fixed in the stated `.y` release.  e.g.  a "last bad kernel" of 5.4 and
a "fixed kernel version" of 5.4.14 has the bug in kernel versions 5.4.0
through 5.4.13 inclusive.

A "-" for "first bad kernel" indicates the bug has been present since
the relevant feature first appeared in btrfs.

A "-" for "last bad kernel" indicates the bug has not yet been fixed in
current kernels (see top of this page for which kernel version that is).

In cases where issues are fixed by commits spread out over multiple
kernel versions, "fixed kernel version" refers to the version that
contains the last committed component of the fix.


Workarounds for known kernel bugs
---------------------------------

* **Hangs with concurrent `LOGICAL_INO` and dedupe/clone**:  on all
  kernel versions so far, multiple threads running `LOGICAL_INO` and
  dedupe/clone ioctls at the same time on the same inodes or extents
  can lead to a kernel hang.  The kernel enters an infinite loop in
  `add_all_parents`, where `count` is 0, `ref->count` is 1, and
  `btrfs_next_item` or `btrfs_next_old_item` never find a matching ref.

  bees has two workarounds for this bug: 1. schedule work so that multiple
  threads do not simultaneously access the same inode or the same extent,
  and 2. use a brute-force global lock within bees that prevents any
  thread from running `LOGICAL_INO` while any other thread is running
  dedupe.

  Workaround #1 isn't really a workaround, since we want to do the same
  thing for unrelated performance reasons.  If multiple threads try to
  perform dedupe operations on the same extent or inode, btrfs will make
  all the threads wait for the same locks anyway, so it's better to have
  bees find some other inode or extent to work on while waiting for btrfs
  to finish.

  Workaround #2 doesn't seem to be needed after implementing workaround
  #1, but it's better to be slightly slower than to hang one CPU core
  and the filesystem until the kernel is rebooted.

  It is still theoretically possible to trigger the kernel bug when
  running bees at the same time as other dedupers, or other programs
  that use `LOGICAL_INO` like `btdu`, or when performing a reflink clone
  operation such as `cp` or `mv`; however, it's extremely difficult to
  reproduce the bug without closely cooperating threads.

* **Slow backrefs** (aka toxic extents):  On older kernels, under certain
  conditions, if the number of references to a single shared extent grows
  too high, the kernel consumes more and more CPU while also holding
  locks that delay write access to the filesystem.  This is no longer
  a concern on kernels after 5.7 (or an up-to-date 5.4 LTS version),
  but there are still some remains of earlier workarounds for this issue
  in bees that have not been fully removed.

  bees avoided this bug by measuring the time the kernel spends performing
  `LOGICAL_INO` operations and permanently blacklisting any extent or
  hash involved where the kernel starts to get slow.  In the bees log,
  such blocks are labelled as 'toxic' hash/block addresses.

  Future bees releases will remove toxic extent detection (it only detects
  false positives now) and clear all previously saved toxic extent bits.

* **dedupe breaks `btrfs send` in old kernels**.  The bees option
  `--workaround-btrfs-send` prevents any modification of read-only subvols
  in order to avoid breaking `btrfs send` on kernels before 5.2.

  This workaround is no longer necessary to avoid kernel crashes and
  send performance failure on kernel 5.4.4 and later.  bees will pause
  dedupe until the send is finished on current kernels.

  `btrfs receive` is not and has never been affected by this issue.
